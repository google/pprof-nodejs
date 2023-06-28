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
#include <map>
#include <memory>

#include <node_object_wrap.h>  // cppcheck-suppress missingIncludeSystem
#if NODE_MODULE_VERSION >= 102
#include <v8-callbacks.h>
#endif
#include <v8-profiler.h>
#include <v8.h>

#include "code-event-record.hh"

namespace dd {

using CodeEntries = std::map<uintptr_t, std::shared_ptr<CodeEventRecord>>;

class CodeMap : public v8::CodeEventHandler {
 private:
  CodeEntries code_entries_;
  v8::Isolate* isolate_;
  int refs = 0;

  void HandleJitEvent(const v8::JitCodeEvent* event);
  static void StaticHandleJitEvent(const v8::JitCodeEvent* event);

 public:
  explicit CodeMap(v8::Isolate* isolate, CodeEntries entries = {});
  ~CodeMap();

  static std::shared_ptr<CodeMap> For(v8::Isolate* isolate);

  CodeEntries Entries();

  void Enable();
  void Disable();

  void Add(uintptr_t address, std::shared_ptr<CodeEventRecord> record);
  void Remove(uintptr_t address);
  void Clear();

  void Handle(v8::CodeEvent* code_event) override;
  std::shared_ptr<CodeEventRecord> Lookup(uintptr_t address);
};

};  // namespace dd
