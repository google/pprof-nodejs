{
  "targets": [
    {
      "target_name": "dd-pprof",
      "sources": [ 
        "bindings/profiler.cc",
      ],
      "include_dirs": ["<!(node -e \"require('nan')\")"],
        "xcode_settings": {
          "MACOSX_DEPLOYMENT_TARGET": "10.10",
          "OTHER_CFLAGS": [
              "-std=c++14",
              "-stdlib=libc++",
              "-Wall",
              "-Werror"
          ]
      },
        "conditions": [
          ["OS == 'linux'", {
              "cflags": [
                  "-std=c++11",
                  "-Wall",
                  "-Werror"
              ],
              "cflags_cc": [
                  "-Wno-cast-function-type"
              ]
          }],
          ["OS == 'win'", {
              "cflags": [
                  "/WX"
              ]
          }]
      ]
    },
  ]
}
