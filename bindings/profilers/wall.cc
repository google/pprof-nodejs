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

#include <cinttypes>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <nan.h>
#include <node.h>
#include <v8-profiler.h>

#include "per-isolate-data.hh"
#include "translate-time-profile.hh"
#include "wall.hh"

#ifndef _WIN32
#define DD_WALL_USE_SIGPROF true

// Declare v8::base::TimeTicks::Now. It is exported from the node executable so
// our addon will be able to dynamically link to the symbol when loaded.
namespace v8 {
namespace base {
struct TimeTicks {
  static int64_t Now();
};
}  // namespace base
}  // namespace v8

static int64_t Now() {
  return v8::base::TimeTicks::Now();
};

#else
#define DD_WALL_USE_SIGPROF false
static int64_t Now() {
  return 0;
};

#endif

using namespace v8;

namespace dd {

// Maximum number of rounds in the GetV8ToEpochOffset
static constexpr int MAX_EPOCH_OFFSET_ATTEMPTS = 20;

bool isIdleOrProgram(const v8::CpuProfileNode* node) {
  auto* function_name = node->GetFunctionNameStr();
  return strcmp(function_name, "(idle)") == 0 ||
         strcmp(function_name, "(program)") == 0;
}

int getTotalHitCount(const v8::CpuProfileNode* node, bool* noHitLeaf) {
  int count = node->GetHitCount();
  auto child_count = node->GetChildrenCount();

  for (int i = 0; i < child_count; ++i) {
    count += getTotalHitCount(node->GetChild(i), noHitLeaf);
  }
  if (child_count == 0 && count == 0) {
    *noHitLeaf = true;
  }
  return count;
}

/** Returns 0 if no bug detected, 1 if possible bug (it could be a false
 * positive), 2 if bug detected for certain. */
int detectV8Bug(const v8::CpuProfile* profile) {
  /* When the profiler operates correctly, there'll be at least one node with
   * a non-zero hit count and the number of samples will be strictly greater
   * than the number of hits because they'll contain at least the starting
   * sample and potentially some deoptimization samples. If these conditions
   * don't hold, it implies that v8::SamplingEventsProcessor::ProcessOneSample
   * loop is stuck for ticks_buffer_ or vm_ticks_buffer_. */

  bool noHitLeaf = false;
  auto totalHitCount = getTotalHitCount(profile->GetTopDownRoot(), &noHitLeaf);
  if (totalHitCount == 0) {
    return 2;
  }

  if (profile->GetSamplesCount() == totalHitCount && !noHitLeaf) {
    /*  Checking number of samples against number of hits potentially leads to
     * false positive because some ticks samples can be discarded if their
     * timestamp is older than profile start time because of queueing.
     * Additionally check for leaf nodes with zero hit count, if there is one,
     * this implies that one non-tick sample was processed.
     */
    return 1;
  }
  return 0;
}

class ProtectedProfilerMap {
 public:
  WallProfiler* GetProfiler(const Isolate* isolate) const {
    // Prevent updates to profiler map by atomically setting g_profilers to null
    auto prof_map = profilers_.exchange(nullptr, std::memory_order_acq_rel);
    if (!prof_map) {
      return nullptr;
    }
    auto prof_it = prof_map->find(isolate);
    WallProfiler* profiler = nullptr;
    if (prof_it != prof_map->end()) {
      profiler = prof_it->second;
    }
    // Allow updates
    profilers_.store(prof_map, std::memory_order_release);
    return profiler;
  }

  bool RemoveProfiler(const v8::Isolate* isolate, WallProfiler* profiler) {
    return UpdateProfilers([isolate, profiler, this](auto map) {
      terminatedWorkersCpu_ += profiler->GetAndResetThreadCpu();

      if (isolate != nullptr) {
        auto it = map->find(isolate);
        if (it != map->end() && it->second == profiler) {
          map->erase(it);
          return true;
        }
      } else {
        auto it = std::find_if(map->begin(), map->end(), [profiler](auto& x) {
          return x.second == profiler;
        });
        if (it != map->end()) {
          map->erase(it);
          return true;
        }
      }
      return false;
    });
  }

  bool AddProfiler(const v8::Isolate* isolate, WallProfiler* profiler) {
    return UpdateProfilers([isolate, profiler](auto map) {
      return map->emplace(isolate, profiler).second;
    });
  }

