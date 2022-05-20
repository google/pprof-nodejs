#include <algorithm>

#include <nan.h>

#include "per-isolate-data.hh"
#include "sample.hh"
#include "location.hh"
#include "code-map.hh"

#include <iostream>

#include <uv.h>

namespace dd {

std::vector<uintptr_t> MakeFrames(v8::Isolate* isolate) {
  static const size_t frames_limit = 255;
  void* frames[frames_limit];

  v8::SampleInfo sample_info;
  v8::RegisterState register_state;
  register_state.pc = nullptr;
  register_state.fp = &register_state;
  register_state.sp = &register_state;

  isolate->GetStackSample(register_state, frames, frames_limit, &sample_info);

  size_t n = sample_info.frames_count;
  std::vector<uintptr_t> output(n);

  for (size_t i = 0; i < n; i++) {
    output.push_back(reinterpret_cast<uintptr_t>(frames[i]));
  }

  return output;
}

Sample::Sample(v8::Isolate* isolate,
               std::shared_ptr<LabelWrap> labels,
               std::vector<uintptr_t> frames,
               int64_t cpu_time)
  : labels_(std::move(labels)),
    frames(frames),
    cpu_time(cpu_time) {
  timestamp = uv_hrtime();
}

Sample::Sample(v8::Isolate* isolate,
               std::shared_ptr<LabelWrap> labels,
               int64_t cpu_time)
  : Sample(isolate, std::move(labels), MakeFrames(isolate), cpu_time) {}

std::vector<uintptr_t> Sample::GetFrames() {
  return frames;
}

v8::Local<v8::Array> Sample::Symbolize(
  std::shared_ptr<CodeMap> code_map) {
  auto isolate = v8::Isolate::GetCurrent();
  if (!locations_.IsEmpty()) return locations_.Get(isolate);

  auto locations = Nan::New<v8::Array>();

  auto ToCodeEventRecord = [code_map](uintptr_t address)
    -> std::shared_ptr<CodeEventRecord> {
    return code_map->Lookup(address);
  };

  std::deque<std::shared_ptr<CodeEventRecord>> records;
  std::transform(frames.begin(), frames.end(), std::front_inserter(records),
                 ToCodeEventRecord);

  for (auto record : records) {
    if (record) {
      auto location = Location::New(isolate, record);
      Nan::Set(locations, locations->Length(), location->handle()).Check();
    }
  }

  locations_.Reset(locations);
  return locations;
}

v8::Local<v8::Integer> Sample::GetCpuTime(v8::Isolate* isolate) {
  return v8::Integer::New(isolate, cpu_time);
}

v8::Local<v8::Value> Sample::GetLabels(v8::Isolate* isolate) {
  if (!labels_) return v8::Undefined(isolate);
  return labels_->handle();
}

v8::Local<v8::Array> Sample::GetLocations(v8::Isolate* isolate) {
  if (locations_.IsEmpty()) {
    return Nan::New<v8::Array>();
  }
  return locations_.Get(isolate);
}

NAN_GETTER(Sample::GetCpuTime) {
  Sample* wrap = Nan::ObjectWrap::Unwrap<Sample>(info.Holder());
  info.GetReturnValue().Set(wrap->GetCpuTime(info.GetIsolate()));
}

NAN_GETTER(Sample::GetLabels) {
  Sample* wrap = Nan::ObjectWrap::Unwrap<Sample>(info.Holder());
  info.GetReturnValue().Set(wrap->GetLabels(info.GetIsolate()));
}

NAN_GETTER(Sample::GetLocations) {
  Sample* wrap = Nan::ObjectWrap::Unwrap<Sample>(info.Holder());
  info.GetReturnValue().Set(wrap->GetLocations(info.GetIsolate()));
}

v8::Local<v8::Object> Sample::ToObject(v8::Isolate* isolate) {
  if (!persistent().IsEmpty()) {
    return handle();
  }

  auto per_isolate = PerIsolateData::For(isolate);
  v8::Local<v8::Function> cons = Nan::New(
      per_isolate->SampleConstructor());
  auto inst = Nan::NewInstance(cons, 0, {}).ToLocalChecked();

  Wrap(inst);

  return handle();
}

NAN_MODULE_INIT(Sample::Init) {
  auto class_name = Nan::New<v8::String>("Sample")
      .ToLocalChecked();

  auto tpl = Nan::New<v8::FunctionTemplate>(nullptr);
  tpl->SetClassName(class_name);
  tpl->InstanceTemplate()
      ->SetInternalFieldCount(1);

  // auto proto = tpl->PrototypeTemplate();
  auto proto = tpl->InstanceTemplate();

  Nan::SetAccessor(
      proto,
      Nan::New("cpuTime").ToLocalChecked(),
      GetCpuTime);
  Nan::SetAccessor(
      proto,
      Nan::New("labels").ToLocalChecked(),
      GetLabels);
  Nan::SetAccessor(
      proto,
      Nan::New("locations").ToLocalChecked(),
      GetLocations);

  auto fn = Nan::GetFunction(tpl).ToLocalChecked();
  auto per_isolate = PerIsolateData::For(target->GetIsolate());
  per_isolate->SampleConstructor().Reset(fn);
}

} // namespace dd
