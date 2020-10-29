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

// Time profiler
#if NODE_MODULE_VERSION >= NODE_12_0_MODULE_VERSION
// For Node 12 and Node 14, a new CPU profiler object will be created each
// time profiling is started to work around
// https://bugs.chromium.org/p/v8/issues/detail?id=11051.
CpuProfiler* cpuProfiler;
// Default sampling interval is 1000us.
int samplingIntervalUS = 1000;
#elif NODE_MODULE_VERSION > NODE_8_0_MODULE_VERSION
// This profiler exists for the lifetime of the program. Not calling
// CpuProfiler::Dispose() is intentional.
CpuProfiler* cpuProfiler = CpuProfiler::New(v8::Isolate::GetCurrent());
#else
CpuProfiler* cpuProfiler = v8::Isolate::GetCurrent()->GetCpuProfiler();
#endif

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

#if NODE_MODULE_VERSION > NODE_11_0_MODULE_VERSION
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
#endif

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

// Signature:
// startProfiling(runName: string, includeLineInfo: boolean)
NAN_METHOD(StartProfiling) {
  if (info.Length() != 2) {
    return Nan::ThrowTypeError("StartProfiling must have two arguments.");
  }
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError("First argument must be a string.");
  }
  if (!info[1]->IsBoolean()) {
    return Nan::ThrowTypeError("Second argument must be a boolean.");
  }

#if NODE_MODULE_VERSION >= NODE_12_0_MODULE_VERSION
  // Since the CPU profiler is created and destroyed each time a CPU
  // profile is collected, there cannot be multiple CPU profiling requests
  // inflight in parallel.
  if (cpuProfiler) {
    return Nan::ThrowError("CPU profiler is already started.");
  }
  cpuProfiler = CpuProfiler::New(v8::Isolate::GetCurrent());
  cpuProfiler->SetSamplingInterval(samplingIntervalUS);
#endif

  Local<String> name =
      Nan::MaybeLocal<String>(info[0].As<String>()).ToLocalChecked();

  // Sample counts and timestamps are not used, so we do not need to record
  // samples.
  const bool recordSamples = false;

// Line level accurate line information is not available in Node 11 or earlier.
#if NODE_MODULE_VERSION > NODE_11_0_MODULE_VERSION
  bool includeLineInfo =
      Nan::MaybeLocal<Boolean>(info[1].As<Boolean>()).ToLocalChecked()->Value();
  if (includeLineInfo) {
    cpuProfiler->StartProfiling(name, CpuProfilingMode::kCallerLineNumbers,
                                recordSamples);
  } else {
    cpuProfiler->StartProfiling(name, recordSamples);
  }
#else
  cpuProfiler->StartProfiling(name, recordSamples);
#endif
}

// Signature:
// stopProfiling(runName: string, includeLineInfo: boolean): TimeProfile
NAN_METHOD(StopProfiling) {
#if NODE_MODULE_VERSION >= NODE_12_0_MODULE_VERSION
  if (!cpuProfiler) {
    return Nan::ThrowError("StopProfiling called without an active CPU profiler.");
  }
#endif
  if (info.Length() != 2) {
    return Nan::ThrowTypeError("StopProfling must have two arguments.");
  }
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError("First argument must be a string.");
  }
  if (!info[1]->IsBoolean()) {
    return Nan::ThrowTypeError("Second argument must be a boolean.");
  }
  Local<String> name =
      Nan::MaybeLocal<String>(info[0].As<String>()).ToLocalChecked();
  bool includeLineInfo =
      Nan::MaybeLocal<Boolean>(info[1].As<Boolean>()).ToLocalChecked()->Value();

  CpuProfile* profile = cpuProfiler->StopProfiling(name);
  Local<Value> translated_profile =
      TranslateTimeProfile(profile, includeLineInfo);
  profile->Delete();
#if NODE_MODULE_VERSION >= NODE_12_0_MODULE_VERSION
  // Dispose of CPU profiler to work around memory leak.
  cpuProfiler->Dispose();
  cpuProfiler = NULL;
#endif
  info.GetReturnValue().Set(translated_profile);
}

// Signature:
// setSamplingInterval(intervalMicros: number)
NAN_METHOD(SetSamplingInterval) {
#if NODE_MODULE_VERSION > NODE_8_0_MODULE_VERSION
  int us = info[0].As<Integer>()->Value();
#else
  int us = info[0].As<Integer>()->IntegerValue();
#endif
#if NODE_MODULE_VERSION >= NODE_12_0_MODULE_VERSION
  samplingIntervalUS = us;
#else
  cpuProfiler->SetSamplingInterval(us);
#endif
}

NAN_MODULE_INIT(InitAll) {
  Local<Object> timeProfiler = Nan::New<Object>();
  Nan::Set(timeProfiler, Nan::New("startProfiling").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(StartProfiling))
               .ToLocalChecked());
  Nan::Set(timeProfiler, Nan::New("stopProfiling").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(StopProfiling))
               .ToLocalChecked());
  Nan::Set(timeProfiler, Nan::New("setSamplingInterval").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(SetSamplingInterval))
               .ToLocalChecked());
  Nan::Set(target, Nan::New<String>("timeProfiler").ToLocalChecked(),
           timeProfiler);

  Local<Object> heapProfiler = Nan::New<Object>();
  Nan::Set(
      heapProfiler, Nan::New("startSamplingHeapProfiler").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(StartSamplingHeapProfiler))
          .ToLocalChecked());
  Nan::Set(
      heapProfiler, Nan::New("stopSamplingHeapProfiler").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(StopSamplingHeapProfiler))
          .ToLocalChecked());
  Nan::Set(heapProfiler, Nan::New("getAllocationProfile").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(GetAllocationProfile))
               .ToLocalChecked());
  Nan::Set(target, Nan::New<String>("heapProfiler").ToLocalChecked(),
           heapProfiler);
}

NODE_MODULE(google_cloud_profiler, InitAll);