  ThreadCpuClock::duration GatherTotalWorkerCpuAndReset() {
    std::lock_guard<std::mutex> lock(update_mutex_);

    // Retrieve CPU of workers that have terminated during the last period
    ThreadCpuClock::duration totalWorkerCpu = terminatedWorkersCpu_;

    // Reset terminated workers cpu to 0
    terminatedWorkersCpu_ = ThreadCpuClock::duration::zero();

    if (!init_) {
      return totalWorkerCpu;
    }

    auto currProfilers = profilers_.load(std::memory_order_acquire);
    // Wait until sighandler is done using the map
    while (!currProfilers) {
      currProfilers = profilers_.load(std::memory_order_relaxed);
    }

    // Gather CPU of workers that are still running
    for (auto& profiler : *currProfilers) {
      totalWorkerCpu += profiler.second->GetAndResetThreadCpu();
    }

    return totalWorkerCpu;
  }

 private:
  template <typename F>
  bool UpdateProfilers(F updateFn) {
    // use mutex to prevent two isolates of updating profilers concurrently
    std::lock_guard<std::mutex> lock(update_mutex_);

    if (!init_) {
      profilers_.store(new ProfilerMap(), std::memory_order_release);
      init_ = true;
    }

    auto currProfilers = profilers_.load(std::memory_order_acquire);
    // Wait until sighandler is done using the map
    while (!currProfilers) {
      currProfilers = profilers_.load(std::memory_order_relaxed);
    }
    auto newProfilers = new ProfilerMap(*currProfilers);
    auto res = updateFn(newProfilers);
    // Wait until sighandler is done using the map before installing a new map.
    // The value in profilers is either nullptr or currProfilers.
    for (;;) {
      ProfilerMap* currProfilers2 = currProfilers;
      if (profilers_.compare_exchange_weak(
              currProfilers2, newProfilers, std::memory_order_acq_rel)) {
        break;
      }
    }
    delete currProfilers;
    return res;
  }

  using ProfilerMap = std::unordered_map<const Isolate*, WallProfiler*>;
  mutable std::atomic<ProfilerMap*> profilers_;
  std::mutex update_mutex_;
  bool init_ = false;
  std::chrono::nanoseconds terminatedWorkersCpu_{};
};

using ProfilerMap = std::unordered_map<Isolate*, WallProfiler*>;

static ProtectedProfilerMap g_profilers;

namespace {

#if DD_WALL_USE_SIGPROF
class SignalHandler {
 public:
  static void IncreaseUseCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++use_count_;
    // Always reinstall the signal handler
    Install();
  }

  static void DecreaseUseCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (--use_count_ == 0) {
      Restore();
    }
  }

  static bool Installed() {
    std::lock_guard<std::mutex> lock(mutex_);
    return installed_;
  }

 private:
  static void Install() {
    struct sigaction sa;
    sa.sa_sigaction = &HandleProfilerSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK;
    if (installed_) {
      sigaction(SIGPROF, &sa, nullptr);
    } else {
      installed_ = (sigaction(SIGPROF, &sa, &old_handler_) == 0);
      old_handler_func_.store(old_handler_.sa_sigaction,
                              std::memory_order_relaxed);
    }
  }

  static void Restore() {
    if (installed_) {
      sigaction(SIGPROF, &old_handler_, nullptr);
      installed_ = false;
      old_handler_func_.store(nullptr, std::memory_order_relaxed);
    }
  }

  static void HandleProfilerSignal(int signal, siginfo_t* info, void* context);

  // Protects the process wide state below.
  static std::mutex mutex_;
  static int use_count_;
  static bool installed_;
  static struct sigaction old_handler_;
  using HandlerFunc = void (*)(int, siginfo_t*, void*);
  static std::atomic<HandlerFunc> old_handler_func_;
};

std::mutex SignalHandler::mutex_;
int SignalHandler::use_count_ = 0;
struct sigaction SignalHandler::old_handler_;
bool SignalHandler::installed_ = false;
std::atomic<SignalHandler::HandlerFunc> SignalHandler::old_handler_func_;

