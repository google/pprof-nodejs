#include "cpu-time.test.hh"
#include "../cpu-time.hh"

void test_cpu_time(Tap& t) {
  t.plan(3);

  dd::CpuTime cpu_time({
    2, // tv_sec
    1, // tv_nsec
  });

  int64_t diff = cpu_time.Diff({
    4,  // tv_sec
    3, // tv_nsec
  });

  t.equal(diff, 2000000002, "should compute time diff correctly");

  struct timespec now = cpu_time.Now();
  t.ok(
    now.tv_nsec > 0 || now.tv_sec > 0,
    "should get the current cpu time"
  );

  struct timespec now2 = cpu_time.Now();
  t.ok(
    now2.tv_nsec >= now.tv_nsec && now2.tv_sec >= now.tv_sec,
    "should have current time after previous check"
  );
}
