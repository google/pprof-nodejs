#pragma once

#include <stdint.h>
#include <time.h>

#ifdef __linux__
#include <pthread.h>
#elif __APPLE__
#include <mach/mach.h>
#elif _WIN32
#include <Windows.h>
#endif

namespace dd {

class CpuTime {
 private:
  struct timespec last_;
#ifdef __linux__
  clockid_t clockid;
#elif __APPLE__
  mach_port_t thread_;
#elif _WIN32
  HANDLE thread_;
#endif

 public:
  CpuTime(struct timespec time);
  CpuTime();
  int64_t Diff(struct timespec time);
  int64_t Diff();
  struct timespec Now();
};

} // namespace dd
