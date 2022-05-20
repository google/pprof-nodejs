#include "code-map.test.hh"
#include "../code-map.hh"

void test_code_map(Tap& t) {
  t.plan(11);

  auto isolate = v8::Isolate::GetCurrent();

  // Lookup in an empty map should return nullptr
  {
    dd::CodeMap map(isolate);
    t.equal(map.Lookup(1234), nullptr, "should not find record in empty map");
  }

  auto record = std::make_shared<dd::CodeEventRecord>(
    isolate, 1234, 0, 5678, 1, 2, "fn");

  // Lookup with record at matching address should return record
  {
    dd::CodeMap map(isolate, {
      { 1234, record }
    });

    t.ok(record->Equal(map.Lookup(1234).get()), "should find record by exact address");
  }

  // Lookup with address in size range of matching record should return record
  {
    dd::CodeMap map(isolate, {
      { 1234, record }
    });

    t.ok(record->Equal(map.Lookup(2000).get()), "should find record in size range");
  }

  // Lookup with address outside size range should return nullptr
  {
    dd::CodeMap map(isolate, {
      { 1234, record }
    });

    t.equal(map.Lookup(1000), nullptr, "should not find record below size range");
    t.equal(map.Lookup(9001), nullptr, "should not find record above size range");
  }

  // Add a new record
  {
    dd::CodeMap map(isolate);
    map.Add(1234, record);

    t.ok(record->Equal(map.Lookup(1234).get()), "should find record after added");
  }

  // Remove an existing record
  {
    dd::CodeMap map(isolate, {
      { 1234, record }
    });
    map.Remove(1234);

    t.ok(!map.Lookup(1234), "should not find record after removal");
  }

  {
    dd::CodeMap map(isolate);
    t.equal((int)map.Entries().size(), 0, "should be empty before enabling");

    map.Enable();
    t.ok((int)map.Entries().size() > 0, "should not be empty after enabled");

    map.Disable();
    t.equal((int)map.Entries().size(), 0, "should be empty after disabling");

    map.Enable();
    t.ok((int)map.Entries().size() > 0, "should refill if enabled again");
  }
}
