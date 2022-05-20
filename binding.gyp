{
  "variables": {
    "asan%": 0,
    "lsan%": 0,
    "ubsan%": 0,
  },
  "conditions": [
    ["OS == 'mac'", {
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "10.10",
        'CLANG_CXX_LIBRARY': 'libc++',
        "OTHER_CFLAGS": [
          "-std=c++14",
          "-stdlib=libc++",
          "-Wall",
          "-Werror",
          "-Wno-deprecated-declarations",
        ]
      },
    }],
    ["OS == 'linux'", {
      "link_settings": {
        "libraries": ["-lrt"]
      },
      "cflags": [
        "-std=c++14",
        "-Wall",
        "-Werror"
      ],
      "cflags_cc": [
        "-Wno-cast-function-type",
        # TODO: Remove when nan is updated to support v18 properly
        "-Wno-deprecated-declarations",
      ]
    }],
    ["OS == 'win'", {
      "cflags": [
        "/WX"
      ]
    }],
    # No macOS support for -fsanitize=leak
    ["lsan == 'true' and OS != 'mac'", {
      "cflags+": ["-fsanitize=leak"],
      "ldflags": ["-fsanitize=leak"],
    }],
    ["asan == 'true' and OS != 'mac'", {
      "cflags+": [
        "-fno-omit-frame-pointer",
        "-fsanitize=address",
        "-fsanitize-address-use-after-scope",
      ],
      "cflags!": [ "-fomit-frame-pointer" ],
      "ldflags": [ "-fsanitize=address" ],
    }],
    ["asan == 'true' and OS == 'mac'", {
      "xcode_settings+": {
        "OTHER_CFLAGS+": [
          "-fno-omit-frame-pointer",
          "-gline-tables-only",
          "-fsanitize=address",
        ],
        "OTHER_CFLAGS!": [
          "-fomit-frame-pointer",
        ],
        "OTHER_LDFLAGS": [
          "-fsanitize=address",
        ],
      },
    }],
    # UBSAN
    ["ubsan == 'true' and OS != 'mac'", {
      "cflags+": [
        "-fsanitize=undefined,alignment,bounds",
        "-fno-sanitize-recover",
      ],
      "ldflags": [
        "-fsanitize=undefined,alignment,bounds"
      ],
    }],
    ["ubsan == 'true' and OS == 'mac'", {
      "xcode_settings+": {
        "OTHER_CFLAGS+": [
          "-fsanitize=undefined,alignment,bounds",
          "-fno-sanitize-recover",
        ],
        "OTHER_LDFLAGS": [
          "-fsanitize=undefined,alignment,bounds"
        ],
      },
    }],
  ],
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
        "bindings/binding.cc",
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
      ],
    },
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
        "bindings/test/binding.cc",
        "bindings/test/profilers/cpu.test.cc",
        "bindings/test/code-event-record.test.cc",
        "bindings/test/code-map.test.cc",
        "bindings/test/cpu-time.test.cc",
        "bindings/test/location.test.cc",
        "bindings/test/sample.test.cc",
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
      ],
    },
  ]
}
