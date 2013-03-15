#include "mpirpc.h"

namespace memutil {
const char* align_to_page(const char* p) {
  static const int kPageSize = sysconf(_SC_PAGESIZE);
  return p - (size_t) (p) % kPageSize;
}
}

namespace fiber {
void* run_boost_fn(void* bound_fn) {
  VoidFn* boost_fn = (VoidFn*) (bound_fn);
  (*boost_fn)();
  delete boost_fn;
  return NULL;
}

pth_t run_in_fiber(VoidFn f) {
  void* heap_fn = new VoidFn(f);
  pth_attr_t t_attr = pth_attr_new();
  pth_attr_init(t_attr);
  return pth_spawn(t_attr, run_boost_fn, heap_fn);
}

void wait_for_all(std::vector<pth_t>& fibers) {
  for (size_t i = 0; i < fibers.size(); ++i) {
    pth_join(fibers[i], NULL);
  }
}

// Run this function in a while(1) loop.
void run_forever(VoidFn f) {
  while (1) {
    f();
  }
}

void run_forever_in_fiber(VoidFn f) {
  run_in_fiber(boost::bind(&run_forever, f));
}
}

void segfault_handler(int signal_number, siginfo_t* info, void* ctx) {
  PendingOps::Info send_info = _pending.find_op((const char*) info->si_addr);
  if (send_info.base == NULL) {
    PANIC("Caught exception for an unmanaged region.");
  }

  LOG("Caught exception for region %p (%ld bytes).  Waiting for operation to complete.", send_info.base, send_info.count);
  _pending.wait_for_op(send_info.base);
}

void PendingOps::register_op(MPI::Request req, pth_t worker, const char* ptr, int num_bytes) {
  const char* aligned = memutil::align_to_page(ptr);
  ptrdiff_t aligned_bytes = ptr - aligned + num_bytes;
  mprotect((void*) aligned, aligned_bytes, PROT_READ | PROT_EXEC);
  Info info = { req, worker, aligned, aligned_bytes };
  _active[(ptrdiff_t) (aligned)] = info;
}

void PendingOps::wait_for_op(const char* ptr) {
  PendingOps::Info info = find_op(ptr);
  ASSERT(info.base != NULL);
  info.request.Wait();
  mprotect((char*) info.base, info.count, PROT_READ | PROT_WRITE | PROT_EXEC);
  _active.erase(_active.find((ptrdiff_t) (info.base)));
}

void PendingOps::wait_for_all() {
  for (auto i = _active.begin(); i != _active.end();) {
    PendingOps::Info info = i->second;
    info.request.Wait();
    mprotect((char*) info.base, info.count, PROT_READ | PROT_WRITE | PROT_EXEC);
    i = _active.erase(i);
  }
}

PendingOps::Info PendingOps::find_op(const char* ptr) {
  if (_active.size() == 0) {
    return _dummy;
  }
  WorkerMap::iterator i = _active.lower_bound((ptrdiff_t) (ptr));
  if (i->second.in_range(ptr)) {
    return i->second;
  }
  if (i == _active.begin()) {
    return _dummy;
  }
  --i;
  if (i->second.in_range(ptr)) {
    return i->second;
  }
  return _dummy;
}

PendingOps _pending;
