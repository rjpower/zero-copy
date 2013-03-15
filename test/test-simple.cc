#include "mpirpc.h"
#include <string>

using namespace std;
int main(int argc, char** argv) {
  MPI::Init();
  static const long kMaxSize = 1 << 20;
  static const int kNumSends = 100;

  auto world = MPI::COMM_WORLD;
  MPIRPC rpc;
  string buffer;
  buffer.resize(kMaxSize);

  LOG("Testing send and recv up to %ld", kMaxSize);

  if (world.Get_rank() == 0) {
    for (int i = 1; i < kMaxSize; i <<= 1) {
      LOG("Sending: %5d", i);
      // Start sending.
      rpc.send_zerocopy(1, 0, buffer.data(), i);

      // Try tampering with the data
      buffer[0] = 1;
    }

    double start = now();
    for (int j = 0; j < kNumSends; ++j) {
      rpc.send_zerocopy(1, 0, buffer.data(), kMaxSize);
    }
    rpc.wait();
    double end = now();
    double bytes = kMaxSize * kNumSends;
    LOG("%fMB in %f seconds: %f MB/s", bytes / 1e6, end - start, bytes * 1e-6 / (end - start));

    vector<string> copies;
    vector<MPI::Request> reqs;
    start = now();
    for (int j = 0; j < kNumSends; ++j) {
      copies.push_back(buffer);
      reqs.push_back(rpc.isend(1, 0, copies.back().data(), copies.back().size()));
    }
    for (int j = 0; j < kNumSends; ++j) {
      reqs[j].Wait();
    }
    end = now();
    bytes = kMaxSize * kNumSends;
    LOG("%fMB in %f seconds: %f MB/s", bytes / 1e6, end - start, bytes * 1e-6 / (end - start));
  } else {
    for (int i = 1; i < kMaxSize; i <<= 1) {
      LOG("Receiving: %5d", i);
      rpc.recv_data(0, 0, &buffer[0], i);
    }
    for (int j = 0; j < kNumSends; ++j) {
      LOG("Receiving: %d : %5ld", j, kMaxSize);
      rpc.recv_data(0, 0, &buffer[0], kMaxSize);
    }
    for (int j = 0; j < kNumSends; ++j) {
      LOG("Receiving: %d : %5ld", j, kMaxSize);
      rpc.recv_data(0, 0, &buffer[0], kMaxSize);
    }
  }

  MPI::Finalize();
}
