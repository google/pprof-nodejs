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

#include <nan.h>
#include <node.h>
#include <v8.h>
#include <memory>

namespace dd {

struct HeapProfilerState;

class PerIsolateData {
 private:
  Nan::Global<v8::Function> cpu_profiler_constructor;
  Nan::Global<v8::Function> location_constructor;
  Nan::Global<v8::Function> sample_constructor;
  Nan::Global<v8::Function> wall_profiler_constructor;
  std::shared_ptr<HeapProfilerState> heap_profiler_state;

  PerIsolateData() {}

 public:
  static PerIsolateData* For(v8::Isolate* isolate);

  Nan::Global<v8::Function>& CpuProfilerConstructor();
  Nan::Global<v8::Function>& LocationConstructor();
  Nan::Global<v8::Function>& SampleConstructor();
  Nan::Global<v8::Function>& WallProfilerConstructor();
  std::shared_ptr<HeapProfilerState>& GetHeapProfilerState();
};

}  // namespace dd
