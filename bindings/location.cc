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

#include <utility>

#include <node.h>

#include "location.hh"

namespace dd {

Location::Location(std::shared_ptr<CodeEventRecord> code_event_record)
    : code_event_record(std::move(code_event_record)) {}

Location* Location::New(PerIsolateData* per_isolate,
                        std::shared_ptr<CodeEventRecord> code_event_record) {
  v8::Local<v8::Function> cons = Nan::New(per_isolate->LocationConstructor());
  auto inst = Nan::NewInstance(cons, 0, {}).ToLocalChecked();

  auto location = new Location(std::move(code_event_record));
  location->Wrap(inst);
  return location;
}

std::shared_ptr<CodeEventRecord> Location::GetCodeEventRecord() {
  return code_event_record;
}

NAN_GETTER(Location::GetScriptId) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetScriptId(info.GetIsolate()));
}

NAN_GETTER(Location::GetAddress) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetAddress(info.GetIsolate()));
}

NAN_GETTER(Location::GetPreviousAddress) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetPreviousAddress(info.GetIsolate()));
}

NAN_GETTER(Location::GetSize) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetSize(info.GetIsolate()));
}

NAN_GETTER(Location::GetFunctionName) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetFunctionName(info.GetIsolate()));
}

NAN_GETTER(Location::GetScriptName) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetScriptName(info.GetIsolate()));
}

NAN_GETTER(Location::GetLine) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetLine(info.GetIsolate()));
}

NAN_GETTER(Location::GetColumn) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetColumn(info.GetIsolate()));
}

NAN_GETTER(Location::GetComment) {
  Location* wrap = Nan::ObjectWrap::Unwrap<Location>(info.Holder());
  info.GetReturnValue().Set(
      wrap->GetCodeEventRecord()->GetComment(info.GetIsolate()));
}

NAN_MODULE_INIT(Location::Init) {
  auto class_name = Nan::New<v8::String>("CodeEvent").ToLocalChecked();

  auto tpl = Nan::New<v8::FunctionTemplate>(nullptr);
  tpl->SetClassName(class_name);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  auto proto = tpl->InstanceTemplate();

  Nan::SetAccessor(proto, Nan::New("scriptId").ToLocalChecked(), GetScriptId);
  Nan::SetAccessor(proto, Nan::New("address").ToLocalChecked(), GetAddress);
  Nan::SetAccessor(
      proto, Nan::New("previousAddress").ToLocalChecked(), GetPreviousAddress);
  Nan::SetAccessor(proto, Nan::New("size").ToLocalChecked(), GetSize);
  Nan::SetAccessor(proto, Nan::New("line").ToLocalChecked(), GetLine);
  Nan::SetAccessor(proto, Nan::New("column").ToLocalChecked(), GetColumn);
  Nan::SetAccessor(proto, Nan::New("comment").ToLocalChecked(), GetComment);
  Nan::SetAccessor(
      proto, Nan::New("functionName").ToLocalChecked(), GetFunctionName);
  Nan::SetAccessor(
      proto, Nan::New("scriptName").ToLocalChecked(), GetScriptName);

  auto fn = Nan::GetFunction(tpl).ToLocalChecked();
  auto per_isolate = PerIsolateData::For(target->GetIsolate());
  per_isolate->LocationConstructor().Reset(fn);
}

};  // namespace dd
