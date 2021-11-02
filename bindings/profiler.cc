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

#include <memory>

#include <node.h>
#include "nan.h"
#include "v8-profiler.h"

using namespace v8;

// Sampling Heap Profiler

Local<Value> TranslateAllocationProfile(AllocationProfile::Node* node) {
  Local<Object> js_node = Nan::New<Object>();

  Nan::Set(js_node, Nan::New<String>("name").ToLocalChecked(), node->name);
  Nan::Set(js_node, Nan::New<String>("scriptName").ToLocalChecked(),
           node->script_name);
  Nan::Set(js_node, Nan::New<String>("scriptId").ToLocalChecked(),
           Nan::New<Integer>(node->script_id));
  Nan::Set(js_node, Nan::New<String>("lineNumber").ToLocalChecked(),
           Nan::New<Integer>(node->line_number));
  Nan::Set(js_node, Nan::New<String>("columnNumber").ToLocalChecked(),
           Nan::New<Integer>(node->column_number));

  Local<Array> children = Nan::New<Array>(node->children.size());
  for (size_t i = 0; i < node->children.size(); i++) {
    Nan::Set(children, i, TranslateAllocationProfile(node->children[i]));
  }
  Nan::Set(js_node, Nan::New<String>("children").ToLocalChecked(), children);
  Local<Array> allocations = Nan::New<Array>(node->allocations.size());
  for (size_t i = 0; i < node->allocations.size(); i++) {
    AllocationProfile::Allocation alloc = node->allocations[i];
    Local<Object> js_alloc = Nan::New<Object>();
    Nan::Set(js_alloc, Nan::New<String>("sizeBytes").ToLocalChecked(),
             Nan::New<Number>(alloc.size));
    Nan::Set(js_alloc, Nan::New<String>("count").ToLocalChecked(),
             Nan::New<Number>(alloc.count));
    Nan::Set(allocations, i, js_alloc);
  }
  Nan::Set(js_node, Nan::New<String>("allocations").ToLocalChecked(),
           allocations);
  return js_node;
}

NAN_METHOD(StartSamplingHeapProfiler) {
  if (info.Length() == 2) {
    if (!info[0]->IsUint32()) {
      return Nan::ThrowTypeError("First argument type must be uint32.");
    }
    if (!info[1]->IsNumber()) {
      return Nan::ThrowTypeError("First argument type must be Integer.");
    }

#if NODE_MODULE_VERSION > NODE_8_0_MODULE_VERSION
    uint64_t sample_interval = info[0].As<Integer>()->Value();
    int stack_depth = info[1].As<Integer>()->Value();
#else
    uint64_t sample_interval = info[0].As<Integer>()->Uint32Value();
    int stack_depth = info[1].As<Integer>()->IntegerValue();
#endif

    info.GetIsolate()->GetHeapProfiler()->StartSamplingHeapProfiler(
        sample_interval, stack_depth);
  } else {
    info.GetIsolate()->GetHeapProfiler()->StartSamplingHeapProfiler();
  }
}

// Signature:
// stopSamplingHeapProfiler()
NAN_METHOD(StopSamplingHeapProfiler) {
  info.GetIsolate()->GetHeapProfiler()->StopSamplingHeapProfiler();
}

// Signature:
// getAllocationProfile(): AllocationProfileNode
NAN_METHOD(GetAllocationProfile) {
  std::unique_ptr<v8::AllocationProfile> profile(
      info.GetIsolate()->GetHeapProfiler()->GetAllocationProfile());
  AllocationProfile::Node* root = profile->GetRootNode();
  info.GetReturnValue().Set(TranslateAllocationProfile(root));
}

