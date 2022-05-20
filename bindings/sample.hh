#pragma once

#include "code-map.hh"
#include "wrap.hh"

#include <node_object_wrap.h>
#include <v8.h>

namespace dd {

class Sample : public Nan::ObjectWrap {
 private:
  static const size_t frames_limit = 255;

  std::shared_ptr<LabelWrap> labels_;
  uint64_t timestamp;
  std::vector<uintptr_t> frames;
  Nan::Global<v8::Array> locations_;
  int64_t cpu_time;

 public:
  Sample(v8::Isolate* isolate,
         std::shared_ptr<LabelWrap> labels,
         std::vector<uintptr_t> frames,
         int64_t cpu_time);

  Sample(v8::Isolate* isolate,
         std::shared_ptr<LabelWrap> labels,
         int64_t cpu_time);

  std::vector<uintptr_t> GetFrames();
  v8::Local<v8::Array> Symbolize(
    std::shared_ptr<CodeMap> code_map);

  v8::Local<v8::Integer> GetCpuTime(v8::Isolate* isolate);
  v8::Local<v8::Value> GetLabels(v8::Isolate* isolate);
  v8::Local<v8::Array> GetLocations(v8::Isolate* isolate);

  v8::Local<v8::Object> ToObject(v8::Isolate* isolate);

  static NAN_GETTER(GetCpuTime);
  static NAN_GETTER(GetLabels);
  static NAN_GETTER(GetLocations);

  static NAN_MODULE_INIT(Init);
};

} // namespace dd
