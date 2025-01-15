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

#include <v8.h>

namespace dd {
class ProfileTranslator {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> emptyArray = v8::Array::New(isolate, 0);

 protected:
  v8::Local<v8::Object> NewObject() { return v8::Object::New(isolate); }

  v8::Local<v8::Integer> NewInteger(int x) {
    return v8::Integer::New(isolate, x);
  }

  v8::Local<v8::Boolean> NewBoolean(bool x) {
    return v8::Boolean::New(isolate, x);
  }

  template <typename T>
  v8::Local<v8::Number> NewNumber(T x) {
    return v8::Number::New(isolate, x);
  }

  v8::Local<v8::Array> NewArray(int length) {
    return length == 0 ? emptyArray : v8::Array::New(isolate, length);
  }

  v8::Local<v8::String> NewString(const char* str) {
    return v8::String::NewFromUtf8(isolate, str).ToLocalChecked();
  }

  v8::MaybeLocal<v8::Value> Get(v8::Local<v8::Array> arr, uint32_t index) {
    return arr->Get(context, index);
  }

  v8::Maybe<bool> Set(v8::Local<v8::Array> arr,
                      uint32_t index,
                      v8::Local<v8::Value> value) {
    return arr->Set(context, index, value);
  }

  v8::Maybe<bool> Set(v8::Local<v8::Object> obj,
                      v8::Local<v8::Value> key,
                      v8::Local<v8::Value> value) {
    return obj->Set(context, key, value);
  }

  ProfileTranslator() = default;
};
};  // namespace dd
