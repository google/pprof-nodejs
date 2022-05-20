#include <queue>

#include <v8-profiler.h>
#include <v8.h>
#include <node.h>
#include <uv.h>

#include "../per-isolate-data.hh"
#include "../location.hh"
#include "cpu.hh"

namespace dd {

CpuProfiler::CpuProfiler()
  : isolate_(v8::Isolate::GetCurrent()),
    async(new uv_async_t()),
    code_map(CodeMap::For(isolate_)),
    samples(Nan::New<v8::Array>()),
    sampler_running(false) {
  // TODO: Move symbolizer worker to a separate class?
  // Initialize libuv async worker to process samples when JS thread is idle
  uv_async_init(Nan::GetCurrentEventLoop(), async, Run);
  // Unref the async worker so it won't hold the loop open when there are no
  // other tasks. This allows it to clean itself up automatically.
  uv_unref(reinterpret_cast<uv_handle_t*>(async));
  // The async worker needs a reference to the profiler instance so it can get
  // the pending sample and the vector to push symbolized samples to.
  async->data = static_cast<void*>(this);
  uv_sem_init(&sampler_thread_done, 1);
}

CpuProfiler::~CpuProfiler() {
  uv_close(reinterpret_cast<uv_handle_t*>(async),
    [](uv_handle_t* handle){
      delete reinterpret_cast<uv_handle_t*>(handle);
    });
  Stop();

  uv_sem_wait(&sampler_thread_done);
  uv_sem_destroy(&sampler_thread_done);
}

CpuProfiler* CpuProfiler::New() {
  auto isolate = v8::Isolate::GetCurrent();
  auto per_isolate = PerIsolateData::For(isolate);
  v8::Local<v8::Function> cons = Nan::New(
      per_isolate->CpuProfilerConstructor());
  auto inst = Nan::NewInstance(cons, 0, {}).ToLocalChecked();
  return Nan::ObjectWrap::Unwrap<CpuProfiler>(inst);
}

v8::Local<v8::Number> CpuProfiler::GetFrequency() {
  return Nan::New<v8::Number>(frequency);
}

// NOTE: last sample must be a unique_ptr to ensure it is cleaned up
// when no longer referenced. However when `ProcessSample()` is called
// it will be released from the unique_ptr as it will become owned by
// the JavaScript thread which creates a corresponding handle object,
// making it garbage-collectable.
void CpuProfiler::SetLastSample(std::unique_ptr<Sample> sample) {
  lastSample = std::move(sample);
}

// NOTE: For test/debug purposes only
Sample* CpuProfiler::GetLastSample() {
  return lastSample.get();
}

void CpuProfiler::CaptureSample(v8::Isolate* isolate) {
  const std::lock_guard<std::mutex> lock(mutex);
  auto diff = cpu_time.Diff();
  SetLastSample(
    std::make_unique<Sample>(isolate, labels_, diff));
}

// TODO: Make sampler thread a separate class?
void CpuProfiler::SamplerThread(double hz) {
  using hrc = std::chrono::high_resolution_clock;
  std::chrono::duration<double> interval(1.0 / hz);
  while (sampler_running) {
    isolate_->RequestInterrupt([](v8::Isolate* isolate, void* data) {
      auto profiler = static_cast<CpuProfiler*>(data);
      profiler->CaptureSample(isolate);

      // Notify symbolizer worker that we have a new sample
      uv_async_send(profiler->async);
    }, this);

    std::this_thread::sleep_for(interval);
  }
  uv_sem_post(&sampler_thread_done);
}

void CpuProfiler::ProcessSample() {
  v8::HandleScope scope(isolate_);

  Sample* sample;
  {
    const std::lock_guard<std::mutex> lock(mutex);
    sample = lastSample.release();
  }

  if (!sample) return;
  if (!sample->Symbolize(code_map)->Length()) {
    delete sample;
    return;
  }

  // Append the newly processed sample to the samples array
  auto arr = samples.Get(isolate_);
  arr->Set(isolate_->GetCurrentContext(), arr->Length(),
    sample->ToObject(isolate_)).Check();
}

void CpuProfiler::Run(uv_async_t* handle) {
  auto profiler = static_cast<CpuProfiler*>(handle->data);
  profiler->ProcessSample();
}

void CpuProfiler::Start(double hz) {
  if (sampler_running) return;

  frequency = hz;
  sampler_running = true;
  uv_sem_init(&sampler_thread_done, 0);
  uv_thread_create(&sampler_thread, [](void* arg) {
    auto profiler = static_cast<CpuProfiler*>(arg);
    profiler->SamplerThread(profiler->frequency);
  }, this);
  start_time = uv_hrtime();
  code_map->Enable();
}

void CpuProfiler::Stop() {
  if (!sampler_running) return;

  frequency = 0;
  sampler_running = false;
  code_map->Disable();
}

v8::Local<v8::Value> CpuProfiler::GetLabels() {
  const std::lock_guard<std::mutex> lock(mutex);
  if (!labels_) return Nan::Undefined();
  return labels_->handle();
}

void CpuProfiler::SetLabels(v8::Local<v8::Value> value) {
  const std::lock_guard<std::mutex> lock(mutex);
  labels_ = std::make_shared<LabelWrap>(value);
}

uint32_t CpuProfiler::GetSampleCount() {
  return samples.Get(isolate_)->Length();
}

// TODO: Probably should make an explicit clear method rather than
// implicitly clearing whenever getting the samples array
v8::Local<v8::Array> CpuProfiler::GetSamples() {
  auto array = samples.Get(isolate_);
  samples.Reset(Nan::New<v8::Array>());
  return array;
}

v8::Local<v8::Value> CpuProfiler::GetProfile() {
  auto profile = Nan::New<v8::Object>();
  auto end_time = uv_hrtime();

  Nan::Set(profile,
           Nan::New("name").ToLocalChecked(),
           Nan::New("(root)").ToLocalChecked()).Check();
  Nan::Set(profile,
           Nan::New("startTime").ToLocalChecked(),
           v8::BigInt::New(isolate_, start_time)).Check();
  Nan::Set(profile,
           Nan::New("endTime").ToLocalChecked(),
           v8::BigInt::New(isolate_, end_time)).Check();
  Nan::Set(profile,
           Nan::New("samples").ToLocalChecked(),
           GetSamples()).Check();

  start_time = end_time;

  return profile;
}

NAN_METHOD(CpuProfiler::New) {
  if (info.IsConstructCall()) {
    CpuProfiler* profiler = new CpuProfiler();
    profiler->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    auto per_isolate = PerIsolateData::For(info.GetIsolate());
    v8::Local<v8::Function> cons = Nan::New(
        per_isolate->CpuProfilerConstructor());
    info.GetReturnValue().Set(Nan::NewInstance(cons, 0, {}).ToLocalChecked());
  }
}

NAN_GETTER(CpuProfiler::GetFrequency) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetFrequency());
}

