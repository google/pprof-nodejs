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

#include <nan.h>
#include <node_object_wrap.h>  // cppcheck-suppress missingIncludeSystem

#include "code-event-record.hh"
#include "per-isolate-data.hh"

namespace dd {

class Location : public Nan::ObjectWrap {
 private:
  std::shared_ptr<CodeEventRecord> code_event_record;

 public:
  explicit Location(std::shared_ptr<CodeEventRecord> code_event_record);

  static Location* New(PerIsolateData* per_isolate,
                       std::shared_ptr<CodeEventRecord> code_event_record);

  std::shared_ptr<CodeEventRecord> GetCodeEventRecord();

  static NAN_GETTER(GetScriptId);
  static NAN_GETTER(GetAddress);
  static NAN_GETTER(GetPreviousAddress);
  static NAN_GETTER(GetSize);
  static NAN_GETTER(GetLine);
  static NAN_GETTER(GetColumn);
  static NAN_GETTER(GetFunctionName);
  static NAN_GETTER(GetScriptName);
  static NAN_GETTER(GetComment);

  static NAN_MODULE_INIT(Init);

  friend class CodeMap;

  using Nan::ObjectWrap::Ref;
  using Nan::ObjectWrap::Unref;
};

};  // namespace dd