Local<Object> CreateTimeNode(Local<String> name, Local<String> scriptName,
                             Local<Integer> scriptId, Local<Integer> lineNumber,
                             Local<Integer> columnNumber,
                             Local<Integer> hitCount, Local<Array> children) {
  Local<Object> js_node = Nan::New<Object>();
  Nan::Set(js_node, Nan::New<String>("name").ToLocalChecked(), name);
  Nan::Set(js_node, Nan::New<String>("scriptName").ToLocalChecked(),
           scriptName);
  Nan::Set(js_node, Nan::New<String>("scriptId").ToLocalChecked(), scriptId);
  Nan::Set(js_node, Nan::New<String>("lineNumber").ToLocalChecked(),
           lineNumber);
  Nan::Set(js_node, Nan::New<String>("columnNumber").ToLocalChecked(),
           columnNumber);
  Nan::Set(js_node, Nan::New<String>("hitCount").ToLocalChecked(), hitCount);
  Nan::Set(js_node, Nan::New<String>("children").ToLocalChecked(), children);

  return js_node;
}

Local<Object> TranslateLineNumbersTimeProfileNode(const CpuProfileNode* parent,
                                                  const CpuProfileNode* node);

Local<Array> GetLineNumberTimeProfileChildren(const CpuProfileNode* parent,
                                              const CpuProfileNode* node) {
  unsigned int index = 0;
  Local<Array> children;
  int32_t count = node->GetChildrenCount();

  unsigned int hitLineCount = node->GetHitLineCount();
  unsigned int hitCount = node->GetHitCount();
  if (hitLineCount > 0) {
    std::vector<CpuProfileNode::LineTick> entries(hitLineCount);
    node->GetLineTicks(&entries[0], hitLineCount);
    children = Nan::New<Array>(count + hitLineCount);
    for (const CpuProfileNode::LineTick entry : entries) {
      Nan::Set(children, index++,
               CreateTimeNode(
                   node->GetFunctionName(), node->GetScriptResourceName(),
                   Nan::New<Integer>(node->GetScriptId()),
                   Nan::New<Integer>(entry.line), Nan::New<Integer>(0),
                   Nan::New<Integer>(entry.hit_count), Nan::New<Array>(0)));
    }
  } else if (hitCount > 0) {
    // Handle nodes for pseudo-functions like "process" and "garbage collection"
    // which do not have hit line counts.
    children = Nan::New<Array>(count + 1);
    Nan::Set(
        children, index++,
        CreateTimeNode(node->GetFunctionName(), node->GetScriptResourceName(),
                       Nan::New<Integer>(node->GetScriptId()),
                       Nan::New<Integer>(node->GetLineNumber()),
                       Nan::New<Integer>(node->GetColumnNumber()),
                       Nan::New<Integer>(hitCount), Nan::New<Array>(0)));
  } else {
    children = Nan::New<Array>(count);
  }

  for (int32_t i = 0; i < count; i++) {
    Nan::Set(children, index++,
             TranslateLineNumbersTimeProfileNode(node, node->GetChild(i)));
  };

  return children;
}

Local<Object> TranslateLineNumbersTimeProfileNode(const CpuProfileNode* parent,
                                                  const CpuProfileNode* node) {
  return CreateTimeNode(
      parent->GetFunctionName(), parent->GetScriptResourceName(),
      Nan::New<Integer>(parent->GetScriptId()),
      Nan::New<Integer>(node->GetLineNumber()),
      Nan::New<Integer>(node->GetColumnNumber()), Nan::New<Integer>(0),
      GetLineNumberTimeProfileChildren(parent, node));
}

// In profiles with line level accurate line numbers, a node's line number
// and column number refer to the line/column from which the function was
// called.
Local<Value> TranslateLineNumbersTimeProfileRoot(const CpuProfileNode* node) {
  int32_t count = node->GetChildrenCount();
  std::vector<Local<Array>> childrenArrs(count);
  int32_t childCount = 0;
  for (int32_t i = 0; i < count; i++) {
    Local<Array> c = GetLineNumberTimeProfileChildren(node, node->GetChild(i));
    childCount = childCount + c->Length();
    childrenArrs[i] = c;
  }

  Local<Array> children = Nan::New<Array>(childCount);
  int32_t idx = 0;
  for (int32_t i = 0; i < count; i++) {
    Local<Array> arr = childrenArrs[i];
    for (uint32_t j = 0; j < arr->Length(); j++) {
      Nan::Set(children, idx, Nan::Get(arr, j).ToLocalChecked());
      idx++;
    }
  }

  return CreateTimeNode(node->GetFunctionName(), node->GetScriptResourceName(),
                        Nan::New<Integer>(node->GetScriptId()),
                        Nan::New<Integer>(node->GetLineNumber()),
                        Nan::New<Integer>(node->GetColumnNumber()),
                        Nan::New<Integer>(0), children);
}

