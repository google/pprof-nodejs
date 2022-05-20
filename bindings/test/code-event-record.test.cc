#include <unordered_map>
#include <sstream>

#include "code-event-record.test.hh"
#include "../code-event-record.hh"

void test_code_event_record(Tap& t) {
  t.plan(19);

  auto isolate = v8::Isolate::GetCurrent();

  auto record = new dd::CodeEventRecord(
      isolate, 1234, 0, 5678, 1, 2, "a", "b", "c");
  record->SetScriptId(123);

  // Type helpers
  auto Str = [isolate](v8::Local<v8::Value> val) -> std::string {
    return *v8::String::Utf8Value(isolate, val);
  };

  auto Int = [](v8::Local<v8::Value> val) -> int64_t {
    return val.As<v8::Integer>()->Value();
  };

  t.equal(123, Int(record->GetScriptId(isolate)), "script id");
  t.equal(1234, Int(record->GetAddress(isolate)), "address");
  t.equal(0, Int(record->GetPreviousAddress(isolate)), "previous address");
  t.equal(5678, Int(record->GetSize(isolate)), "size");
  t.equal(1, Int(record->GetLine(isolate)), "line");
  t.equal(2, Int(record->GetColumn(isolate)), "column");
  t.equal("a", Str(record->GetComment(isolate)), "comment");
  t.equal("b", Str(record->GetFunctionName(isolate)), "function name");
  t.equal("c", Str(record->GetScriptName(isolate)), "script name");

  auto same = new dd::CodeEventRecord(
      isolate, 1234, 0, 5678, 1, 2, "a", "b", "c");
  same->SetScriptId(123);
  t.ok(record->Equal(same), "should be equal to itself");

  using TestPair = std::pair<dd::CodeEventRecord*, dd::CodeEventRecord*>;
  std::unordered_map<std::string, TestPair> non_matching = {
    {"id",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a")}},
    {"address",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 2, 1, 1, 1, 1, "a", "a", "a")}},
    {"previousAddress",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 2, 1, 1, 1, "a", "a", "a")}},
    {"size",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 2, 1, 1, "a", "a", "a")}},
    {"line",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 2, 1, "a", "a", "a")}},
    {"column",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 2, "a", "a", "a")}},
    {"comment",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "b", "a", "a")}},
    {"functionName",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "b", "a")}},
    {"scriptName",
      {new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "a"),
        new dd::CodeEventRecord(isolate, 1, 1, 1, 1, 1, "a", "a", "b")}}
  };

  // Script Id is not a constructor argument
  non_matching["id"].second->SetScriptId(123);

  for (const auto& pair : non_matching) {
    auto name = pair.first;
    auto test = pair.second;
    std::ostringstream s;
    s << "should not have equal " << name;
    t.not_ok(test.first->Equal(test.second), s.str());
  }
}
