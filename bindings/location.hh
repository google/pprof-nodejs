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