void SignalHandler::HandleProfilerSignal(int sig,
                                         siginfo_t* info,
                                         void* context) {
  auto old_handler = old_handler_func_.load(std::memory_order_relaxed);

  if (!old_handler) {
    return;
  }
  auto isolate = Isolate::GetCurrent();
  WallProfiler* prof = g_profilers.GetProfiler(isolate);

  if (!prof) {
    // no profiler found for current isolate, just pass the signal to old
    // handler
    old_handler(sig, info, context);
    return;
  }

  auto mode = prof->collectionMode();
  if (mode == WallProfiler::CollectionMode::kNoCollect) {
    return;
  } else if (mode == WallProfiler::CollectionMode::kPassThrough) {
    old_handler(sig, info, context);
    return;
  }

  int64_t cpu_time = 0;
  if (prof->collectCpuTime()) {
    cpu_time = CurrentThreadCpuClock::now().time_since_epoch().count();
  }
  auto time_from = Now();
  old_handler(sig, info, context);
  auto time_to = Now();
  double async_id = node::AsyncHooksGetExecutionAsyncId(isolate);
  prof->PushContext(time_from, time_to, cpu_time, async_id);
}
#else
class SignalHandler {
 public:
  static void IncreaseUseCount() {}
  static void DecreaseUseCount() {}
};
#endif
}  // namespace

static_assert((-1L >> 1) == -1L, "Right shift is not arithmetic");

static int64_t midpoint(int64_t x, int64_t y) {
  // TODO: remove when we're on C++20 as it has a built-in midpoint
  return ((x ^ y) >> 1) + (x & y);
}

static int64_t GetV8ToEpochOffset() {
  using namespace std::chrono;
  // Make a best effort to capture the difference between UNIX epoch and the V8
  // profiling timer as precisely as possible. Will make at most 20 attempts to
  // capture the epoch time within the same V8 microsecond and use the one with
  // the smallest error. We repeat this every time we gather a profile (so,
  // every minute) instead of once statically, as the difference doesn't
  // necessarily remain constant depending on the characteristics of the clocks
  // being used.
  int64_t V8toEpochOffset = 0;
  int64_t smallestDiff = std::numeric_limits<int64_t>::max();
  for (int i = 0; i < MAX_EPOCH_OFFSET_ATTEMPTS; ++i) {
    auto v8Now = Now();
    auto epochNow =
        duration_cast<microseconds>(system_clock::now().time_since_epoch())
            .count();
    auto v8Now2 = Now();
    auto diff = v8Now2 - v8Now;
    if (diff < smallestDiff) {
      V8toEpochOffset = epochNow - midpoint(v8Now, v8Now2);
      if (diff == 0) {
        break;
      }
      smallestDiff = diff;
    }
  }
  return V8toEpochOffset;
}

ContextsByNode WallProfiler::GetContextsByNode(CpuProfile* profile,
                                               ContextBuffer& contexts,
                                               int64_t startCpuTime) {
  ContextsByNode contextsByNode;

  auto sampleCount = profile->GetSamplesCount();
  if (contexts.empty() || sampleCount == 0) {
    return contextsByNode;
  }

  auto isolate = Isolate::GetCurrent();
  auto v8Context = isolate->GetCurrentContext();
  auto contextIt = contexts.begin();

  // deltaIdx is the offset of the sample to process compared to current
  // iteration index
  int deltaIdx = 0;

  auto contextKey = String::NewFromUtf8Literal(isolate, "context");
  auto timestampKey = String::NewFromUtf8Literal(isolate, "timestamp");
  auto cpuTimeKey = String::NewFromUtf8Literal(isolate, "cpuTime");
  auto asyncIdKey = String::NewFromUtf8Literal(isolate, "asyncId");
  auto V8toEpochOffset = GetV8ToEpochOffset();
  auto lastCpuTime = startCpuTime;

  // skip first sample because it's the one taken on profiler start, outside of
  // signal handler
  for (int i = 1; i < sampleCount; i++) {
    // Handle out-of-order samples, hypothesis is that at most 2 consecutive
    // samples can be out-of-order
    if (deltaIdx == 1) {
      // previous iteration was processing next sample, so this one should
      // process previous sample
      deltaIdx = -1;
    } else if (deltaIdx == -1) {
      // previous iteration was processing previous sample, returns to normal
      // index
      deltaIdx = 0;
    } else if (i < sampleCount - 1 && profile->GetSampleTimestamp(i + 1) <
                                          profile->GetSampleTimestamp(i)) {
      // detected  out-of-order sample, process next sample
      deltaIdx = 1;
    }

    auto sampleIdx = i + deltaIdx;
    auto sample = profile->GetSample(sampleIdx);

    auto sampleTimestamp = profile->GetSampleTimestamp(sampleIdx);

    // This loop will drop all contexts that are too old to be associated with
    // the current sample; association is done by matching each sample with
    // context whose [time_from,time_to] interval encompasses sample timestamp.
    while (contextIt != contexts.end()) {
      auto& sampleContext = *contextIt;
      if (sampleContext.time_to < sampleTimestamp) {
        // Current sample context is too old, discard it and fetch the next one.
        ++contextIt;
      } else if (sampleContext.time_from > sampleTimestamp) {
        // Current sample context is too recent, we'll try to match it to the
        // next sample.
        break;
      } else {
        // This sample context is the closest to this sample.
        auto it = contextsByNode.find(sample);
        Local<Array> array;
        if (it == contextsByNode.end()) {
          array = Array::New(isolate);
          contextsByNode[sample] = {array, 1};
        } else {
          array = it->second.contexts;
          ++it->second.hitcount;
        }
        if (sampleContext.context) {
          // Conforms to TimeProfileNodeContext defined in v8-types.ts
          Local<Object> timedContext = Object::New(isolate);
          timedContext
              ->Set(v8Context,
                    contextKey,
                    sampleContext.context.get()->Get(isolate))
              .Check();
          timedContext
              ->Set(v8Context,
                    timestampKey,
                    BigInt::New(isolate, sampleTimestamp + V8toEpochOffset))
              .Check();

          // if current sample is idle/program, reports its cpu time to the next
          // sample
          if (collectCpuTime_ && !isIdleOrProgram(sample)) {
            timedContext
                ->Set(
                    v8Context,
                    cpuTimeKey,
                    Number::New(isolate, sampleContext.cpu_time - lastCpuTime))
                .Check();
            lastCpuTime = sampleContext.cpu_time;
          }
          timedContext
              ->Set(v8Context,
                    asyncIdKey,
                    Number::New(isolate, sampleContext.async_id))
              .Check();
          array->Set(v8Context, array->Length(), timedContext).Check();
        }

        // Sample context was consumed, fetch the next one
        ++contextIt;
        break;  // don't match more than one context to one sample
      }
    }
  }

  return contextsByNode;
}

