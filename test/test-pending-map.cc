#include "mpirpc.h"
#include <string>

using namespace std;
int main(int argc, char** argv) {
  _pending.register_op(MPI::REQUEST_NULL, (pth_t) 1, (char*) 100, 100);
  ASSERT(_pending.find_op((char*)150).worker == (pth_t)1);

  _pending.register_op(MPI::REQUEST_NULL, (pth_t) 2, (char*) 200, 100);
  ASSERT(_pending.find_op((char*)250).worker == (pth_t)2);

  _pending.register_op(MPI::REQUEST_NULL, (pth_t) 3, (char*) 300, 100);
  ASSERT(_pending.find_op((char*)350).worker == (pth_t)3);
}
