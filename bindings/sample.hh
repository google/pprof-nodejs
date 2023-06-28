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
#include <memory>
#include <vector>

#include "code-map.hh"
#include "wrap.hh"

#include <node_object_wrap.h>
#include <v8.h>

namespace dd {

class Sample : public Nan::ObjectWrap {
 private:
  std::shared_ptr<LabelWrap> labels_;
  uint64_t timestamp;
  std::vector<uintptr_t> frames;
  Nan::Global<v8::Array> locations_;
  int64_t cpu_time;

 public:
  Sample(v8::Isolate* isolate,
         std::shared_ptr<LabelWrap> labels,
         std::vector<uintptr_t> frames,
         int64_t cpu_time);

  Sample(v8::Isolate* isolate,
         std::shared_ptr<LabelWrap> labels,
         int64_t cpu_time);

  std::vector<uintptr_t> GetFrames();
  v8::Local<v8::Array> Symbolize(std::shared_ptr<CodeMap> code_map);

  v8::Local<v8::Integer> GetCpuTime(v8::Isolate* isolate);
  v8::Local<v8::Value> GetLabels(v8::Isolate* isolate);
  v8::Local<v8::Array> GetLocations(v8::Isolate* isolate);

  v8::Local<v8::Object> ToObject(v8::Isolate* isolate);

  static NAN_GETTER(GetCpuTime);
  static NAN_GETTER(GetLabels);
  static NAN_GETTER(GetLocations);

  static NAN_MODULE_INIT(Init);

  static const size_t frames_limit = 255;
};

}  // namespace dd
