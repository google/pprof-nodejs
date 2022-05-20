#pragma once

#include <nan.h>
#include <v8-profiler.h>

namespace dd {

class WallProfiler : public Nan::ObjectWrap {
 private:
  int samplingInterval = 0;
  v8::CpuProfiler* cpuProfiler = nullptr;
  ~WallProfiler();
  void Dispose();

  // A new CPU profiler object will be created each time profiling is started
  // to work around https://bugs.chromium.org/p/v8/issues/detail?id=11051.
  v8::CpuProfiler* GetProfiler();
 public:
  explicit WallProfiler(int interval);

  static NAN_METHOD(New);
  static NAN_METHOD(Dispose);
  static NAN_METHOD(Start);
  static NAN_METHOD(Stop);
  static NAN_MODULE_INIT(Init);
};

} // namespace dd
