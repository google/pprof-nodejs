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

#include <atomic>
#include <cinttypes>
#include <cstdint>
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
#define DD_WALL_USE_SIGPROF

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
static int64_t Now() {
  return 0;
};
#endif

using namespace v8;

namespace dd {

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
    return UpdateProfilers([isolate, profiler](auto map) {
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

 private:
  template <typename F>
  bool UpdateProfilers(F updateFn) {
    // use mutex to prevent two isolates of updating profilers concurrently
    std::lock_guard<std::mutex> lock(update_mutex_);

    if (!init_) {
      profilers_.store(new ProfilerMap(), std::memory_order_release);
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
  ;
};

using ProfilerMap = std::unordered_map<Isolate*, WallProfiler*>;

static ProtectedProfilerMap g_profilers;
static std::mutex g_profilers_update_mtx;

namespace {

#ifdef DD_WALL_USE_SIGPROF
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

  // Check if sampling is allowed
  if (!prof->collectSampleAllowed()) {
    return;
  }

  auto time_from = Now();
  old_handler(sig, info, context);
  auto time_to = Now();
  prof->PushContext(time_from, time_to);
}
#else
class SignalHandler {
 public:
  static void IncreaseUseCount() {}
  static void DecreaseUseCount() {}
};
#endif
}  // namespace

LabelSetsByNode WallProfiler::GetLabelSetsByNode(CpuProfile* profile,
                                                 ContextBuffer& contexts) {
  LabelSetsByNode labelSetsByNode;

  auto sampleCount = profile->GetSamplesCount();
  if (contexts.empty() || sampleCount == 0) {
    return labelSetsByNode;
  }

  auto isolate = Isolate::GetCurrent();
  // auto labelKey = Nan::New<String>("label").ToLocalChecked();

  auto contextIt = contexts.begin();

  // deltaIdx is the offset of the sample to process compared to current
  // iteration index
  int deltaIdx = 0;

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
        auto it = labelSetsByNode.find(sample);
        Local<Array> array;
        if (it == labelSetsByNode.end()) {
          array = Nan::New<Array>();
          assert(labelSetsByNode.find(sample) == labelSetsByNode.end());
          labelSetsByNode[sample] = {array, 1};
        } else {
          array = it->second.labelSets;
          ++it->second.hitcount;
        }
        if (sampleContext.labels) {
          Nan::Set(
              array, array->Length(), sampleContext.labels.get()->Get(isolate));
        }

        // Sample context was consumed, fetch the next one
        ++contextIt;
        break;  // don't match more than one context to one sample
      }
    }
  }

  return labelSetsByNode;
}

