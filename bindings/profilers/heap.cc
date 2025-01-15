/**
 * Copyright 2018 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "heap.hh"

#include "defer.hh"
#include "per-isolate-data.hh"
#include "translate-heap-profile.hh"

#include <chrono>
#include <memory>
#include <vector>

#include <node.h>
#include <v8-profiler.h>

namespace dd {

static size_t NearHeapLimit(void* data,
                            size_t current_heap_limit,
                            size_t initial_heap_limit);
static void InterruptCallback(v8::Isolate* isolate, void* data);
static void AsyncCallback(uv_async_t* handle);

struct Node {
  using Allocation = v8::AllocationProfile::Allocation;
  std::string name;
  std::string script_name;
  int line_number;
  int column_number;
  int script_id;
  std::vector<std::shared_ptr<Node>> children;
  std::vector<Allocation> allocations;
};

enum CallbackMode {
  kNoCallback = 0,
  kAsyncCallback = 1,
  kInterruptCallback = 2,
};

struct HeapProfilerState {
  explicit HeapProfilerState(v8::Isolate* isolate) : isolate(isolate) {}

  ~HeapProfilerState() {
    UninstallNearHeapLimitCallback();
    if (async) {
      // defer deletion of async when uv_close callback is invoked
      uv_close(reinterpret_cast<uv_handle_t*>(async), [](uv_handle_t* handle) {
        delete reinterpret_cast<uv_async_t*>(handle);
      });
      async = nullptr;
    }
  }

  void UninstallNearHeapLimitCallback() {
    if (isolate && callbackInstalled) {
      isolate->RemoveNearHeapLimitCallback(&NearHeapLimit, 0);
      callbackInstalled = false;
    }
  }

  void InstallNearHeapLimitCallback() {
    if (isolate) {
      isolate->AddNearHeapLimitCallback(&NearHeapLimit, nullptr);
      callbackInstalled = true;
    }
  }

  void RegisterAsyncCallback() {
    if (async) {
      return;
    }
    // async is dynamically allocated so that its lifetime can be different
    // from the one of HeapProfilerState since uv_close is asynchronous
    async = new uv_async_t();
    uv_async_init(Nan::GetCurrentEventLoop(), async, AsyncCallback);
    uv_unref(reinterpret_cast<uv_handle_t*>(async));
  }

  void OnNewProfile() {
    profile.reset();
    if (!callbackInstalled) {
      // Reinstall NearHeapLimit callback if it was removed before
      InstallNearHeapLimitCallback();
    }
  }

  v8::Isolate* isolate = nullptr;
  uint32_t heap_extension_size = 0;
  uint32_t max_heap_extension_count = 0;
  uint32_t current_heap_extension_count = 0;
  uv_async_t* async = nullptr;
  std::shared_ptr<Node> profile;
  std::vector<std::string> export_command;
  bool dumpProfileOnStderr = false;
  Nan::Callback callback;
  uint32_t callbackMode = 0;
  bool isMainThread = true;
  bool callbackInstalled = false;
  bool insideCallback = false;
};

std::shared_ptr<Node> TranslateAllocationProfileToCpp(
    v8::AllocationProfile::Node* node) {
  auto new_node = std::make_shared<Node>();
  new_node->line_number = node->line_number;
  new_node->column_number = node->column_number;
  new_node->script_id = node->script_id;
  Nan::Utf8String name(node->name);
  new_node->name.assign(*name, name.length());
  Nan::Utf8String script_name(node->script_name);
  new_node->script_name.assign(*script_name, script_name.length());

  new_node->children.reserve(node->children.size());
  for (auto& child : node->children) {
    new_node->children.push_back(TranslateAllocationProfileToCpp(child));
  }

  new_node->allocations.reserve(node->allocations.size());
  for (auto& allocation : node->allocations) {
    new_node->allocations.push_back(allocation);
  }
  return new_node;
}

v8::Local<v8::Value> TranslateAllocationProfile(Node* node) {
  v8::Local<v8::Object> js_node = Nan::New<v8::Object>();

  Nan::Set(js_node,
           Nan::New<v8::String>("name").ToLocalChecked(),
           Nan::New(node->name).ToLocalChecked());
  Nan::Set(js_node,
           Nan::New<v8::String>("scriptName").ToLocalChecked(),
           Nan::New(node->script_name).ToLocalChecked());
  Nan::Set(js_node,
           Nan::New<v8::String>("scriptId").ToLocalChecked(),
           Nan::New<v8::Integer>(node->script_id));
  Nan::Set(js_node,
           Nan::New<v8::String>("lineNumber").ToLocalChecked(),
           Nan::New<v8::Integer>(node->line_number));
  Nan::Set(js_node,
           Nan::New<v8::String>("columnNumber").ToLocalChecked(),
           Nan::New<v8::Integer>(node->column_number));

  v8::Local<v8::Array> children = Nan::New<v8::Array>(node->children.size());
  for (size_t i = 0; i < node->children.size(); i++) {
    Nan::Set(children, i, TranslateAllocationProfile(node->children[i].get()));
  }
  Nan::Set(
      js_node, Nan::New<v8::String>("children").ToLocalChecked(), children);
  v8::Local<v8::Array> allocations =
      Nan::New<v8::Array>(node->allocations.size());
  for (size_t i = 0; i < node->allocations.size(); i++) {
    v8::AllocationProfile::Allocation alloc = node->allocations[i];
    v8::Local<v8::Object> js_alloc = Nan::New<v8::Object>();
    Nan::Set(js_alloc,
             Nan::New<v8::String>("sizeBytes").ToLocalChecked(),
             Nan::New<v8::Number>(alloc.size));
    Nan::Set(js_alloc,
             Nan::New<v8::String>("count").ToLocalChecked(),
             Nan::New<v8::Number>(alloc.count));
    Nan::Set(allocations, i, js_alloc);
  }
  Nan::Set(js_node,
           Nan::New<v8::String>("allocations").ToLocalChecked(),
           allocations);
  return js_node;
}

static void dumpAllocationProfile(FILE* file,
                                  Node* node,
                                  std::string& cur_stack) {
  auto initial_len = cur_stack.size();
  char buf[256];

  snprintf(buf,
           sizeof(buf),
           "%s%s:%s:%d",
           cur_stack.empty() ? "" : ";",
           node->script_name.empty() ? "_" : node->script_name.c_str(),
           node->name.empty() ? "(anonymous)" : node->name.c_str(),
           node->line_number);
  cur_stack += buf;
  for (auto& allocation : node->allocations) {
    fprintf(file,
            "%s %u %zu\n",
            cur_stack.c_str(),
            allocation.count,
            allocation.count * allocation.size);
  }
  for (auto& child : node->children) {
    dumpAllocationProfile(file, child.get(), cur_stack);
  }
  cur_stack.resize(initial_len);
}

static void dumpAllocationProfile(FILE* file, Node* node) {
  std::string stack;
  dumpAllocationProfile(file, node, stack);
}

static void dumpAllocationProfileAsJSON(FILE* file, Node* node) {
  fprintf(
      file,
      R"({"name":"%s","scriptName":"%s","scriptId":%d,"lineNumber":%d,"columnNumber":%d,"children":[)",
      node->name.c_str(),
      node->script_name.c_str(),
      node->script_id,
      node->line_number,
      node->column_number);

  bool first = true;
  for (auto& child : node->children) {
    if (!first) {
      fputs(",", file);
    } else {
      first = false;
    }
    dumpAllocationProfileAsJSON(file, child.get());
  }
  fprintf(file, R"(],"allocations":[)");
  first = true;
  for (auto& allocation : node->allocations) {
    fprintf(file,
            R"(%s{"sizeBytes":%zu,"count":%d})",
            first ? "" : ",",
            allocation.size,
            allocation.count);
    first = false;
  }
  fputs("]}", file);
}

static void OnExit(uv_process_t* req, int64_t, int) {
  if (req->data) {
    uv_timer_stop(reinterpret_cast<uv_timer_t*>(req->data));
  }
  uv_close((uv_handle_t*)req, nullptr);
}

static void CloseLoop(uv_loop_t& loop) {
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_walk(
      &loop,
      [](uv_handle_t* handle, void* arg) {
        if (!uv_is_closing(handle)) {
          uv_close(handle, nullptr);
        }
      },
      nullptr);
  int r;
  do {
    r = uv_run(&loop, UV_RUN_ONCE);
  } while (r != 0);

  if (uv_loop_close(&loop)) {
    fprintf(stderr, "Failed to close event loop\n");
  }
}

static int CreateTempFile(uv_loop_t& loop, std::string& filepath) {
  char buf[PATH_MAX];
  size_t sz = sizeof(buf);
  int r;
  if ((r = uv_os_tmpdir(buf, &sz)) != 0) {
    fprintf(stderr, "Failed to retrieve temp directory: %s\n", uv_strerror(r));
    return -1;
  }

#if defined(__linux__) || defined(__APPLE__)
  filepath = std::string{buf, sz} + "/heap_profile_XXXXXX";
  int fd = mkstemp(&filepath[0]);
  if (fd < 0) {
    fprintf(stderr,
            "Failed to create temp file %s : %s\n",
            filepath.c_str(),
            strerror(errno));
    return -1;
  }
  return fd;
#else
  // Use custom implementation of mkstemp() for Windows
  // uv_fs_mkstemp() is not used because it fails unexpectedly on Windows
  // (fail fast exception is raised when trying to write to the returned file
  // descriptor)
  const int max_tries = 3;
  for (int i = 0; i < max_tries; ++i) {
    filepath = std::string{buf, sz} + "/heap_profile_" +
               std::to_string(
                   std::chrono::system_clock::now().time_since_epoch().count());
    uv_fs_t fs_req{};
    int fd = uv_fs_open(&loop,
                        &fs_req,
                        filepath.c_str(),
                        UV_FS_O_CREAT | UV_FS_O_EXCL | UV_FS_O_WRONLY,
                        0600,
                        nullptr);
    uv_fs_req_cleanup(&fs_req);
    if (fd >= 0) {
      return r;
    }
    if (fd != UV_EEXIST) {
      fprintf(stderr, "Failed to create temp file: %s\n", uv_strerror(fd));
      return -1;
    }
  }
  return -1;
#endif
}

static void ExportProfile(HeapProfilerState& state) {
  const int64_t timeoutMs = 5000;
  uv_loop_t loop;
  int r;

  if ((r = uv_loop_init(&loop)) != 0) {
    fprintf(stderr, "Failed to init new event loop: %s\n", uv_strerror(r));
    return;
  }

  defer {
    CloseLoop(loop);
  };

  std::string filepath;
  int fd;
  if ((fd = CreateTempFile(loop, filepath)) < 0) {
    return;
  }
  FILE* file = fdopen(fd, "w");
  dumpAllocationProfileAsJSON(file, state.profile.get());
  fclose(file);
  std::vector<char*> args;
  for (auto& arg : state.export_command) {
    args.push_back(const_cast<char*>(arg.data()));
  }
  args.push_back(&filepath[0]);
  args.push_back(nullptr);
  uv_process_options_t options = {};
  options.flags = UV_PROCESS_DETACHED;
  options.file = args[0];
  options.args = args.data();
  options.exit_cb = &OnExit;
  uv_process_t child_req;
  uv_timer_t timer;
  timer.data = &child_req;
  child_req.data = &timer;

  fprintf(stderr, "Spawning export process:");
  for (auto arg : args) {
    fprintf(stderr, " %s", arg ? arg : "\n");
  }
  if ((r = uv_spawn(&loop, &child_req, &options))) {
    fprintf(stderr, "Failed to spawn export process: %s\n", uv_strerror(r));
    return;
  }
  if ((r = uv_timer_init(&loop, &timer)) != 0) {
    fprintf(stderr, "Failed to init timer: %s\n", uv_strerror(r));
    return;
  }
  if ((r = uv_timer_start(
           &timer,
           [](uv_timer_t* handle) {
             uv_process_kill(reinterpret_cast<uv_process_t*>(handle->data),
                             SIGKILL);
           },
           timeoutMs,
           0))) {
    fprintf(stderr, "Failed to start timer: %s\n", uv_strerror(r));
    return;
  }
  uv_run(&loop, UV_RUN_DEFAULT);

  // Delete temp file
  uv_fs_t fs_req{};
  uv_fs_unlink(&loop, &fs_req, filepath.c_str(), nullptr);
  uv_fs_req_cleanup(&fs_req);
}

size_t NearHeapLimit(void* data,
                     size_t current_heap_limit,
                     size_t initial_heap_limit) {
  auto isolate = v8::Isolate::GetCurrent();
  auto state = PerIsolateData::For(isolate)->GetHeapProfilerState();

  if (state->insideCallback) {
    // Reentrant call detected, try to increase heap limit a bit so that
    // previous callback can proceed
    const uint32_t default_heap_extension_size = 10 * 1024 * 1024;
    auto extension_size = state->heap_extension_size
                              ? state->heap_extension_size
                              : default_heap_extension_size;
    return current_heap_limit + extension_size;
  }
  state->insideCallback = true;
  defer {
    state->insideCallback = false;
  };

  ++state->current_heap_extension_count;
  fprintf(stderr,
          "NearHeapLimit(count=%d): current_heap_limit=%zu, "
          "initial_heap_limit=%zu\n",
          state->current_heap_extension_count,
          current_heap_limit,
          initial_heap_limit);

  auto n = isolate->NumberOfTrackedHeapObjectTypes();
  v8::HeapObjectStatistics stats;

  for (size_t i = 0; i < n; ++i) {
    if (isolate->GetHeapObjectStatisticsAtLastGC(&stats, i) &&
        stats.object_count() > 0) {
      fprintf(stderr,
              "HeapObjectStats: type=%s, subtype=%s, size=%zu, count=%zu\n",
              stats.object_type(),
              stats.object_sub_type(),
              stats.object_size(),
              stats.object_count());
    }
  }
  std::unique_ptr<v8::AllocationProfile> profile{
      isolate->GetHeapProfiler()->GetAllocationProfile()};
  state->profile = TranslateAllocationProfileToCpp(profile->GetRootNode());
  if (state->dumpProfileOnStderr) {
    dumpAllocationProfile(stderr, state->profile.get());
  }

  if (!state->export_command.empty()) {
    ExportProfile(*state);
  }

  if (!state->callback.IsEmpty()) {
    if (state->callbackMode & kInterruptCallback) {
      isolate->RequestInterrupt(InterruptCallback, nullptr);
    }
    if (state->callbackMode & kAsyncCallback) {
      uv_async_send(state->async);
    }
  } else {
    state->profile.reset();
  }

  if (!state->isMainThread) {
    // In worker thread, OOM is not fatal to the whole process and will only
    // terminate the worker.
    // This is done by a callback registered by node, that's why we remove our
    // callback and then call LowMemoryNotification() here to trigger another
    // garbage collection, which will eventually call the callback registered by
    // node.
    state->UninstallNearHeapLimitCallback();
    isolate->LowMemoryNotification();
    // use the same value as node plus 1
    constexpr size_t kExtraHeapAllowance = 16 * 1024 * 1024;
    return current_heap_limit + kExtraHeapAllowance + 1;
  }

  size_t new_heap_limit =
      current_heap_limit +
      ((state->current_heap_extension_count <= state->max_heap_extension_count)
           ? state->heap_extension_size
           : 0);
  if (state->current_heap_extension_count >= state->max_heap_extension_count) {
    // On Node 14, NearLimitCallback is sometimes called many times, without the
    // process aborting, even when returned limit is not increased. Disable
    // callback until next call to GetAllocationProfile()
    state->UninstallNearHeapLimitCallback();
  }
  return new_heap_limit;
}

NAN_METHOD(HeapProfiler::StartSamplingHeapProfiler) {
  if (info.Length() == 2) {
    if (!info[0]->IsUint32()) {
      return Nan::ThrowTypeError("First argument type must be uint32.");
    }
    if (!info[1]->IsNumber()) {
      return Nan::ThrowTypeError("First argument type must be Integer.");
    }

    uint64_t sample_interval = info[0].As<v8::Integer>()->Value();
    int stack_depth = info[1].As<v8::Integer>()->Value();

    info.GetIsolate()->GetHeapProfiler()->StartSamplingHeapProfiler(
        sample_interval, stack_depth);
  } else {
    info.GetIsolate()->GetHeapProfiler()->StartSamplingHeapProfiler();
  }
}

// Signature:
// stopSamplingHeapProfiler()
NAN_METHOD(HeapProfiler::StopSamplingHeapProfiler) {
  auto isolate = info.GetIsolate();
  isolate->GetHeapProfiler()->StopSamplingHeapProfiler();
  PerIsolateData::For(isolate)->GetHeapProfilerState().reset();
}

// Signature:
// getAllocationProfile(): AllocationProfileNode
NAN_METHOD(HeapProfiler::GetAllocationProfile) {
  auto isolate = info.GetIsolate();
  std::unique_ptr<v8::AllocationProfile> profile(
      isolate->GetHeapProfiler()->GetAllocationProfile());
  v8::AllocationProfile::Node* root = profile->GetRootNode();
  auto state = PerIsolateData::For(isolate)->GetHeapProfilerState();
  if (state) {
    state->OnNewProfile();
  }
  info.GetReturnValue().Set(TranslateAllocationProfile(root));
}

NAN_METHOD(HeapProfiler::MonitorOutOfMemory) {
  if (info.Length() != 7) {
    return Nan::ThrowTypeError("MonitorOOMCondition must have 7 arguments.");
  }
  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError("Heap limit extension size must be a uint32.");
  }
  if (!info[1]->IsUint32()) {
    return Nan::ThrowTypeError(
        "Max heap limit extension count must be a uint32.");
  }
  if (!info[2]->IsBoolean()) {
    return Nan::ThrowTypeError("DumpHeapProfileOnStdErr must be a boolean.");
  }
  if (!info[3]->IsArray()) {
    return Nan::ThrowTypeError("Export command must be a string array.");
  }
  if (!info[4]->IsNullOrUndefined() && !info[4]->IsFunction()) {
    return Nan::ThrowTypeError("Callback name must be a function.");
  }
  if (!info[5]->IsUint32()) {
    return Nan::ThrowTypeError("CallbackMode must be a uint32.");
  }
  if (!info[6]->IsBoolean()) {
    return Nan::ThrowTypeError("IsMainThread must be a boolean.");
  }

  auto isolate = v8::Isolate::GetCurrent();

  auto& state = PerIsolateData::For(isolate)->GetHeapProfilerState();
  state = std::make_shared<HeapProfilerState>(isolate);

  state->heap_extension_size = info[0].As<v8::Integer>()->Value();
  state->max_heap_extension_count = info[1].As<v8::Integer>()->Value();
  state->dumpProfileOnStderr = info[2].As<v8::Boolean>()->Value();
  state->callbackMode = info[5].As<v8::Integer>()->Value();
  state->isMainThread = info[6].As<v8::Boolean>()->Value();
  state->InstallNearHeapLimitCallback();
  if (!info[4]->IsNullOrUndefined() && state->callbackMode != kNoCallback) {
    state->callback.Reset(Nan::To<v8::Function>(info[4]).ToLocalChecked());
  }

  auto commands = info[3].As<v8::Array>();
  for (uint32_t i = 0; i < commands->Length(); ++i) {
    auto value = Nan::Get(commands, i).ToLocalChecked();
    if (value->IsString()) {
      Nan::Utf8String arg{value};
      state->export_command.emplace_back(*arg, arg.length());
    }
  }

  if (!state->callback.IsEmpty() && (state->callbackMode & kAsyncCallback)) {
    state->RegisterAsyncCallback();
  }
}

NAN_MODULE_INIT(HeapProfiler::Init) {
  v8::Local<v8::Object> heapProfiler = Nan::New<v8::Object>();
  Nan::SetMethod(
      heapProfiler, "startSamplingHeapProfiler", StartSamplingHeapProfiler);
  Nan::SetMethod(
      heapProfiler, "stopSamplingHeapProfiler", StopSamplingHeapProfiler);
  Nan::SetMethod(heapProfiler, "getAllocationProfile", GetAllocationProfile);
  Nan::SetMethod(heapProfiler, "monitorOutOfMemory", MonitorOutOfMemory);
  Nan::Set(target,
           Nan::New<v8::String>("heapProfiler").ToLocalChecked(),
           heapProfiler);
}

void InterruptCallback(v8::Isolate* isolate, void* data) {
  v8::HandleScope scope(isolate);
  auto state = PerIsolateData::For(isolate)->GetHeapProfilerState();
  if (!state->profile) {
    return;
  }
  v8::Local<v8::Value> argv[1] = {
      dd::TranslateAllocationProfile(state->profile.get())};
  Nan::AsyncResource resource("NearHeapLimit");
  state->callback.Call(1, argv, &resource);
}

void AsyncCallback(uv_async_t* handle) {
  InterruptCallback(v8::Isolate::GetCurrent(), nullptr);
}

}  // namespace dd
