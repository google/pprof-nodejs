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
#include "general-regs-only.hh"
#include "profile-translator.hh"

namespace dd {

namespace {
class TimeProfileTranslator : ProfileTranslator {
 private:
  std::shared_ptr<ContextsByNode> contextsByNode;
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
  X(contexts)

#define X(name) v8::Local<v8::String> str_##name = NewString(#name);
  FIELDS
#undef X

  v8::Local<v8::Array> getContextsForNode(const v8::CpuProfileNode* node,
                                          uint32_t& hitcount) {
    hitcount = node->GetHitCount();
    if (!contextsByNode) {
      // custom contexts are not enabled, keep the node hitcount and return
      // empty array
      return emptyArray;
    }

    auto it = contextsByNode->find(node);
    auto contexts = emptyArray;
    if (it != contextsByNode->end()) {
      hitcount = it->second.hitcount;
      contexts = it->second.contexts;
    } else {
      // no context found for node, discard it since every sample taken from
      // signal handler should have a matching context if it does not, it means
      // sample was captured by a deopt event
      hitcount = 0;
    }
    return contexts;
  }

  v8::Local<v8::Object> CreateTimeNode(v8::Local<v8::String> name,
                                       v8::Local<v8::String> scriptName,
                                       v8::Local<v8::Integer> scriptId,
                                       v8::Local<v8::Integer> lineNumber,
                                       v8::Local<v8::Integer> columnNumber,
                                       v8::Local<v8::Integer> hitCount,
                                       v8::Local<v8::Array> children,
                                       v8::Local<v8::Array> contexts) {
    v8::Local<v8::Object> js_node = NewObject();
#define X(name) Set(js_node, str_##name, name);
    FIELDS
#undef X
#undef FIELDS
    return js_node;
  }

  v8::Local<v8::Array> GetLineNumberTimeProfileChildren(
      const v8::CpuProfileNode* node) GENERAL_REGS_ONLY {
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
        Set(children,
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
      Set(children,
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
      Set(children,
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
        Set(children, idx, Get(arr, j).ToLocalChecked());
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
    v8::Local<v8::Array> children = NewArray(count);
    for (int32_t i = 0; i < count; i++) {
      Set(children, i, TranslateTimeProfileNode(node->GetChild(i)));
    }

    uint32_t hitcount = 0;
    auto contexts = getContextsForNode(node, hitcount);

    return CreateTimeNode(node->GetFunctionName(),
                          node->GetScriptResourceName(),
                          NewInteger(node->GetScriptId()),
                          NewInteger(node->GetLineNumber()),
                          NewInteger(node->GetColumnNumber()),
                          NewInteger(hitcount),
                          children,
                          contexts);
  }

 public:
  explicit TimeProfileTranslator(std::shared_ptr<ContextsByNode> nls = nullptr)
      : contextsByNode(nls) {}

  v8::Local<v8::Value> TranslateTimeProfile(const v8::CpuProfile* profile,
                                            bool includeLineInfo,
                                            bool hasCpuTime,
                                            int64_t nonJSThreadsCpuTime) {
    v8::Local<v8::Object> js_profile = NewObject();

    if (includeLineInfo) {
      Set(js_profile,
          NewString("topDownRoot"),
          TranslateLineNumbersTimeProfileRoot(profile->GetTopDownRoot()));
    } else {
      Set(js_profile,
          NewString("topDownRoot"),
          TranslateTimeProfileNode(profile->GetTopDownRoot()));
    }
    Set(js_profile, NewString("startTime"), NewNumber(profile->GetStartTime()));
    Set(js_profile, NewString("endTime"), NewNumber(profile->GetEndTime()));
    Set(js_profile, NewString("hasCpuTime"), NewBoolean(hasCpuTime));

    Set(js_profile,
        NewString("nonJSThreadsCpuTime"),
        NewNumber(nonJSThreadsCpuTime));
    return js_profile;
  }
};
}  // namespace

v8::Local<v8::Value> TranslateTimeProfile(
    const v8::CpuProfile* profile,
    bool includeLineInfo,
    std::shared_ptr<ContextsByNode> contextsByNode,
    bool hasCpuTime,
    int64_t nonJSThreadsCpuTime) {
  return TimeProfileTranslator(contextsByNode)
      .TranslateTimeProfile(
          profile, includeLineInfo, hasCpuTime, nonJSThreadsCpuTime);
}

}  // namespace dd
