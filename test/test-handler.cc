#include <signal.h>
#include <memory.h>
#include <stdio.h>

#include "mpirpc.h"

static void my_segfault_handler(int sig, struct siginfo* info, void* ctx) {
  exit(0);
}

int main(int argc, char** argv) {
  struct sigaction act;
  act.sa_sigaction = &my_segfault_handler;
  act.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&act.sa_mask);
  sigaction(SIGSEGV, &act, NULL);

  int* test = (int*)NULL;
  test[0] = 1;
}