Local<Value> TranslateTimeProfileNode(const CpuProfileNode* node) {
  int32_t count = node->GetChildrenCount();
  Local<Array> children = Nan::New<Array>(count);
  for (int32_t i = 0; i < count; i++) {
    Nan::Set(children, i, TranslateTimeProfileNode(node->GetChild(i)));
  }

  return CreateTimeNode(node->GetFunctionName(), node->GetScriptResourceName(),
                        Nan::New<Integer>(node->GetScriptId()),
                        Nan::New<Integer>(node->GetLineNumber()),
                        Nan::New<Integer>(node->GetColumnNumber()),
                        Nan::New<Integer>(node->GetHitCount()), children);
}

Local<Value> TranslateTimeProfile(const CpuProfile* profile,
                                  bool includeLineInfo) {
  Local<Object> js_profile = Nan::New<Object>();
  Nan::Set(js_profile, Nan::New<String>("title").ToLocalChecked(),
           profile->GetTitle());

#if NODE_MODULE_VERSION > NODE_11_0_MODULE_VERSION
  if (includeLineInfo) {
    Nan::Set(js_profile, Nan::New<String>("topDownRoot").ToLocalChecked(),
             TranslateLineNumbersTimeProfileRoot(profile->GetTopDownRoot()));
  } else {
    Nan::Set(js_profile, Nan::New<String>("topDownRoot").ToLocalChecked(),
             TranslateTimeProfileNode(profile->GetTopDownRoot()));
  }
#else
  Nan::Set(js_profile, Nan::New<String>("topDownRoot").ToLocalChecked(),
           TranslateTimeProfileNode(profile->GetTopDownRoot()));
#endif
  Nan::Set(js_profile, Nan::New<String>("startTime").ToLocalChecked(),
           Nan::New<Number>(profile->GetStartTime()));
  Nan::Set(js_profile, Nan::New<String>("endTime").ToLocalChecked(),
           Nan::New<Number>(profile->GetEndTime()));
  return js_profile;
}

class TimeProfiler : public Nan::ObjectWrap {
 public:
  explicit TimeProfiler(int interval)
    : samplingInterval(interval) {}

  void Dispose() {
    if (cpuProfiler != nullptr) {
      cpuProfiler->Dispose();
      cpuProfiler = nullptr;
    }
  }

  static NAN_METHOD(Dispose) {
    TimeProfiler* timeProfiler =
        Nan::ObjectWrap::Unwrap<TimeProfiler>(info.Holder());

    timeProfiler->Dispose();
  }

  static NAN_METHOD(New) {
    if (info.Length() != 1) {
      return Nan::ThrowTypeError("TimeProfiler must have one argument.");
    }
    if (!info[0]->IsNumber()) {
      return Nan::ThrowTypeError("Sample rate must be a number.");
    }

    if (info.IsConstructCall()) {
      int interval =
          Nan::MaybeLocal<Integer>(info[0].As<Integer>()).ToLocalChecked()->Value();

      TimeProfiler* obj = new TimeProfiler(interval);
      obj->Wrap(info.This());
      info.GetReturnValue().Set(info.This());
    } else {
      const int argc = 1;
      v8::Local<v8::Value> argv[argc] = {info[0]};
      v8::Local<v8::Function> cons = Nan::New(constructor());
      info.GetReturnValue().Set(Nan::NewInstance(cons, argc, argv).ToLocalChecked());
    }
  }

  void StartProfiling(Local<String> name, bool includeLines) {
    // Sample counts and timestamps are not used, so we do not need to record
    // samples.
    const bool recordSamples = false;

    if (includeLines) {
      GetProfiler()->StartProfiling(name, CpuProfilingMode::kCallerLineNumbers,
                                    recordSamples);
    } else {
      GetProfiler()->StartProfiling(name, recordSamples);
    }
  }

