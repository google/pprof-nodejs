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

#include <nan.h>
#include <node.h>
#include <v8.h>

#include "profilers/cpu.hh"
#include "profilers/heap.hh"
#include "profilers/wall.hh"

NODE_MODULE_INIT(/* exports, module, context */) {
  dd::CpuProfiler::Init(exports);
  dd::HeapProfiler::Init(exports);
  dd::WallProfiler::Init(exports);
}
