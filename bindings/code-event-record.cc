#include <node.h>

#include "code-event-record.hh"

namespace dd {

  CodeEventRecord::CodeEventRecord(v8::Isolate *isolate,
                                   uintptr_t address,
                                   uintptr_t previousAddress,
                                   size_t size,
                                   int line,
                                   int column,
                                   std::string comment,
                                   std::string functionName,
                                   std::string scriptName)
      : address(address),
        previousAddress(previousAddress),
        size(size),
        line(line),
        column(column),
        comment(comment),
        functionName(functionName),
        scriptName(scriptName) {}

std::string safe_string(const char* maybe_string) {
  return maybe_string == nullptr ? "" : maybe_string;
}

std::string safe_string(v8::Isolate* isolate, v8::Local<v8::String> maybe_string) {
  auto len = maybe_string->Utf8Length(isolate);
  std::string buffer(len + 1, 0);
  maybe_string->WriteUtf8(isolate, &buffer[0], len + 1);
  return std::string(buffer.c_str());
}

CodeEventRecord::CodeEventRecord(v8::Isolate* isolate, v8::CodeEvent* code_event)
  : CodeEventRecord(
      isolate,
      code_event->GetCodeStartAddress(),
  // CodeEvent::GetPreviousCodeStartAddress didn't exist until Node.js 13.
  #if NODE_MODULE_VERSION > 79
      code_event->GetPreviousCodeStartAddress(),
  #else
      0,
  #endif
      code_event->GetCodeSize(),
      code_event->GetScriptLine(),
      code_event->GetScriptColumn(),
      safe_string(code_event->GetComment()),
      safe_string(isolate, code_event->GetFunctionName()),
      safe_string(isolate, code_event->GetScriptName())
    ) {}

void CodeEventRecord::SetScriptId(int _scriptId) {
  scriptId = _scriptId;
}

v8::Local<v8::Integer> CodeEventRecord::GetScriptId(v8::Isolate* isolate) {
  return v8::Integer::New(isolate, scriptId);
}

v8::Local<v8::Integer> CodeEventRecord::GetAddress(v8::Isolate* isolate) {
  return v8::Integer::NewFromUnsigned(isolate, address);
}

v8::Local<v8::Integer> CodeEventRecord::GetPreviousAddress(v8::Isolate* isolate) {
  return v8::Integer::NewFromUnsigned(isolate, previousAddress);
}

v8::Local<v8::Integer> CodeEventRecord::GetSize(v8::Isolate* isolate) {
  return v8::Integer::NewFromUnsigned(isolate, size);
}

v8::Local<v8::Value> CodeEventRecord::GetFunctionName(v8::Isolate* isolate) {
  if (functionName.empty()) {
    return v8::Undefined(isolate);
  }
  return Nan::New(functionName).ToLocalChecked();
}

v8::Local<v8::Value> CodeEventRecord::GetScriptName(v8::Isolate* isolate) {
  if (scriptName.empty()) {
    return v8::Undefined(isolate);
  }
  return Nan::New(scriptName).ToLocalChecked();
}

v8::Local<v8::Integer> CodeEventRecord::GetLine(v8::Isolate* isolate) {
  return v8::Integer::New(isolate, line);
}

v8::Local<v8::Integer> CodeEventRecord::GetColumn(v8::Isolate* isolate) {
  return v8::Integer::New(isolate, column);
}

v8::Local<v8::Value> CodeEventRecord::GetComment(v8::Isolate* isolate) {
  if (comment.empty()) {
    return v8::Undefined(isolate);
  }
  return Nan::New(comment).ToLocalChecked();
}

bool CodeEventRecord::Equal(const CodeEventRecord* rhs) const {
  return scriptId == rhs->scriptId &&
      address == rhs->address &&
      previousAddress == rhs->previousAddress &&
      size == rhs->size &&
      line == rhs->line &&
      column == rhs->column &&
      comment == rhs->comment &&
      functionName == rhs->functionName &&
      scriptName == rhs->scriptName;
}

}; // namespace dd
