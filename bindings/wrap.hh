#pragma once

#include <v8.h> // cppcheck-suppress missingIncludeSystem

namespace dd {

class LabelWrap {
 protected:
  v8::Global<v8::Value> handle_;

 public:
  LabelWrap(v8::Local<v8::Value> object)
    : handle_(v8::Isolate::GetCurrent(), object) {}

  v8::Local<v8::Value> handle() {
    return handle_.Get(v8::Isolate::GetCurrent());
  }
};

}; // namespace dd
