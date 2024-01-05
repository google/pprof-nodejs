/*
 * Copyright 2023 Datadog, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "thread-cpu-clock.hh"

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

namespace {
constexpr std::chrono::nanoseconds timespec_to_duration(timespec ts) {
  return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

#ifdef _WIN32
constexpr std::chrono::nanoseconds filetime_to_nanos(FILETIME t) {
  return std::chrono::nanoseconds{
      ((static_cast<uint64_t>(t.dwHighDateTime) << 32) |
       static_cast<uint64_t>(t.dwLowDateTime)) *
      100};
}
#endif
}  // namespace

CurrentThreadCpuClock::time_point CurrentThreadCpuClock::now() noexcept {
#ifndef _WIN32
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return time_point{timespec_to_duration(ts)};
#else
  FILETIME creationTime, exitTime, kernelTime, userTime;
  if (!GetThreadTimes(GetCurrentThread(),
                      &creationTime,
                      &exitTime,
                      &kernelTime,
                      &userTime)) {
    return {};
  }
  return time_point{filetime_to_nanos(kernelTime) +
                    filetime_to_nanos(userTime)};
#endif
}

ProcessCpuClock::time_point ProcessCpuClock::now() noexcept {
#ifndef _WIN32
  timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return time_point{timespec_to_duration(ts)};
#else
  FILETIME creationTime, exitTime, kernelTime, userTime;
  if (!GetProcessTimes(GetCurrentProcess(),
                       &creationTime,
                       &exitTime,
                       &kernelTime,
                       &userTime)) {
    return {};
  }
  return time_point{filetime_to_nanos(kernelTime) +
                    filetime_to_nanos(userTime)};
#endif
}

ThreadCpuClock::ThreadCpuClock() {
#ifdef __linux__
  pthread_getcpuclockid(pthread_self(), &clockid_);
#elif __APPLE__
  thread_ = mach_thread_self();
#elif _WIN32
  thread_ = GetCurrentThread();
#endif
}

ThreadCpuClock::time_point ThreadCpuClock::now() const noexcept {
#ifdef __linux__
  timespec ts;
  if (clock_gettime(clockid_, &ts)) {
    return {};
  }
  return time_point{timespec_to_duration(ts)};
#elif __APPLE__
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  thread_basic_info_data_t info;
  kern_return_t kr =
      thread_info(thread_, THREAD_BASIC_INFO, (thread_info_t)&info, &count);

  if (kr != KERN_SUCCESS) {
    return {};
  }

  return time_point{
      std::chrono::seconds{info.user_time.seconds + info.system_time.seconds} +
      std::chrono::microseconds{info.user_time.microseconds +
                                info.system_time.microseconds}};
#elif _WIN32
  FILETIME creationTime, exitTime, kernelTime, userTime;
  if (!GetThreadTimes(
          thread_, &creationTime, &exitTime, &kernelTime, &userTime)) {
    return {};
  }
  return time_point{filetime_to_nanos(kernelTime) +
                    filetime_to_nanos(userTime)};
#endif

  return {};
}

}  // namespace dd