NAN_METHOD(CpuProfiler::Start) {
  if (!info[0]->IsNumber()) {
    Nan::ThrowTypeError("hz is not a number");
    return;
  }

  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  profiler->Start(Nan::To<double>(info[0]).FromJust());
}

NAN_METHOD(CpuProfiler::Stop) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  profiler->Stop();
}

NAN_METHOD(CpuProfiler::CaptureSample) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  profiler->CaptureSample(info.GetIsolate());
}

NAN_METHOD(CpuProfiler::ProcessSample) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  profiler->ProcessSample();
}

NAN_GETTER(CpuProfiler::GetLabels) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetLabels());
}

NAN_SETTER(CpuProfiler::SetLabels) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  profiler->SetLabels(value);
}

NAN_METHOD(CpuProfiler::GetSamples) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetSamples());
}

NAN_METHOD(CpuProfiler::GetProfile) {
  auto profiler = Nan::ObjectWrap::Unwrap<CpuProfiler>(info.Holder());
  info.GetReturnValue().Set(profiler->GetProfile());
}

NAN_MODULE_INIT(CpuProfiler::Init) {
  Location::Init(target);
  Sample::Init(target);

  auto class_name = Nan::New<v8::String>("CpuProfiler")
      .ToLocalChecked();

  auto tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(class_name);
  tpl->InstanceTemplate()
      ->SetInternalFieldCount(1);

  auto inst = tpl->InstanceTemplate();
  Nan::SetAccessor(inst,
    Nan::New("labels").ToLocalChecked(),
    GetLabels, SetLabels);

  Nan::SetAccessor(inst,
    Nan::New("frequency").ToLocalChecked(),
    GetFrequency);

  Nan::SetPrototypeMethod(tpl, "start", Start);
  Nan::SetPrototypeMethod(tpl, "stop", Stop);
  Nan::SetPrototypeMethod(tpl, "captureSample", CaptureSample);
  Nan::SetPrototypeMethod(tpl, "processSample", ProcessSample);
  Nan::SetPrototypeMethod(tpl, "samples", GetSamples);
  Nan::SetPrototypeMethod(tpl, "profile", GetProfile);

  auto fn = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(target, class_name, fn);
  auto per_isolate = PerIsolateData::For(target->GetIsolate());
  per_isolate->CpuProfilerConstructor().Reset(fn);
}

} // namespace dd
