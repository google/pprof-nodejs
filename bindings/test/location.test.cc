#include "location.test.hh"
#include "../location.hh"

void test_location(Tap& t) {
  t.plan(9);

  auto isolate = v8::Isolate::GetCurrent();

  auto record = std::make_shared<dd::CodeEventRecord>(
      isolate, 1234, 0, 5678, 1, 2, "a", "b", "c");
  record->SetScriptId(123);

  auto obj = dd::Location::New(isolate, record)->handle();

  // Type helpers
  auto Get = [isolate](v8::Local<v8::Object> obj, std::string key)
    -> v8::Local<v8::Value> {
    auto context = isolate->GetCurrentContext();
    return obj->Get(context, Nan::New(key).ToLocalChecked()).ToLocalChecked();
  };

  auto Str = [isolate](v8::Local<v8::Value> val) -> std::string {
    return *v8::String::Utf8Value(isolate, val);
  };

  auto Int = [](v8::Local<v8::Value> val) -> int64_t {
    return val.As<v8::Integer>()->Value();
  };

  t.equal(123, Int(Get(obj, "scriptId")), "script id");
  t.equal(1234, Int(Get(obj, "address")), "address");
  t.equal(0, Int(Get(obj, "previousAddress")), "previous address");
  t.equal(5678, Int(Get(obj, "size")), "size");
  t.equal(1, Int(Get(obj, "line")), "line");
  t.equal(2, Int(Get(obj, "column")), "column");
  t.equal("a", Str(Get(obj, "comment")), "comment");
  t.equal("b", Str(Get(obj, "functionName")), "function name");
  t.equal("c", Str(Get(obj, "scriptName")), "script name");
}