WallProfiler::WallProfiler(int samplingPeriodMicros,
                           int durationMicros,
                           bool includeLines,
                           bool withLabels)
    : samplingPeriodMicros_(samplingPeriodMicros),
      includeLines_(includeLines),
      withLabels_(withLabels) {
  contexts_.reserve(durationMicros * 2 / samplingPeriodMicros);
  curLabels_.store(&labels1_, std::memory_order_relaxed);
  collectSamples_.store(false, std::memory_order_relaxed);
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
  if (info.Length() != 4) {
    return Nan::ThrowTypeError("WallProfiler must have four arguments.");
  }

  if (!info[0]->IsNumber()) {
    return Nan::ThrowTypeError("Sample period must be a number.");
  }
  if (!info[1]->IsNumber()) {
    return Nan::ThrowTypeError("Duration must be a number.");
  }
  if (!info[2]->IsBoolean()) {
    return Nan::ThrowTypeError("includeLines must be a boolean.");
  }
  if (!info[3]->IsBoolean()) {
    return Nan::ThrowTypeError("withLabels must be a boolean.");
  }

  if (info.IsConstructCall()) {
    int interval = info[0].As<v8::Integer>()->Value();
    int duration = info[1].As<v8::Integer>()->Value();

    if (interval <= 0) {
      return Nan::ThrowTypeError("Sample rate must be positive.");
    }
    if (duration <= 0) {
      return Nan::ThrowTypeError("Duration must be positive.");
    }
    if (duration < interval) {
      return Nan::ThrowTypeError("Duration must not be less than sample rate.");
    }

    bool includeLines = info[2].As<v8::Boolean>()->Value();
    bool withLabels = info[3].As<v8::Boolean>()->Value();

#ifndef DD_WALL_USE_SIGPROF
    if (withLabels) {
      return Nan::ThrowTypeError("Labels are not supported.");
    }
#endif

    if (includeLines && withLabels) {
      // Currently custom labels are not compatible with caller line
      // information, because it's not possible to associate labels with line
      // ticks:
      // labels are associated to sample which itself is associated with
      // a CpuProfileNode, but this node has several line ticks, and we cannot
      // determine labels <-> line ticks association. Note that line number is
      // present in v8 internal sample struct and would allow mapping sample to
      // line tick, and thus labels to line tick, but this information is not
      // available in v8 public API.
      // More over in caller line number mode, line number of a CpuProfileNode
      // is not the line of the current function, but the line number where this
      // function is called, therefore we don't access either to the line of the
      // function (otherwise we could ignoree line ticks and replace them with
      // single hitcount for the function).
      return Nan::ThrowTypeError(
          "Include line option is not compatible with labels.");
    }

    WallProfiler* obj =
        new WallProfiler(interval, duration, includeLines, withLabels);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    const int argc = 4;
    v8::Local<v8::Value> argv[argc] = {info[0], info[1], info[2], info[3]};
    v8::Local<v8::Function> cons = Nan::New(
        PerIsolateData::For(info.GetIsolate())->WallProfilerConstructor());
    info.GetReturnValue().Set(
        Nan::NewInstance(cons, argc, argv).ToLocalChecked());
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

  collectSamples_.store(true, std::memory_order_relaxed);
  started_ = true;
  return {};
}

std::string WallProfiler::StartInternal() {
  char buf[128];
  snprintf(buf, sizeof(buf), "pprof-%" PRId64, profileIdx_++);
  v8::Local<v8::String> title = Nan::New<String>(buf).ToLocalChecked();
  cpuProfiler_->StartProfiling(title,
                               includeLines_
                                   ? CpuProfilingMode::kCallerLineNumbers
                                   : CpuProfilingMode::kLeafNodeLineNumbers,
                               withLabels_);

  // reinstall sighandler on each new upload period
  if (withLabels_) {
    SignalHandler::IncreaseUseCount();
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
#if NODE_MODULE_VERSION < NODE_16_0_MODULE_VERSION
  auto err = wallProfiler->StopImplOld(restart, profile);
#else
  auto err = wallProfiler->StopImpl(restart, profile);
#endif

  if (!err.success) {
    return Nan::ThrowTypeError(err.msg.c_str());
  }
  info.GetReturnValue().Set(profile);
}

Result WallProfiler::StopImpl(bool restart, v8::Local<v8::Value>& profile) {
  if (!started_) {
    return Result{"Stop called on not started profiler."};
  }

  auto oldProfileId = profileId_;
  if (withLabels_) {
    collectSamples_.store(false, std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);

    // make sure timestamp changes to avoid having samples from previous profile
    auto now = Now();
    while (Now() == now) {
    }
  }

  if (restart) {
    profileId_ = StartInternal();
  }

  if (withLabels_) {
    SignalHandler::DecreaseUseCount();
  }
  auto v8_profile = cpuProfiler_->StopProfiling(
      Nan::New<String>(oldProfileId).ToLocalChecked());

  ContextBuffer contexts;
  if (withLabels_) {
    contexts.reserve(contexts_.capacity());
    std::swap(contexts, contexts_);
  }

  if (restart && withLabels_) {
    // make sure timestamp changes to avoid mixing sample taken upon start and a
    // sample from signal handler
    auto now = Now();
    while (Now() == now) {
    }
    collectSamples_.store(true, std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);
  }

  if (withLabels_) {
    auto labelSetsByNode = GetLabelSetsByNode(v8_profile, contexts);
    profile = TranslateTimeProfile(v8_profile, includeLines_, &labelSetsByNode);

  } else {
    profile = TranslateTimeProfile(v8_profile, includeLines_);
  }
  v8_profile->Delete();

  if (!restart) {
    Dispose(v8::Isolate::GetCurrent());
  }
  started_ = restart;

  return {};
}

Result WallProfiler::StopImplOld(bool restart, v8::Local<v8::Value>& profile) {
  if (!started_) {
    return Result{"Stop called on not started profiler."};
  }

  if (withLabels_) {
    SignalHandler::DecreaseUseCount();
  }
  auto v8_profile = cpuProfiler_->StopProfiling(
      Nan::New<String>(profileId_).ToLocalChecked());

  if (withLabels_) {
    auto labelSetsByNode = GetLabelSetsByNode(v8_profile, contexts_);
    profile = TranslateTimeProfile(v8_profile, includeLines_, &labelSetsByNode);

  } else {
    profile = TranslateTimeProfile(v8_profile, includeLines_);
  }
  contexts_.clear();
  v8_profile->Delete();
  Dispose(v8::Isolate::GetCurrent());

  if (restart) {
    CreateV8CpuProfiler();
    profileId_ = StartInternal();
  } else {
    started_ = false;
  }

  return {};
}

NAN_MODULE_INIT(WallProfiler::Init) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  Local<String> className = Nan::New("TimeProfiler").ToLocalChecked();
  tpl->SetClassName(className);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetAccessor(tpl->InstanceTemplate(),
                   Nan::New("labels").ToLocalChecked(),
                   GetLabels,
                   SetLabels);

  Nan::SetPrototypeMethod(tpl, "start", Start);
  Nan::SetPrototypeMethod(tpl, "stop", Stop);

  PerIsolateData::For(Isolate::GetCurrent())
      ->WallProfilerConstructor()
      .Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, className, Nan::GetFunction(tpl).ToLocalChecked());
}

