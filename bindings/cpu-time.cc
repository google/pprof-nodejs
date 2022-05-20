#include "cpu-time.hh"

#ifdef __linux__
#include <errno.h>
#include <pthread.h>
#include <string.h>
#elif __APPLE__
#define _DARWIN_C_SOURCE
#include <mach/mach_error.h>
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#elif _WIN32
#include <Windows.h>
#endif

namespace dd {

CpuTime::CpuTime(struct timespec time) : last_(time) {
#ifdef __linux__
  pthread_getcpuclockid(pthread_self(), &clockid);
#elif __APPLE__
  thread_ = mach_thread_self();
#elif _WIN32
  thread_ = GetCurrentThread();
#endif
}

CpuTime::CpuTime()
  : CpuTime(Now()) {}

int64_t CpuTime::Diff(struct timespec now) {
  int64_t current = now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
  int64_t prev = last_.tv_sec * INT64_C(1000000000) + last_.tv_nsec;
  int64_t cpu_time = current - prev;

  last_ = now;

  return cpu_time;
}

int64_t CpuTime::Diff() {
  return Diff(Now());
}

struct timespec CpuTime::Now() {
  struct timespec cpu_time = {0, 0};

#ifdef __linux__
  if (clock_gettime(clockid, &cpu_time)) {
    return (struct timespec){0, 0};
  }
#elif __APPLE__
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  thread_basic_info_data_t info;
  kern_return_t kr =
    thread_info(thread_, THREAD_BASIC_INFO, (thread_info_t)&info, &count);

  if (kr != KERN_SUCCESS) {
    return cpu_time;
  }

  cpu_time = {
    // tv_sec
    info.user_time.seconds + info.system_time.seconds,
    // tv_nsec
    (info.user_time.microseconds + info.system_time.microseconds) * 1000,
  };
#elif _WIN32
  FILETIME a, b, c, d;
  if (!GetThreadTimes(thread_, &a, &b, &c, &d)) {
    return cpu_time;
  }

  // Convert 100-ns interval to nanooseconds
  uint64_t us = (((uint64_t)d.dwHighDateTime << 32) |
                (uint64_t)d.dwLowDateTime) * 100;

  cpu_time = {
    // tv_sec
    (time_t)(us / 1000000000),
    // tv_nsec
    (long)(us % 1000000000),
  };
#endif

  return cpu_time;
}

}
