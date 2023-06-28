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
                            "bindings/profilers/heap.cc",
                            "bindings/profilers/wall.cc",
                            "bindings/per-isolate-data.cc",
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
                            "bindings/profilers/heap.cc",
                            "bindings/profilers/wall.cc",
                            "bindings/per-isolate-data.cc",
                            "bindings/translate-time-profile.cc",
                            "bindings/test/binding.cc",
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