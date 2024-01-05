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

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

#ifdef __linux__
#include <pthread.h>
#elif __APPLE__
#include <mach/mach.h>
#elif _WIN32
#include <Windows.h>
#endif

namespace dd {

struct CurrentThreadCpuClock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<CurrentThreadCpuClock, duration>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept;
};

struct ProcessCpuClock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<ProcessCpuClock, duration>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept;
};

class ThreadCpuClock {
 public:
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<ThreadCpuClock, duration>;

  static constexpr bool is_steady = true;

  ThreadCpuClock();
  time_point now() const noexcept;

 private:
#ifdef __linux__
  clockid_t clockid_;
#elif __APPLE__
  mach_port_t thread_;
#elif _WIN32
  HANDLE thread_;
#endif
};

class ThreadCpuStopWatch {
 public:
  ThreadCpuStopWatch() { last_ = clock_.now(); }

  ThreadCpuClock::duration GetAndReset() {
    auto now = clock_.now();
    auto d = now - last_;
    last_ = now;
    return d;
  }

 private:
  ThreadCpuClock clock_;
  ThreadCpuClock::time_point last_;
};

}  // namespace dd
