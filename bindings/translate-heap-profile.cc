/*
 * Copyright 2024 Datadog, Inc
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

#include "translate-heap-profile.hh"
#include <nan.h>
#include "profile-translator.hh"

namespace dd {

namespace {
class HeapProfileTranslator : ProfileTranslator {
#define NODE_FIELDS                                                            \
  X(name)                                                                      \
  X(scriptName)                                                                \
  X(scriptId)                                                                  \
  X(lineNumber)                                                                \
  X(columnNumber)                                                              \
  X(children)                                                                  \
  X(allocations)

#define ALLOCATION_FIELDS                                                      \
  X(sizeBytes)                                                                 \
  X(count)

#define X(name) v8::Local<v8::String> str_##name = NewString(#name);
  NODE_FIELDS
  ALLOCATION_FIELDS
#undef X

 public:
  v8::Local<v8::Value> TranslateAllocationProfile(
      v8::AllocationProfile::Node* node) {
    v8::Local<v8::Array> children = NewArray(node->children.size());
    for (size_t i = 0; i < node->children.size(); i++) {
      Set(children, i, TranslateAllocationProfile(node->children[i]));
    }

    v8::Local<v8::Array> allocations = NewArray(node->allocations.size());
    for (size_t i = 0; i < node->allocations.size(); i++) {
      auto alloc = node->allocations[i];
      Set(allocations,
          i,
          CreateAllocation(NewNumber(alloc.count), NewNumber(alloc.size)));
    }

    return CreateNode(node->name,
                      node->script_name,
                      NewInteger(node->script_id),
                      NewInteger(node->line_number),
                      NewInteger(node->column_number),
                      children,
                      allocations);
  }

  v8::Local<v8::Value> TranslateAllocationProfile(Node* node) {
    v8::Local<v8::Array> children = NewArray(node->children.size());
    for (size_t i = 0; i < node->children.size(); i++) {
      Set(children, i, TranslateAllocationProfile(node->children[i].get()));
    }

    v8::Local<v8::Array> allocations = NewArray(node->allocations.size());
    for (size_t i = 0; i < node->allocations.size(); i++) {
      auto alloc = node->allocations[i];
      Set(allocations,
          i,
          CreateAllocation(NewNumber(alloc.count), NewNumber(alloc.size)));
    }

    return CreateNode(NewString(node->name.c_str()),
                      NewString(node->script_name.c_str()),
                      NewInteger(node->script_id),
                      NewInteger(node->line_number),
                      NewInteger(node->column_number),
                      children,
                      allocations);
  }

 private:
  v8::Local<v8::Object> CreateNode(v8::Local<v8::String> name,
                                   v8::Local<v8::String> scriptName,
                                   v8::Local<v8::Integer> scriptId,
                                   v8::Local<v8::Integer> lineNumber,
                                   v8::Local<v8::Integer> columnNumber,
                                   v8::Local<v8::Array> children,
                                   v8::Local<v8::Array> allocations) {
    v8::Local<v8::Object> js_node = NewObject();
#define X(name) Set(js_node, str_##name, name);
    NODE_FIELDS
#undef X
#undef NODE_FIELDS
    return js_node;
  }

  v8::Local<v8::Object> CreateAllocation(v8::Local<v8::Number> count,
                                         v8::Local<v8::Number> sizeBytes) {
    v8::Local<v8::Object> js_alloc = NewObject();
#define X(name) Set(js_alloc, str_##name, name);
    ALLOCATION_FIELDS
#undef X
#undef ALLOCATION_FIELDS
    return js_alloc;
  }

 public:
  explicit HeapProfileTranslator() {}
};
}  // namespace

std::shared_ptr<Node> TranslateAllocationProfileToCpp(
    v8::AllocationProfile::Node* node) {
  auto new_node = std::make_shared<Node>();
  new_node->line_number = node->line_number;
  new_node->column_number = node->column_number;
  new_node->script_id = node->script_id;
  Nan::Utf8String name(node->name);
  new_node->name.assign(*name, name.length());
  Nan::Utf8String script_name(node->script_name);
  new_node->script_name.assign(*script_name, script_name.length());

  new_node->children.reserve(node->children.size());
  for (auto& child : node->children) {
    new_node->children.push_back(TranslateAllocationProfileToCpp(child));
  }

  new_node->allocations.reserve(node->allocations.size());
  for (auto& allocation : node->allocations) {
    new_node->allocations.push_back(allocation);
  }
  return new_node;
}

v8::Local<v8::Value> TranslateAllocationProfile(
    v8::AllocationProfile::Node* node) {
  return HeapProfileTranslator().TranslateAllocationProfile(node);
}

v8::Local<v8::Value> TranslateAllocationProfile(Node* node) {
  return HeapProfileTranslator().TranslateAllocationProfile(node);
}

}  // namespace dd
