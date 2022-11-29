#pragma once

#include <cstdint>
#include <memory>
#include <atomic>

#include <nan.h>
#include <uv.h>

#include "../code-map.hh"
#include "../cpu-time.hh"
#include "../sample.hh"
#include "../wrap.hh"

namespace dd {

class SampleBuffer
{
public:
  using SamplePtr = std::unique_ptr<Sample>;

  explicit SampleBuffer(size_t size) : samples_(std::make_unique<SamplePtr[]>(size)),
                                        capacity_(size),
                                        size_(0),
                                        back_index_(0),
                                        front_index_(0) {}

  bool full() const { return size_ == capacity_; }
  bool empty() const { return size_ == 0; }

  SamplePtr &front()
  {
    return samples_[front_index_];
  }

  const SamplePtr &front() const
  {
    return samples_[front_index_];
  }

  void push_back(SamplePtr ptr)
  {
    if (full())
    {
      if (empty())
      {
        return;
      }
      increment(back_index_);
      front_index_ = back_index_;
    }
    else
    {
      samples_[back_index_] = std::move(ptr);
      increment(back_index_);
      ++size_;
    }
  }

  SamplePtr pop_front()
  {
    auto idx = front_index_;
    increment(front_index_);
    --size_;
    return std::move(samples_[idx]);
  }

private:
  void increment(size_t &idx) const
  {
    idx = idx + 1 == capacity_ ? 0 : idx + 1;
  }
  std::unique_ptr<SamplePtr[]> samples_;
  size_t capacity_;
  size_t size_;
  size_t back_index_;
  size_t front_index_;
};

class CpuProfiler : public Nan::ObjectWrap {
  friend class CodeMap;

 private:
  v8::Isolate* isolate_;
  uv_async_t* async;
  std::shared_ptr<CodeMap> code_map;
  CpuTime cpu_time;
  SampleBuffer last_samples;
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
  static CpuProfiler* New();

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

} // namespace dd
