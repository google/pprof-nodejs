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

#include "translate-time-profile.hh"

#include <vector>

namespace dd {

namespace {
class ProfileTranslator {
 private:
  LabelSetsByNode* labelSetsByNode;
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Array> emptyArray = NewArray(0);
  v8::Local<v8::Integer> zero = NewInteger(0);

#define FIELDS                                                                 \
  X(name)                                                                      \
  X(scriptName)                                                                \
  X(scriptId)                                                                  \
  X(lineNumber)                                                                \
  X(columnNumber)                                                              \
  X(hitCount)                                                                  \
  X(children)                                                                  \
  X(labelSets)

#define X(name) v8::Local<v8::String> str_##name = NewString(#name);
  FIELDS
#undef X

  v8::Local<v8::Array> getLabelSetsForNode(const v8::CpuProfileNode* node,
                                           uint32_t& hitcount) {
    hitcount = node->GetHitCount();
    if (!labelSetsByNode) {
      // custom labels are not enabled, keep the node hitcount and return empty
      // array
      return emptyArray;
    }

    auto it = labelSetsByNode->find(node);
    auto labelSets = emptyArray;
    if (it != labelSetsByNode->end()) {
      hitcount = it->second.hitcount;
      labelSets = it->second.labelSets;
    } else {
      // no context found for node, discard it since every sample taken from
      // signal handler should have a matching context if it does not, it means
      // sample was captured by a deopt event
      hitcount = 0;
    }
    return labelSets;
  }

  v8::Local<v8::Object> CreateTimeNode(v8::Local<v8::String> name,
                                       v8::Local<v8::String> scriptName,
                                       v8::Local<v8::Integer> scriptId,
                                       v8::Local<v8::Integer> lineNumber,
                                       v8::Local<v8::Integer> columnNumber,
                                       v8::Local<v8::Integer> hitCount,
                                       v8::Local<v8::Array> children,
                                       v8::Local<v8::Array> labelSets) {
    v8::Local<v8::Object> js_node = Nan::New<v8::Object>();
#define X(name) Nan::Set(js_node, str_##name, name);
    FIELDS
#undef X
#undef FIELDS
    return js_node;
  }

  v8::Local<v8::Integer> NewInteger(int32_t x) {
    return v8::Integer::New(isolate, x);
  }

  v8::Local<v8::Array> NewArray(int length) {
    return v8::Array::New(isolate, length);
  }

  v8::Local<v8::String> NewString(const char* str) {
    return Nan::New<v8::String>(str).ToLocalChecked();
  }

  v8::Local<v8::Array> GetLineNumberTimeProfileChildren(
      const v8::CpuProfileNode* node) {
    unsigned int index = 0;
    v8::Local<v8::Array> children;
    int32_t count = node->GetChildrenCount();

    unsigned int hitLineCount = node->GetHitLineCount();
    unsigned int hitCount = node->GetHitCount();
    auto scriptId = NewInteger(node->GetScriptId());
    if (hitLineCount > 0) {
      std::vector<v8::CpuProfileNode::LineTick> entries(hitLineCount);
      node->GetLineTicks(&entries[0], hitLineCount);
      children = NewArray(count + hitLineCount);
      for (const v8::CpuProfileNode::LineTick entry : entries) {
        Nan::Set(children,
                 index++,
                 CreateTimeNode(node->GetFunctionName(),
                                node->GetScriptResourceName(),
                                scriptId,
                                NewInteger(entry.line),
                                zero,
                                NewInteger(entry.hit_count),
                                emptyArray,
                                emptyArray));
      }
    } else if (hitCount > 0) {
      // Handle nodes for pseudo-functions like "process" and "garbage
      // collection" which do not have hit line counts.
      children = NewArray(count + 1);
      Nan::Set(children,
               index++,
               CreateTimeNode(node->GetFunctionName(),
                              node->GetScriptResourceName(),
                              scriptId,
                              NewInteger(node->GetLineNumber()),
                              NewInteger(node->GetColumnNumber()),
                              NewInteger(hitCount),
                              emptyArray,
                              emptyArray));
    } else {
      children = NewArray(count);
    }

    for (int32_t i = 0; i < count; i++) {
      Nan::Set(children,
               index++,
               TranslateLineNumbersTimeProfileNode(node, node->GetChild(i)));
    };

    return children;
  }

