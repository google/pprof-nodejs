#pragma once

#include <nan.h>
#include <node.h>
#include <v8.h>
#include <memory>

namespace dd {

struct HeapProfilerState;

class PerIsolateData {
 private:
  Nan::Global<v8::Function> cpu_profiler_constructor;
  Nan::Global<v8::Function> location_constructor;
  Nan::Global<v8::Function> sample_constructor;
  Nan::Global<v8::Function> wall_profiler_constructor;
  std::shared_ptr<HeapProfilerState> heap_profiler_state;

  PerIsolateData() {}

 public:
  static PerIsolateData* For(v8::Isolate* isolate);

  Nan::Global<v8::Function>& CpuProfilerConstructor();
  Nan::Global<v8::Function>& LocationConstructor();
  Nan::Global<v8::Function>& SampleConstructor();
  Nan::Global<v8::Function>& WallProfilerConstructor();
  std::shared_ptr<HeapProfilerState>& GetHeapProfilerState();
};

}  // namespace dd