WallProfiler::WallProfiler(std::chrono::microseconds samplingPeriod,
                           std::chrono::microseconds duration,
                           bool includeLines,
                           bool withContexts,
                           bool workaroundV8Bug,
                           bool collectCpuTime,
                           bool isMainThread)
    : samplingPeriod_(samplingPeriod),
      includeLines_(includeLines),
      withContexts_(withContexts),
      isMainThread_(isMainThread) {
  // Try to workaround V8 bug where profiler event processor loop becomes stuck.
  // When starting a new profile, wait for one signal before and one signal
  // after to reduce the likelihood that race condition occurs and one code
  // event just after triggers the issue.
  workaroundV8Bug_ = workaroundV8Bug && DD_WALL_USE_SIGPROF && detectV8Bug_;
  collectCpuTime_ = collectCpuTime && withContexts;

  if (withContexts_) {
    contexts_.reserve(duration * 2 / samplingPeriod);
  }

  curContext_.store(&context1_, std::memory_order_relaxed);
  collectionMode_.store(CollectionMode::kNoCollect, std::memory_order_relaxed);

  auto isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::ArrayBuffer> buffer =
      v8::ArrayBuffer::New(isolate, sizeof(uint32_t) * kFieldCount);

  v8::Local<v8::Uint32Array> jsArray =
      v8::Uint32Array::New(buffer, 0, kFieldCount);
  fields_ = static_cast<uint32_t*>(buffer->GetBackingStore()->Data());
  jsArray_ = v8::Global<v8::Uint32Array>(isolate, jsArray);
  std::fill(fields_, fields_ + kFieldCount, 0);
}

WallProfiler::~WallProfiler() {
  Dispose(nullptr);
}

void WallProfiler::Dispose(Isolate* isolate) {
  if (cpuProfiler_ != nullptr) {
    cpuProfiler_->Dispose();
    cpuProfiler_ = nullptr;

    g_profilers.RemoveProfiler(isolate, this);
  }
}

