#pragma once

#include <string>
#include <map>

#include <node_object_wrap.h> // cppcheck-suppress missingIncludeSystem
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

}; // namespace dd