// A new CPU profiler object will be created each time profiling is started
// to work around https://bugs.chromium.org/p/v8/issues/detail?id=11051.
// TODO: Fixed in v16. Delete this hack when deprecating v14.
v8::CpuProfiler* WallProfiler::CreateV8CpuProfiler() {
  if (cpuProfiler_ == nullptr) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();

    bool inserted = g_profilers.AddProfiler(isolate, this);

    if (!inserted) {
      // refuse to create a new profiler if one is already active
      return nullptr;
    }
    cpuProfiler_ = v8::CpuProfiler::New(isolate);
    cpuProfiler_->SetSamplingInterval(samplingPeriodMicros_);
  }
  return cpuProfiler_;
}

v8::Local<v8::Value> WallProfiler::GetLabels(Isolate* isolate) {
  auto labels = *curLabels_.load(std::memory_order_relaxed);
  if (!labels) return v8::Undefined(isolate);
  return labels->Get(isolate);
}

void WallProfiler::SetLabels(Isolate* isolate, Local<Value> value) {
  // Need to be careful here, because we might be interrupted by a
  // signal handler that will make use of curLabels_.
  // Update of shared_ptr is not atomic, so instead we use a pointer
  // (curLabels_) that points on two shared_ptr (labels1_ and labels2_), update
  // the shared_ptr that is not currently in use and then atomically update
  // curLabels_.
  auto newCurLabels = curLabels_.load(std::memory_order_relaxed) == &labels1_
                          ? &labels2_
                          : &labels1_;
  if (value->BooleanValue(isolate)) {
    *newCurLabels = std::make_shared<Global<Value>>(isolate, value);
  } else {
    newCurLabels->reset();
  }
  std::atomic_signal_fence(std::memory_order_release);
  curLabels_.store(newCurLabels, std::memory_order_relaxed);
}

NAN_GETTER(WallProfiler::GetLabels) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetLabels(info.GetIsolate()));
}

NAN_SETTER(WallProfiler::SetLabels) {
  auto profiler = Nan::ObjectWrap::Unwrap<WallProfiler>(info.Holder());
  profiler->SetLabels(info.GetIsolate(), value);
}

void WallProfiler::PushContext(int64_t time_from, int64_t time_to) {
  // Be careful this is called in a signal handler context therefore all
  // operations must be async signal safe (in particular no allocations).
  // Our ring buffer avoids allocations.
  auto labels = curLabels_.load(std::memory_order_relaxed);
  std::atomic_signal_fence(std::memory_order_acquire);
  if (contexts_.size() < contexts_.capacity()) {
    contexts_.push_back({*labels, time_from, time_to});
  }
}

}  // namespace dd
