#include <sstream>

#include "sample.test.hh"
#include "../sample.hh"

#include "../location.hh"

void test_locations(Tap& t, v8::MaybeLocal<v8::Value> maybe_locations,
                    std::vector<uintptr_t> frames,
                    std::shared_ptr<dd::CodeMap> map) {
  size_t n = frames.size();
  t.plan(n + 3);

  v8::Local<v8::Value> locations_value;
  t.ok(
    maybe_locations.ToLocal(&locations_value),
    "location set should not be empty"
  );
  t.ok(
    locations_value->IsArray(),
    "location set should be an array"
  );
  v8::Local<v8::Array> locations = locations_value.As<v8::Array>();
  t.equal(
    (uint32_t) n,
    locations->Length(),
    "length should match the number of frames"
  );

  for (size_t i = 0; i < n; i++) {
    std::ostringstream name("location #", std::ios_base::ate);
    name << i;

    auto record = map->Lookup(frames[n - i - 1]);
    auto location = Nan::Get(locations, i)
      .ToLocalChecked()
      .As<v8::Object>();

    auto wrap = Nan::ObjectWrap::Unwrap<dd::Location>(location);

    t.equal(record, wrap->GetCodeEventRecord(), name.str());
  }
}

void test_sample_to_object(Tap& t, v8::MaybeLocal<v8::Value> maybe_sample,
                           std::vector<uintptr_t> frames,
                           std::shared_ptr<dd::CodeMap> map,
                           v8::Local<v8::Value> labels,
                           uint64_t cpu_time) {
  t.plan(4);
  auto isolate = v8::Isolate::GetCurrent();
  auto context = isolate->GetCurrentContext();

  auto propIs = [=](v8::Local<v8::Object> object, std::string name,
                    v8::Local<v8::Value> value) -> bool {
    auto key = Nan::New(name).ToLocalChecked();
    auto prop = object->Get(context, key).ToLocalChecked();
    return prop->Equals(context, value).ToChecked();
  };

  v8::Local<v8::Value> sample_value;
  t.ok(
    maybe_sample.ToLocal(&sample_value),
    "should unwrap sample object"
  );
  v8::Local<v8::Object> sample_object = sample_value.As<v8::Object>();
  t.ok(
    propIs(sample_object, "labels", labels),
    "should have expected labels"
  );
  t.ok(
    propIs(sample_object, "cpuTime", Nan::New<v8::Number>(cpu_time)),
    "should have expected cpuTime"
  );

  auto locations = sample_object->Get(context,
    Nan::New("locations").ToLocalChecked());

  t.test("sample.locations", [=](Tap& t) {
    test_locations(t, locations, frames, map);
  });
}

void test_sample(Tap& t) {
  t.plan(5);

  auto isolate = v8::Isolate::GetCurrent();
  auto context = isolate->GetCurrentContext();

  auto labels = v8::Number::New(isolate, 9876);
  auto label_wrap = std::make_shared<dd::LabelWrap>(labels);
  std::vector<uintptr_t> frames = {1234, 2345};
  uint64_t cpu_time = 12345;

  dd::Sample* sample = new dd::Sample(isolate, label_wrap, frames, cpu_time);

  t.ok(sample->GetLabels(isolate)->Equals(context, labels).ToChecked(),
      "sample->Labels() should return supplied labels");
  t.equal(sample->GetFrames(), frames,
      "sample->GetFrames() should return supplied frames");

  // Before symbolization, Locations() and ToObject() should be empty
  t.equal(0u, sample->GetLocations(isolate)->Length(),
      "location set should be empty before symbolizing");

  // Do symbolization
  auto recordA = std::make_shared<dd::CodeEventRecord>(
    isolate, 1234, 0, 5678, 1, 2, "fnA");
  auto recordB = std::make_shared<dd::CodeEventRecord>(
    isolate, 2345, 0, 5678, 3, 4, "fnB");

  auto map = dd::CodeMap::For(isolate);
  map->Clear();
  map->Add(1234, recordA);
  map->Add(2345, recordB);

  sample->Symbolize(map);

  // After symbolization, Locations() should return a location array
  t.test("sample->GetLocations()", [=](Tap& t) {
    test_locations(t, sample->GetLocations(isolate), frames, map);
  });

  // After symbolization, ToObject() should return a valid sample object
  t.test("sample->ToObject()", [=](Tap& t) {
    test_sample_to_object(t, sample->ToObject(isolate), frames, map, labels, cpu_time);
  });
}
