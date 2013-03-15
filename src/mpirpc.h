#ifndef MPIRPC_H_
#define MPIRPC_H_

#include <mpi.h>
#include <malloc.h>
#include <pth.h>
#include <signal.h>

#include <vector>
#include <map>
#include <boost/bind.hpp>
#include <boost/function.hpp>

#include <sys/mman.h>

#define LOG(...)\
    fprintf(stderr, "%s:%d -- ", __FILE__, __LINE__);\
    fprintf(stderr, ##__VA_ARGS__);\
    fprintf(stderr, "\n");

#define PANIC(...)\
    LOG("Something bad happened:");\
    LOG(__VA_ARGS__);\
    abort();

#define ASSERT(condition)\
    if (!(condition)) {\
        PANIC("Condition %s failed.", #condition);\
    }

static inline uint64_t rdtsc() {
  uint32_t hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return (((uint64_t) hi) << 32) | ((uint64_t) lo);
}

static inline double now() {
  timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return tp.tv_sec + 1e-9 * tp.tv_nsec;
}

typedef boost::function<void(void)> VoidFn;

namespace memutil {
const char* align_to_page(const char* offset);
void mark_protected(const char* offset, int count);
}

namespace fiber {
void* run_boost_fn(void* bound_fn);

pth_t run_in_fiber(VoidFn f);

void wait_for_all(std::vector<pth_t>& fibers);

// Run this function in a while(1) loop.
void run_forever(VoidFn f);

// Start a new fiber and run forever.
void run_forever_in_fiber(VoidFn f);
}
void segfault_handler(int signal_number, siginfo_t* info, void* ctx);

template<class T>
struct MPITypeMapper {
  static MPI::Datatype map();
};

template<>
struct MPITypeMapper<void> {
  static MPI::Datatype map() {
    return MPI::BYTE;
  }
};

template<>
struct MPITypeMapper<char> {
  static MPI::Datatype map() {
    return MPI::BYTE;
  }
};

template<>
struct MPITypeMapper<float> {
  static MPI::Datatype map() {
    return MPI::FLOAT;
  }
};

template<>
struct MPITypeMapper<double> {
  static MPI::Datatype map() {
    return MPI::DOUBLE;
  }
};

class PendingOps {
public:
  struct Info {
    MPI::Request request;
    pth_t worker;
    const char* base;
    ptrdiff_t count;

    bool in_range(const char* p) {
      return p >= base && p < (base + count);
    }
  };

  PendingOps() {
    _dummy = {MPI::REQUEST_NULL, NULL, NULL, 0};
  }

  void register_op(MPI::Request req, pth_t worker, const char* ptr, int num_bytes);

  // Wait for the request using the given range to be completed.
  // Once finished, remove the pending entry.
  void wait_for_op(const char* ptr);

  void wait_for_all();

  Info find_op(const char* ptr);
private:
  typedef std::map<ptrdiff_t, Info> WorkerMap;
  WorkerMap _active;
  Info _dummy;
};

extern PendingOps _pending;

class MPIRPC {
  int first_, last_;
  MPI::Intracomm world_;

  void init_zerocopy() {
    struct sigaction act;
    act.sa_sigaction = &segfault_handler;
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&act.sa_mask);
    sigaction(SIGSEGV, &act, NULL);
  }

  void init() {
    pth_init();
    init_zerocopy();
  }

public:
  MPIRPC() :
      first_(0), last_(MPI::COMM_WORLD.Get_size()), world_(MPI::COMM_WORLD) {
    init();
  }

  MPIRPC(int firstId, int lastId) :
      first_(firstId), last_(lastId), world_(MPI::COMM_WORLD) {
    init();
  }

  void wait() {
    _pending.wait_for_all();
  }

  template<class T>
  void send_sharded(int tag, const T* data, int num_elements);

  template<class T>
  void send_pod(int dst, int tag, const T& data);

  template<class T>
  void send(int dst, int tag, const T* data, int num_elements);

  template<class T>
  MPI::Request isend(int dst, int tag, const T* data, int num_elements);

  template<class T>
  void bsend(int dst, int tag, const T* data, int num_elements);

  template<class T>
  void send_zerocopy(int dst, int tag, const T* val, int num_elements);

  template<class T>
  void send_all(int tag, const T& data);

  void send_all(int tag, void* data, int num_bytes);

  template<class T>
  void recv_data(int src, int tag, T* data, int num_elements);

  template<class T>
  void recv_pod(int src, int tag, T* val);

  template<class T>
  void recv_all(int tag, std::vector<T>* res);

  template<class T>
  void recv_sharded(int tag, T* data, int num_elements);
};

template<class T>
inline void MPIRPC::send_zerocopy(int dst, int tag, const T* data, int num_elements) {
  // We don't support multiple sends of the same region yet.
  PendingOps::Info current = _pending.find_op((char*) data);
  if (current.base != NULL) {
    LOG("Existing worker %p registered for region %p[%d], blocking.", current.worker, data, num_elements);
    _pending.wait_for_op(data);
//    pth_join(current.worker, NULL);
  }

  MPI::Request req = world_.Isend(data, num_elements, MPITypeMapper<T>::map(), dst, tag);
  _pending.register_op(req, NULL, data, sizeof(T) * num_elements);

//  VoidFn f = boost::bind(&MPIRPC::send<T>, this, dst, tag, data, num_elements);
//  _pending.register_op(run_in_fiber(f), aligned, num_elements);
}

template<class T>
inline void MPIRPC::send_pod(int dst, int tag, const T& data) {
  send(dst, tag, (char*) (&data), sizeof(T));
}

template<class T>
inline void MPIRPC::send(int dst, int tag, const T* data, int num_elements) {
  MPI::Datatype type = MPITypeMapper<T>::map();
  MPI::Request pending = world_.Isend(data, num_elements, type, dst, tag);
  MPI::Status status;
  while (!pending.Test(status)) {
    pth_yield(NULL);
  }
  ASSERT(status.Get_count(type) == num_elements);
}

template<class T>
inline MPI::Request MPIRPC::isend(int dst, int tag, const T* data, int num_elements) {
  MPI::Datatype type = MPITypeMapper<T>::map();
  return world_.Isend(data, num_elements, type, dst, tag);
}

template<class T>
inline void MPIRPC::bsend(int dst, int tag, const T* data, int num_elements) {
  MPI::Datatype type = MPITypeMapper<T>::map();
  world_.Bsend(data, num_elements, type, dst, tag);
}

inline void MPIRPC::send_all(int tag, void* data, int num_bytes) {
  for (int i = first_; i <= last_; ++i) {
    send(i, tag, data, num_bytes);
  }
}

template<class T>
inline void MPIRPC::send_all(int tag, const T& data) {
  for (int i = first_; i <= last_; ++i) {
    send_pod(i, tag, data);
  }
}

template<class T>
inline void MPIRPC::send_sharded(int tag, const T* data, int num_elements) {
  int64_t num_servers = (last_ - first_ + 1);
  int64_t elems_per_server = num_elements / num_servers;
  for (int j = 0; j < num_servers; ++j) {
    int64_t offset = j * elems_per_server;
    int64_t next_offset = (j == num_servers - 1) ? num_elements : (j + 1) * elems_per_server;
    int dst = first_ + j;

//        fprintf(stderr, "%p %d %d %d %d\n", data, offset, next_offset, next_offset - offset, num_elements);
    send(dst, tag, data + offset, next_offset - offset);
  }
}

template<class T>
inline void MPIRPC::recv_data(int src, int tag, T* data, int num_elements) {
  MPI::Status status;
  MPI::Datatype type = MPITypeMapper<T>::map();
//  while (!world_.Iprobe(src, tag, status)) {
//    pth_yield(NULL);
//  }

//  ASSERT(status.Get_count(type) == num_elements);
  MPI::Request pending = world_.Irecv(data, num_elements, type, src, tag);
  pending.Wait();
//  while (!pending.Test(status)) {
//    pth_yield(NULL);
//  }
}

template<class T>
inline void MPIRPC::recv_pod(int src, int tag, T* val) {
  recv_data(src, tag, (char*) (val), sizeof(T));
}

template<class T>
inline void MPIRPC::recv_all(int tag, std::vector<T>* res) {
  res->resize(last_ - first_ + 1);
  for (int i = first_; i <= last_; ++i) {
    recv_pod(i, tag, &res->at(i - first_));
  }
}
template<class T>
inline void MPIRPC::recv_sharded(int tag, T* data, int num_elements) {
  int64_t num_servers = (last_ - first_ + 1);
  int64_t elems_per_server = num_elements / num_servers;
  for (int j = 0; j < num_servers; ++j) {
    int64_t offset = j * elems_per_server;
    int64_t next_offset = (j == num_servers - 1) ? num_elements : (j + 1) * elems_per_server;
    int src = first_ + j;
    recv_data(src, tag, data + offset, next_offset - offset);
  }
}

#endif /* MPIRPC_H_ */