NAN_METHOD(WallProfiler::New) {
  if (info.Length() != 1 || !info[0]->IsObject()) {
    return Nan::ThrowTypeError("WallProfiler must have one object argument.");
  }

  if (info.IsConstructCall()) {
    auto arg = info[0].As<v8::Object>();
    auto intervalMicrosValue =
        Nan::Get(arg, Nan::New<v8::String>("intervalMicros").ToLocalChecked());
    if (intervalMicrosValue.IsEmpty() ||
        !intervalMicrosValue.ToLocalChecked()->IsNumber()) {
      return Nan::ThrowTypeError("intervalMicros must be a number.");
    }

    std::chrono::microseconds interval{
        intervalMicrosValue.ToLocalChecked().As<v8::Integer>()->Value()};

    if (interval <= std::chrono::microseconds::zero()) {
      return Nan::ThrowTypeError("Sample rate must be positive.");
    }

    auto durationMillisValue =
        Nan::Get(arg, Nan::New<v8::String>("durationMillis").ToLocalChecked());
    if (durationMillisValue.IsEmpty() ||
        !durationMillisValue.ToLocalChecked()->IsNumber()) {
      return Nan::ThrowTypeError("durationMillis must be a number.");
    }

    std::chrono::milliseconds duration{
        durationMillisValue.ToLocalChecked().As<v8::Integer>()->Value()};

    if (duration <= std::chrono::microseconds::zero()) {
      return Nan::ThrowTypeError("Duration must be positive.");
    }
    if (duration < interval) {
      return Nan::ThrowTypeError("Duration must not be less than sample rate.");
    }

    auto lineNumbersValue =
        Nan::Get(arg, Nan::New<v8::String>("lineNumbers").ToLocalChecked());
    if (lineNumbersValue.IsEmpty() ||
        !lineNumbersValue.ToLocalChecked()->IsBoolean()) {
      return Nan::ThrowTypeError("lineNumbers must be a boolean.");
    }
    bool lineNumbers =
        lineNumbersValue.ToLocalChecked().As<v8::Boolean>()->Value();

    auto withContextsValue =
        Nan::Get(arg, Nan::New<v8::String>("withContexts").ToLocalChecked());
    if (withContextsValue.IsEmpty() ||
        !withContextsValue.ToLocalChecked()->IsBoolean()) {
      return Nan::ThrowTypeError("withContext must be a boolean.");
    }
    bool withContexts =
        withContextsValue.ToLocalChecked().As<v8::Boolean>()->Value();

    auto workaroundV8BugValue =
        Nan::Get(arg, Nan::New<v8::String>("workaroundV8Bug").ToLocalChecked());
    if (workaroundV8BugValue.IsEmpty() ||
        !workaroundV8BugValue.ToLocalChecked()->IsBoolean()) {
      return Nan::ThrowTypeError("workaroundV8Bug must be a boolean.");
    }
    bool workaroundV8Bug =
        workaroundV8BugValue.ToLocalChecked().As<v8::Boolean>()->Value();

    auto collectCpuTimeValue =
        Nan::Get(arg, Nan::New<v8::String>("collectCpuTime").ToLocalChecked());
    if (collectCpuTimeValue.IsEmpty() ||
        !collectCpuTimeValue.ToLocalChecked()->IsBoolean()) {
      return Nan::ThrowTypeError("collectCpuTime must be a boolean.");
    }
    bool collectCpuTime =
        collectCpuTimeValue.ToLocalChecked().As<v8::Boolean>()->Value();

    auto isMainThreadValue =
        Nan::Get(arg, Nan::New<v8::String>("isMainThread").ToLocalChecked());
    if (isMainThreadValue.IsEmpty() ||
        !isMainThreadValue.ToLocalChecked()->IsBoolean()) {
      return Nan::ThrowTypeError("isMainThread must be a boolean.");
    }
    bool isMainThread =
        isMainThreadValue.ToLocalChecked().As<v8::Boolean>()->Value();

    if (withContexts && !DD_WALL_USE_SIGPROF) {
      return Nan::ThrowTypeError("Contexts are not supported.");
    }

    if (collectCpuTime && !withContexts) {
      return Nan::ThrowTypeError("Cpu time collection requires contexts.");
    }

    if (lineNumbers && withContexts) {
      // Currently custom contexts are not compatible with caller line
      // information, because it's not possible to associate context with line
      // ticks:
      // context is associated to sample which itself is associated with
      // a CpuProfileNode, but if node has several line ticks, then we cannot
      // determine context <-> line ticks association. Note that line number is
      // present in v8 internal sample struct and would allow mapping sample to
      // line tick, and thus context to line tick, but this information is not
      // available in v8 public API.
      // Moreover in caller line number mode, line number of a CpuProfileNode
      // is not the line of the current function, but the line number where this
      // function is called, therefore we don't have access either to the line
      // of the function (otherwise we could ignore line ticks and replace them
      // with single hit count for the function).
      return Nan::ThrowTypeError(
          "Include line option is not compatible with contexts.");
    }

    WallProfiler* obj = new WallProfiler(interval,
                                         duration,
                                         lineNumbers,
                                         withContexts,
                                         workaroundV8Bug,
                                         collectCpuTime,
                                         isMainThread);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    v8::Local<v8::Value> arg = info[0];
    v8::Local<v8::Function> cons = Nan::New(
        PerIsolateData::For(info.GetIsolate())->WallProfilerConstructor());
    info.GetReturnValue().Set(Nan::NewInstance(cons, 1, &arg).ToLocalChecked());
  }
}

