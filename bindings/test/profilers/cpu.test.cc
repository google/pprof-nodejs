#include "cpu.test.hh"
#include "../../location.hh"
#include "../../profilers/cpu.hh"

void test_labels(Tap& t) {
  t.plan(2);

  auto context = Nan::GetCurrentContext();
  dd::CpuProfiler cpu;

  t.ok(cpu.GetLabels()->IsUndefined(), "should be undefined before setting");

  auto labels = Nan::New<v8::Number>(1);
  cpu.SetLabels(labels);

  t.ok(cpu.GetLabels()->Equals(context, labels).ToChecked(),
       "should match given labels value after setting");
}

void test_samples(Tap& t) {
  t.plan(9);

  auto isolate = v8::Isolate::GetCurrent();

  dd::CpuProfiler cpu;

  // Empty state
  t.equal(0U, cpu.GetSampleCount(), "no processed samples before capture");
  t.ok(!cpu.GetLastSample(), "no unprocessed sample after capture");

  // Set labels to verify they get attached to captured samples
  auto labels = Nan::New<v8::Number>(1);
  cpu.SetLabels(labels);
  cpu.CaptureSample(isolate);

  t.equal(0U, cpu.GetSampleCount(), "no processed samples after capture");
  t.ok(cpu.GetLastSample(), "has unprocessed sample after capture");
  t.equal(labels,
          cpu.GetLastSample()->GetLabels(isolate),
          "should have given labels on unprocessed sample after capture");

  // Make a synthetic sample to set as the "last sample"
  auto label_wrap = std::make_shared<dd::LabelWrap>(labels);
  std::vector<uintptr_t> frames1 = {1234};
  uint64_t cpu_time1 = 12345;

  std::unique_ptr<dd::Sample> sample1(
      new dd::Sample(isolate, label_wrap, frames1, cpu_time1));

  std::vector<uintptr_t> frames2 = {5678};
  uint64_t cpu_time2 = 56789;
  std::unique_ptr<dd::Sample> sample2(
      new dd::Sample(isolate, label_wrap, frames2, cpu_time2));

  auto record =
      std::make_shared<dd::CodeEventRecord>(1234, 0, 5678, 1, 2, "fnA");

  auto map = dd::CodeMap::For(isolate);
  map->Clear();
  map->Add(1234, record);

  cpu.SetLastSample(std::move(sample1));
  cpu.SetLastSample(std::move(sample2));
  cpu.ProcessSample();

  t.equal(
      2U, cpu.GetSampleCount(), "has processed sample after capture/process");

  auto samples = cpu.GetSamples();
  t.equal(2U,
          samples->Length(),
          "should have one processed sample in samples array");

  auto firstSample = Nan::Get(samples, 0).ToLocalChecked().As<v8::Object>();
  auto locations = Nan::Get(firstSample, Nan::New("locations").ToLocalChecked())
                       .ToLocalChecked()
                       .As<v8::Array>();
  t.equal(1U, locations->Length(), "should have one symbolized stack frame");

  auto location = Nan::ObjectWrap::Unwrap<dd::Location>(
      Nan::Get(locations, 0).ToLocalChecked().As<v8::Object>());
  t.equal(
      location->GetCodeEventRecord(),
      record,
      "symbolization of processed sample should match expected code record");
}

void test_profilers_cpu_profiler(Tap& t) {
  t.plan(2);

  t.test("labels", test_labels);
  t.test("samples", test_samples);
}