  v8::Local<v8::Object> TranslateLineNumbersTimeProfileNode(
      const v8::CpuProfileNode* parent, const v8::CpuProfileNode* node) {
    return CreateTimeNode(parent->GetFunctionName(),
                          parent->GetScriptResourceName(),
                          NewInteger(parent->GetScriptId()),
                          NewInteger(node->GetLineNumber()),
                          NewInteger(node->GetColumnNumber()),
                          zero,
                          GetLineNumberTimeProfileChildren(node),
                          emptyArray);
  }

  // In profiles with line level accurate line numbers, a node's line number
  // and column number refer to the line/column from which the function was
  // called.
  v8::Local<v8::Value> TranslateLineNumbersTimeProfileRoot(
      const v8::CpuProfileNode* node) {
    int32_t count = node->GetChildrenCount();
    std::vector<v8::Local<v8::Array>> childrenArrs(count);
    int32_t childCount = 0;
    for (int32_t i = 0; i < count; i++) {
      v8::Local<v8::Array> c =
          GetLineNumberTimeProfileChildren(node->GetChild(i));
      childCount = childCount + c->Length();
      childrenArrs[i] = c;
    }

    v8::Local<v8::Array> children = NewArray(childCount);
    int32_t idx = 0;
    for (int32_t i = 0; i < count; i++) {
      v8::Local<v8::Array> arr = childrenArrs[i];
      for (uint32_t j = 0; j < arr->Length(); j++) {
        Nan::Set(children, idx, Nan::Get(arr, j).ToLocalChecked());
        idx++;
      }
    }

    return CreateTimeNode(node->GetFunctionName(),
                          node->GetScriptResourceName(),
                          NewInteger(node->GetScriptId()),
                          NewInteger(node->GetLineNumber()),
                          NewInteger(node->GetColumnNumber()),
                          zero,
                          children,
                          emptyArray);
  }

  v8::Local<v8::Value> TranslateTimeProfileNode(
      const v8::CpuProfileNode* node) {
    int32_t count = node->GetChildrenCount();
    v8::Local<v8::Array> children = Nan::New<v8::Array>(count);
    for (int32_t i = 0; i < count; i++) {
      Nan::Set(children, i, TranslateTimeProfileNode(node->GetChild(i)));
    }

    uint32_t hitcount = 0;
    auto labels = getLabelSetsForNode(node, hitcount);

    return CreateTimeNode(node->GetFunctionName(),
                          node->GetScriptResourceName(),
                          NewInteger(node->GetScriptId()),
                          NewInteger(node->GetLineNumber()),
                          NewInteger(node->GetColumnNumber()),
                          NewInteger(hitcount),
                          children,
                          labels);
  }

 public:
  explicit ProfileTranslator(LabelSetsByNode* nls = nullptr)
      : labelSetsByNode(nls) {}

  v8::Local<v8::Value> TranslateTimeProfile(const v8::CpuProfile* profile,
                                            bool includeLineInfo) {
    v8::Local<v8::Object> js_profile = Nan::New<v8::Object>();

    if (includeLineInfo) {
      Nan::Set(js_profile,
               NewString("topDownRoot"),
               TranslateLineNumbersTimeProfileRoot(profile->GetTopDownRoot()));
    } else {
      Nan::Set(js_profile,
               NewString("topDownRoot"),
               TranslateTimeProfileNode(profile->GetTopDownRoot()));
    }
    Nan::Set(js_profile,
             NewString("startTime"),
             Nan::New<v8::Number>(profile->GetStartTime()));
    Nan::Set(js_profile,
             NewString("endTime"),
             Nan::New<v8::Number>(profile->GetEndTime()));

    return js_profile;
  }
};
}  // namespace

v8::Local<v8::Value> TranslateTimeProfile(const v8::CpuProfile* profile,
                                          bool includeLineInfo,
                                          LabelSetsByNode* labelSetsByNode) {
  return ProfileTranslator(labelSetsByNode)
      .TranslateTimeProfile(profile, includeLineInfo);
}

}  // namespace dd