NAN_METHOD(WallProfiler::Start) {
  WallProfiler* wallProfiler =
      Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());

  if (info.Length() != 0) {
    return Nan::ThrowTypeError("Start must not have any arguments.");
  }

  auto res = wallProfiler->StartImpl();
  if (!res.success) {
    return Nan::ThrowTypeError(res.msg.c_str());
  }
}

Result WallProfiler::StartImpl() {
  if (started_) {
    return Result{"Start called on already started profiler, stop it first."};
  }

  profileIdx_ = 0;

  if (!CreateV8CpuProfiler()) {
    return Result{"Cannot start profiler: another profiler is already active."};
  }

  profileId_ = StartInternal();

  auto collectionMode = withContexts_
                            ? CollectionMode::kCollectContexts
                            : (workaroundV8Bug_ ? CollectionMode::kPassThrough
                                                : CollectionMode::kNoCollect);
  collectionMode_.store(collectionMode, std::memory_order_relaxed);
  started_ = true;
  return {};
}

std::string WallProfiler::StartInternal() {
  // Reuse the same names for the profiles because strings used for profile
  // names are not released until v8::CpuProfiler object is destroyed.
  // https://github.com/nodejs/node/blob/b53c51995380b1f8d642297d848cab6010d2909c/deps/v8/src/profiler/profile-generator.h#L516
  char buf[128];
  snprintf(buf, sizeof(buf), "pprof-%" PRId64, (profileIdx_++) % 2);
  v8::Local<v8::String> title = Nan::New<String>(buf).ToLocalChecked();
  cpuProfiler_->StartProfiling(
      title,
      includeLines_ ? CpuProfilingMode::kCallerLineNumbers
                    : CpuProfilingMode::kLeafNodeLineNumbers,
      // Always record samples in order to be able to check if non tick samples
      // (ie. starting or deopt samples) have been processed, and therefore if
      // SamplingEventsProcessor::ProcessOneSample is stuck on vm_ticks_buffer_.
      withContexts_ || detectV8Bug_);

  // reinstall sighandler on each new upload period
  if (withContexts_ || workaroundV8Bug_) {
    SignalHandler::IncreaseUseCount();
    fields_[kSampleCount] = 0;
  }

  if (collectCpuTime_) {
    startThreadCpuTime_ =
        CurrentThreadCpuClock::now().time_since_epoch().count();
    startProcessCpuTime_ = ProcessCpuClock::now();
  }

  // Force collection of two other non-tick samples (ie. that will not add to
  // hit count).
  // This is to be able to detect when v8 profiler event processor loop is
  // stuck on ticks_from_vm_buffer_.
  // A non-tick sample is already taken upon profiling start, and should be
  // enough to determine if a non-tick sample has been processed at the end by
  // comparing number of samples with total hit count.
  // The first tick sample might be discarded though if its timestamp is older
  // than profile start time due to queueing and in that case it is still added
  // to hit count but not to the sample array, leading to incorrectly detect
  // that ticks_from_vm_buffer_ is stuck.
  // This is not needed when workaroundV8Bug_ is enabled because in that case,
  // we wait for one signal before starting a new profile which should leave
  // time to process in-flight tick samples.
  if (detectV8Bug_ && !workaroundV8Bug_) {
    cpuProfiler_->CollectSample(v8::Isolate::GetCurrent());
    cpuProfiler_->CollectSample(v8::Isolate::GetCurrent());
  }

  return buf;
}

NAN_METHOD(WallProfiler::Stop) {
  if (info.Length() != 1) {
    return Nan::ThrowTypeError("Stop must have one argument.");
  }
  if (!info[0]->IsBoolean()) {
    return Nan::ThrowTypeError("Restart must be a boolean.");
  }

  bool restart = info[0].As<Boolean>()->Value();

  WallProfiler* wallProfiler =
      Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());

  v8::Local<v8::Value> profile;
  auto err = wallProfiler->StopImpl(restart, profile);

  if (!err.success) {
    return Nan::ThrowTypeError(err.msg.c_str());
  }
  info.GetReturnValue().Set(profile);
}