  static NAN_METHOD(Start) {
    TimeProfiler* timeProfiler =
        Nan::ObjectWrap::Unwrap<TimeProfiler>(info.Holder());

    if (info.Length() != 2) {
      return Nan::ThrowTypeError("Start must have two arguments.");
    }
    if (!info[0]->IsString()) {
      return Nan::ThrowTypeError("Profile name must be a string.");
    }
    if (!info[1]->IsBoolean()) {
      return Nan::ThrowTypeError("Include lines must be a boolean.");
    }

    Local<String> name =
        Nan::MaybeLocal<String>(info[0].As<String>()).ToLocalChecked();

    bool includeLines =
        Nan::MaybeLocal<Boolean>(info[1].As<Boolean>()).ToLocalChecked()->Value();

    timeProfiler->StartProfiling(name, includeLines);
  }

  Local<Value> StopProfiling(Local<String> name, bool includeLines) {
    CpuProfile* profile = GetProfiler()->StopProfiling(name);
    Local<Value> translated_profile =
        TranslateTimeProfile(profile, includeLines);
    profile->Delete();
    return translated_profile;
  }

  static NAN_METHOD(Stop) {
    TimeProfiler* timeProfiler =
        Nan::ObjectWrap::Unwrap<TimeProfiler>(info.Holder());

    if (info.Length() != 2) {
      return Nan::ThrowTypeError("Start must have two arguments.");
    }
    if (!info[0]->IsString()) {
      return Nan::ThrowTypeError("Profile name must be a string.");
    }
    if (!info[1]->IsBoolean()) {
      return Nan::ThrowTypeError("Include lines must be a boolean.");
    }

    Local<String> name =
        Nan::MaybeLocal<String>(info[0].As<String>()).ToLocalChecked();

    bool includeLines =
        Nan::MaybeLocal<Boolean>(info[1].As<Boolean>()).ToLocalChecked()->Value();

    Local<Value> profile = timeProfiler->StopProfiling(name, includeLines);
    info.GetReturnValue().Set(profile);
  }

  static NAN_MODULE_INIT(Init) {
    Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
    Local<String> className = Nan::New("TimeProfiler").ToLocalChecked();
    tpl->SetClassName(className);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    Nan::SetPrototypeMethod(tpl, "start", Start);
    Nan::SetPrototypeMethod(tpl, "dispose", Dispose);
    Nan::SetPrototypeMethod(tpl, "stop", Stop);

    constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, className, Nan::GetFunction(tpl).ToLocalChecked());
  }
 private:
  int samplingInterval = 0;
  CpuProfiler* cpuProfiler = nullptr;

  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }

  ~TimeProfiler() {
    Dispose();
  }

  // A new CPU profiler object will be created each time profiling is started
  // to work around https://bugs.chromium.org/p/v8/issues/detail?id=11051.
  CpuProfiler* GetProfiler() {
    if (cpuProfiler == nullptr) {
      Isolate* isolate = Isolate::GetCurrent();
      cpuProfiler = CpuProfiler::New(isolate);
      cpuProfiler->SetSamplingInterval(samplingInterval);
    }
    return cpuProfiler;
  }
};

extern "C" NODE_MODULE_EXPORT void
NODE_MODULE_INITIALIZER(Local<Object> target,
                        Local<Value> module,
                        Local<Context> context) {
  TimeProfiler::Init(target);

  Local<Object> heapProfiler = Nan::New<Object>();
  Nan::SetMethod(heapProfiler, "startSamplingHeapProfiler",
                 StartSamplingHeapProfiler);
  Nan::SetMethod(heapProfiler, "stopSamplingHeapProfiler",
                 StopSamplingHeapProfiler);
  Nan::SetMethod(heapProfiler, "getAllocationProfile",
                 GetAllocationProfile);
  Nan::Set(target, Nan::New<String>("heapProfiler").ToLocalChecked(),
           heapProfiler);
}
