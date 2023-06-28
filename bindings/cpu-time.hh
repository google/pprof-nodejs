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

}  // namespace dd