bool WallProfiler::waitForSignal(uint64_t targetCallCount) {
  auto currentCallCount = noCollectCallCount_.load(std::memory_order_relaxed);
  std::atomic_signal_fence(std::memory_order_acquire);
  if (targetCallCount != 0) {
    // check if target call count already reached
    if (currentCallCount >= targetCallCount) {
      return true;
    }
  } else {
    // no target call count in input, wait for the next signal
    targetCallCount = currentCallCount + 1;
  }
#if DD_WALL_USE_SIGPROF
  const int maxRetries = 2;
  // wait for a maximum of 2 sample periods
  // if a signal occurs it will interrupt sleep (we use nanosleep and not
  // uv_sleep because we want this behaviour)
  timespec ts = {
      0, std::chrono::nanoseconds(samplingPeriod_ * maxRetries).count()};
  nanosleep(&ts, nullptr);
#endif
  auto res =
      noCollectCallCount_.load(std::memory_order_relaxed) >= targetCallCount;
  std::atomic_signal_fence(std::memory_order_release);
  return res;
}

Result WallProfiler::StopImpl(bool restart, v8::Local<v8::Value>& profile) {
  if (!started_) {
    return Result{"Stop called on not started profiler."};
  }

  uint64_t callCount = 0;
  auto oldProfileId = profileId_;
  if (restart && workaroundV8Bug_) {
    collectionMode_.store(CollectionMode::kNoCollect,
                          std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);
    waitForSignal();
  } else if (withContexts_) {
    collectionMode_.store(CollectionMode::kNoCollect,
                          std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);

    // make sure timestamp changes to avoid having samples from previous profile
    auto now = Now();
    while (Now() == now) {
    }
  }

  auto startThreadCpuTime = startThreadCpuTime_;
  auto startProcessCpuTime = startProcessCpuTime_;

  if (restart) {
    profileId_ = StartInternal();
    // record call count to wait for next signal at the end of function
    callCount = noCollectCallCount_.load(std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_acquire);
  }

  if (withContexts_ || workaroundV8Bug_) {
    SignalHandler::DecreaseUseCount();
  }

  auto v8_profile = cpuProfiler_->StopProfiling(
      Nan::New<String>(oldProfileId).ToLocalChecked());

  ContextBuffer contexts;
  if (withContexts_) {
    contexts.reserve(contexts_.capacity());
    std::swap(contexts, contexts_);
  }

  if (detectV8Bug_) {
    v8ProfilerStuckEventLoopDetected_ = detectV8Bug(v8_profile);
  }

  if (restart && withContexts_ && !workaroundV8Bug_) {
    // make sure timestamp changes to avoid mixing sample taken upon start and a
    // sample from signal handler
    // If v8 bug workaround is enabled, reactivation of sample collection is
    // delayed until function end.
    auto now = Now();
    while (Now() == now) {
    }
    collectionMode_.store(CollectionMode::kCollectContexts,
                          std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);
  }

  if (withContexts_) {
    int64_t nonJSThreadsCpuTime{};

    if (isMainThread_ && collectCpuTime_) {
      // account for non-JS threads CPU only in main thread
      // CPU time of non-JS threads is the difference between process CPU time
      // and sum of all worker JS thread during the profiling period of main
      // worker thread.
      auto totalWorkerCpu = g_profilers.GatherTotalWorkerCpuAndReset();
      auto processCpu = ProcessCpuClock::now() - startProcessCpuTime;
      nonJSThreadsCpuTime =
          std::max(processCpu - totalWorkerCpu, ProcessCpuClock::duration{})
              .count();
    }
    auto contextsByNode =
        GetContextsByNode(v8_profile, contexts, startThreadCpuTime);

    profile = TranslateTimeProfile(v8_profile,
                                   includeLines_,
                                   &contextsByNode,
                                   collectCpuTime_,
                                   nonJSThreadsCpuTime);

  } else {
    profile = TranslateTimeProfile(v8_profile, includeLines_);
  }
  v8_profile->Delete();

  if (!restart) {
    Dispose(v8::Isolate::GetCurrent());
  } else if (workaroundV8Bug_) {
    waitForSignal(callCount + 1);
    collectionMode_.store(withContexts_ ? CollectionMode::kCollectContexts
                                        : CollectionMode::kPassThrough,
                          std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);
  }

  started_ = restart;
  return {};
}

