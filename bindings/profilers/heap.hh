/**
 * Copyright 2018 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <nan.h>

namespace dd {

class HeapProfiler {
 public:
  // Signature:
  // startSamplingHeapProfiler()
  static NAN_METHOD(StartSamplingHeapProfiler);

  // Signature:
  // stopSamplingHeapProfiler()
  static NAN_METHOD(StopSamplingHeapProfiler);

  // Signature:
  // getAllocationProfile(): AllocationProfileNode
  static NAN_METHOD(GetAllocationProfile);

  static NAN_MODULE_INIT(Init);
};

} // namespace dd
