#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <nan.h>
#include <uv.h>

#include "../buffer.hh"
#include "../code-map.hh"
#include "../cpu-time.hh"
#include "../sample.hh"
#include "../wrap.hh"

namespace dd {

class CpuProfiler : public Nan::ObjectWrap {
  friend class CodeMap;

 private:
  v8::Isolate* isolate_;
  uv_async_t* async;
  std::shared_ptr<CodeMap> code_map;
  CpuTime cpu_time;
  RingBuffer<std::unique_ptr<Sample>> last_samples;
  std::shared_ptr<LabelWrap> labels_;
  double frequency = 0;
  Nan::Global<v8::Array> samples;
  uint64_t start_time;
  uv_sem_t sampler_thread_done;
  uv_thread_t sampler_thread;
  std::atomic_bool sampler_running;

 public:
  CpuProfiler();
  ~CpuProfiler();

  // Disable copies and moves
  CpuProfiler(const CpuProfiler& other) = delete;
  CpuProfiler(CpuProfiler&& other) = delete;
  CpuProfiler& operator=(const CpuProfiler& other) = delete;
  CpuProfiler& operator=(CpuProfiler&& other) = delete;

  v8::Local<v8::Number> GetFrequency();

  void SetLastSample(std::unique_ptr<Sample> sample);
  Sample* GetLastSample();
  void CaptureSample(v8::Isolate* isolate);
  void SamplerThread(double hz);

  void ProcessSample();
  static void Run(uv_async_t* handle);

  v8::Local<v8::Value> GetLabels();
  void SetLabels(v8::Local<v8::Value>);
  void Start(double hz);
  void Stop();
  void StopAndWaitThread();
  uint32_t GetSampleCount();
  v8::Local<v8::Array> GetSamples();
  v8::Local<v8::Value> GetProfile();

  static NAN_METHOD(New);
  static NAN_GETTER(GetFrequency);
  static NAN_GETTER(GetLabels);
  static NAN_SETTER(SetLabels);
  static NAN_METHOD(Start);
  static NAN_METHOD(Stop);
  static NAN_METHOD(CaptureSample);
  static NAN_METHOD(ProcessSample);
  static NAN_METHOD(GetSamples);
  static NAN_METHOD(GetProfile);

  static NAN_MODULE_INIT(Init);
};

}  // namespace dd