NAN_MODULE_INIT(WallProfiler::Init) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  Local<String> className = Nan::New("TimeProfiler").ToLocalChecked();
  tpl->SetClassName(className);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetAccessor(tpl->InstanceTemplate(),
                   Nan::New("context").ToLocalChecked(),
                   GetContext,
                   SetContext);

  Nan::SetPrototypeMethod(tpl, "start", Start);
  Nan::SetPrototypeMethod(tpl, "stop", Stop);
  Nan::SetPrototypeMethod(tpl, "dispose", Dispose);
  Nan::SetPrototypeMethod(tpl,
                          "v8ProfilerStuckEventLoopDetected",
                          V8ProfilerStuckEventLoopDetected);

  Nan::SetAccessor(tpl->InstanceTemplate(),
                   Nan::New("state").ToLocalChecked(),
                   SharedArrayGetter);

  PerIsolateData::For(Isolate::GetCurrent())
      ->WallProfilerConstructor()
      .Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, className, Nan::GetFunction(tpl).ToLocalChecked());

  auto isolate = v8::Isolate::GetCurrent();
  v8::PropertyAttribute ReadOnlyDontDelete =
      static_cast<v8::PropertyAttribute>(ReadOnly | DontDelete);

  v8::Local<Object> constants = v8::Object::New(isolate);
  Nan::DefineOwnProperty(constants,
                         Nan::New("kSampleCount").ToLocalChecked(),
                         Nan::New<Integer>(kSampleCount),
                         ReadOnlyDontDelete)
      .FromJust();
  Nan::DefineOwnProperty(target,
                         Nan::New("constants").ToLocalChecked(),
                         constants,
                         ReadOnlyDontDelete)
      .FromJust();
}

v8::CpuProfiler* WallProfiler::CreateV8CpuProfiler() {
  if (cpuProfiler_ == nullptr) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();

    bool inserted = g_profilers.AddProfiler(isolate, this);

    if (!inserted) {
      // refuse to create a new profiler if one is already active
      return nullptr;
    }
    cpuProfiler_ = v8::CpuProfiler::New(isolate);
    cpuProfiler_->SetSamplingInterval(
        std::chrono::microseconds(samplingPeriod_).count());
  }
  return cpuProfiler_;
}

v8::Local<v8::Value> WallProfiler::GetContext(Isolate* isolate) {
  auto context = *curContext_.load(std::memory_order_relaxed);
  if (!context) return v8::Undefined(isolate);
  return context->Get(isolate);
}

void WallProfiler::SetContext(Isolate* isolate, Local<Value> value) {
  // Need to be careful here, because we might be interrupted by a
  // signal handler that will make use of curContext_.
  // Update of shared_ptr is not atomic, so instead we use a pointer
  // (curContext_) that points on two shared_ptr (context1_ and context2_),
  // update the shared_ptr that is not currently in use and then atomically
  // update curContext_.
  auto newCurContext = curContext_.load(std::memory_order_relaxed) == &context1_
                           ? &context2_
                           : &context1_;
  if (!value->IsNullOrUndefined()) {
    *newCurContext = std::make_shared<Global<Value>>(isolate, value);
  } else {
    newCurContext->reset();
  }
  std::atomic_signal_fence(std::memory_order_release);
  curContext_.store(newCurContext, std::memory_order_relaxed);
}

NAN_GETTER(WallProfiler::GetContext) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetContext(info.GetIsolate()));
}

NAN_SETTER(WallProfiler::SetContext) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  profiler->SetContext(info.GetIsolate(), value);
}

NAN_GETTER(WallProfiler::SharedArrayGetter) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->jsArray_.Get(v8::Isolate::GetCurrent()));
}

NAN_METHOD(WallProfiler::V8ProfilerStuckEventLoopDetected) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->v8ProfilerStuckEventLoopDetected());
}

NAN_METHOD(WallProfiler::Dispose) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  delete profiler;
}

void WallProfiler::PushContext(int64_t time_from,
                               int64_t time_to,
                               int64_t cpu_time,
                               double async_id) {
  // Be careful this is called in a signal handler context therefore all
  // operations must be async signal safe (in particular no allocations).
  // Our ring buffer avoids allocations.
  auto context = curContext_.load(std::memory_order_relaxed);
  std::atomic_signal_fence(std::memory_order_acquire);
  if (contexts_.size() < contexts_.capacity()) {
    contexts_.push_back({*context, time_from, time_to, cpu_time, async_id});
    std::atomic_fetch_add_explicit(
        reinterpret_cast<std::atomic<uint32_t>*>(&fields_[kSampleCount]),
        1U,
        std::memory_order_relaxed);
  }
}

}  // namespace dd
