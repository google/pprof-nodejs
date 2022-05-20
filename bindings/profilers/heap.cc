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

#include "heap.hh"

#include <memory>

#include <node.h>
#include <v8-profiler.h>

using namespace v8;

namespace dd {

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

NAN_METHOD(HeapProfiler::StartSamplingHeapProfiler) {
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
NAN_METHOD(HeapProfiler::StopSamplingHeapProfiler) {
  info.GetIsolate()->GetHeapProfiler()->StopSamplingHeapProfiler();
}

// Signature:
// getAllocationProfile(): AllocationProfileNode
NAN_METHOD(HeapProfiler::GetAllocationProfile) {
  std::unique_ptr<v8::AllocationProfile> profile(
      info.GetIsolate()->GetHeapProfiler()->GetAllocationProfile());
  AllocationProfile::Node* root = profile->GetRootNode();
  info.GetReturnValue().Set(TranslateAllocationProfile(root));
}

NAN_MODULE_INIT(HeapProfiler::Init) {
  v8::Local<v8::Object> heapProfiler = Nan::New<v8::Object>();
  Nan::SetMethod(heapProfiler, "startSamplingHeapProfiler",
                 StartSamplingHeapProfiler);
  Nan::SetMethod(heapProfiler, "stopSamplingHeapProfiler",
                 StopSamplingHeapProfiler);
  Nan::SetMethod(heapProfiler, "getAllocationProfile",
                 GetAllocationProfile);
  Nan::Set(target, Nan::New<v8::String>("heapProfiler").ToLocalChecked(),
           heapProfiler);
}

} // namespace dd
