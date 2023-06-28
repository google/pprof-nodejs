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

#include "labelsets.hh"

#include <nan.h>
#include <v8-profiler.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <utility>

namespace dd {

struct Result {
  Result() = default;
  explicit Result(const char* msg) : success{false}, msg{msg} {};

  bool success = true;
  std::string msg;
};

class WallProfiler : public Nan::ObjectWrap {
 private:
  using ValuePtr = std::shared_ptr<v8::Global<v8::Value>>;

  int samplingPeriodMicros_ = 0;
  v8::CpuProfiler* cpuProfiler_ = nullptr;
  // TODO: Investigate use of v8::Persistent instead of shared_ptr<Global> to
  // avoid heap allocation. Need to figure out the right move/copy semantics in
  // and out of the ring buffer.

  // We're using a pair of shared pointers and an atomic pointer-to-current as
  // a way to ensure signal safety on update.
  ValuePtr labels1_;
  ValuePtr labels2_;
  std::atomic<ValuePtr*> curLabels_;
  std::atomic<bool> collectSamples_;
  std::string profileId_;
  int64_t profileIdx_ = 0;
  bool includeLines_ = false;
  bool withLabels_ = false;
  bool started_ = false;

  struct SampleContext {
    ValuePtr labels;
    int64_t time_from;
    int64_t time_to;
  };

  using ContextBuffer = std::vector<SampleContext>;
  ContextBuffer contexts_;

  ~WallProfiler();
  void Dispose(v8::Isolate* isolate);

  // A new CPU profiler object will be created each time profiling is started
  // to work around https://bugs.chromium.org/p/v8/issues/detail?id=11051.
  v8::CpuProfiler* CreateV8CpuProfiler();

  LabelSetsByNode GetLabelSetsByNode(v8::CpuProfile* profile,
                                     ContextBuffer& contexts);

 public:
  /**
   * @param samplingPeriodMicros sampling interval, in microseconds
   * @param durationMicros the duration of sampling, in microseconds. This
   * parameter is informative; it is up to the caller to call the Stop method
   * every period. The parameter is used to preallocate data structures that
   * should not be reallocated in async signal safe code.
   */
  explicit WallProfiler(int samplingPeriodMicros,
                        int durationMicros,
                        bool includeLines,
                        bool withLabels);

  v8::Local<v8::Value> GetLabels(v8::Isolate*);
  void SetLabels(v8::Isolate*, v8::Local<v8::Value>);

  void PushContext(int64_t time_from, int64_t time_to);
  Result StartImpl();
  std::string StartInternal();
  Result StopImpl(bool restart, v8::Local<v8::Value>& profile);
  Result StopImplOld(bool restart, v8::Local<v8::Value>& profile);

  bool collectSampleAllowed() const {
    bool res = collectSamples_.load(std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_acquire);
    return res;
  }

  static NAN_METHOD(New);
  static NAN_METHOD(Start);
  static NAN_METHOD(Stop);
  static NAN_MODULE_INIT(Init);
  static NAN_GETTER(GetLabels);
  static NAN_SETTER(SetLabels);
};

}  // namespace dd
