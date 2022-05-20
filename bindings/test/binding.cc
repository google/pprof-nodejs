#include <unordered_map>
#include <sstream>
#include <cstdlib>

#include "node.h"
#include "v8.h"
#include "nan.h"
#include "tap.h"

#include "../profilers/cpu.hh"

#include "profilers/cpu.test.hh"
#include "code-event-record.test.hh"
#include "code-map.test.hh"
#include "cpu-time.test.hh"
#include "location.test.hh"
#include "sample.test.hh"

NODE_MODULE_INIT(/* exports, module, context */) {
  // Need to do this so the class templates get constructed
  dd::CpuProfiler::Init(exports);

  Tap t;
  const char* env_var = std::getenv("TEST");
  std::string name(env_var == nullptr ? "" : env_var);

  std::unordered_map<std::string, std::function<void(Tap&)>> tests = {
    {"profilers/cpu", test_profilers_cpu_profiler},
    {"code-event-record", test_code_event_record},
    {"code-map", test_code_map},
    {"cpu-time", test_cpu_time},
    {"location", test_location},
    {"sample", test_sample},
  };

  if (name.empty()) {
    t.plan(tests.size());
    for (auto test : tests) {
      t.test(test.first, test.second);
    }
  } else {
    t.plan(1);
    if (tests.count(name)) {
      t.test(name, tests[name]);
    } else {
      std::ostringstream s;
      s << "Unknown test: " << name;
      t.fail(s.str());
    }
  }

  // End test and set `process.exitCode`
  int exitCode = t.end();
  auto processKey = Nan::New<v8::String>("process").ToLocalChecked();
  auto process = Nan::Get(context->Global(), processKey).ToLocalChecked();
  Nan::Set(process.As<v8::Object>(),
      Nan::New<v8::String>("exitCode").ToLocalChecked(),
      Nan::New<v8::Number>(exitCode));
}
