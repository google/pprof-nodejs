#include "node.h"
#include "nan.h"
#include "v8.h"

#include "profilers/cpu.hh"
#include "profilers/heap.hh"
#include "profilers/wall.hh"

NODE_MODULE_INIT(/* exports, module, context */) {
  dd::CpuProfiler::Init(exports);
  dd::HeapProfiler::Init(exports);
  dd::WallProfiler::Init(exports);
}
