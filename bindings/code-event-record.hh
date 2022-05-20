#pragma once

#include <string>
#include <map>

#include <nan.h>
#include <node_object_wrap.h> // cppcheck-suppress missingIncludeSystem
#include <v8-profiler.h>
#include <v8.h>

namespace dd {

class CodeMap;

class CodeEventRecord : public Nan::ObjectWrap {
 private:
  int scriptId = 0;
  uintptr_t address;
  uintptr_t previousAddress;
  size_t size;
  int line;
  int column;
  std::string comment;
  std::string functionName;
  std::string scriptName;

 public:
  explicit CodeEventRecord(v8::Isolate* isolate,
                           uintptr_t address,
                           uintptr_t previousAddress,
                           size_t size,
                           int line,
                           int column,
                           std::string comment,
                           v8::Local<v8::String> functionName,
                           v8::Local<v8::String> scriptName);

  explicit CodeEventRecord(v8::Isolate* isolate,
                           uintptr_t address,
                           uintptr_t previousAddress = 0,
                           size_t size = 0,
                           int line = 0,
                           int column = 0,
                           std::string comment = "",
                           std::string functionName = "",
                           std::string scriptName = "");

  explicit CodeEventRecord(v8::Isolate* isolate, v8::CodeEvent* code_event);

  void SetScriptId(int _id);

  v8::Local<v8::Integer> GetScriptId(v8::Isolate *isolate);
  v8::Local<v8::Integer> GetAddress(v8::Isolate* isolate);
  v8::Local<v8::Integer> GetPreviousAddress(v8::Isolate* isolate);
  v8::Local<v8::Integer> GetSize(v8::Isolate* isolate);
  v8::Local<v8::Integer> GetLine(v8::Isolate* isolate);
  v8::Local<v8::Integer> GetColumn(v8::Isolate* isolate);
  v8::Local<v8::Value> GetFunctionName(v8::Isolate* isolate);
  v8::Local<v8::Value> GetScriptName(v8::Isolate* isolate);
  v8::Local<v8::Value> GetComment(v8::Isolate* isolate);

  bool Equal(const CodeEventRecord *rhs) const;

  friend class CodeMap;
};

}; // namespace dd
