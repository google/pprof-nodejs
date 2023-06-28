{
    "variables": {
        "asan%": 0,
        "lsan%": 0,
        "ubsan%": 0,
        "build_tests%": 0
    },
    "conditions": [
        [
            "build_tests != 'true'",
            {
                "targets": [
                    {
                        "target_name": "dd_pprof",
                        "sources": [
                            "bindings/profilers/cpu.cc",
                            "bindings/profilers/heap.cc",
                            "bindings/profilers/wall.cc",
                            "bindings/code-event-record.cc",
                            "bindings/code-map.cc",
                            "bindings/cpu-time.cc",
                            "bindings/location.cc",
                            "bindings/per-isolate-data.cc",
                            "bindings/sample.cc",
                            "bindings/translate-time-profile.cc",
                            "bindings/binding.cc"
                        ],
                        "include_dirs": [
                            "bindings",
                            "<!(node -e \"require('nan')\")"
                        ]
                    }
                ]
            }
        ],
        [
            "build_tests == 'true'",
            {
                "targets": [
                    {
                        "target_name": "test_dd_pprof",
                        "sources": [
                            "bindings/profilers/cpu.cc",
                            "bindings/profilers/heap.cc",
                            "bindings/profilers/wall.cc",
                            "bindings/code-event-record.cc",
                            "bindings/code-map.cc",
                            "bindings/cpu-time.cc",
                            "bindings/location.cc",
                            "bindings/per-isolate-data.cc",
                            "bindings/sample.cc",
                            "bindings/translate-time-profile.cc",
                            "bindings/test/binding.cc",
                            "bindings/test/profilers/cpu.test.cc",
                            "bindings/test/code-event-record.test.cc",
                            "bindings/test/code-map.test.cc",
                            "bindings/test/cpu-time.test.cc",
                            "bindings/test/location.test.cc",
                            "bindings/test/sample.test.cc"
                        ],
                        "include_dirs": [
                            "bindings",
                            "<!(node -e \"require('nan')\")"
                        ]
                    }
                ]
            }
        ]
    ]
}