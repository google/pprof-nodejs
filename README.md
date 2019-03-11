# pprof support for Node.js

[![NPM Version][npm-image]][npm-url]
[![Build Status][circle-image]][circle-url]
[![Known Vulnerabilities][snyk-image]][snyk-url]

**WIP** [pprof][pprof-url] support for Node.js.

# Running the system test
The system test starts a simple benchmark, uses this module to collect a time
and a heap profile, and then verifies that the profiles can be opened and that
the profiles contain functions from within the benchmark. 

To run the system test, [golang](https://golang.org/) must be installed.

The following command can be used to run the system test with all supported
versions of Node.JS:
```console
$ sh system-test/system_test.sh
```

To run the system test with the v8 canary build, use:
```console
$ RUN_ONLY_V8_CANARY_TEST=true sh system-test/system_test.sh
```

[circle-image]: https://circleci.com/gh/google/pprof-nodejs.svg?style=svg
[circle-url]: https://circleci.com/gh/google/pprof-nodejs
[coveralls-image]: https://coveralls.io/repos/google/pprof-nodejs/badge.svg?branch=master&service=github
[npm-image]: https://badge.fury.io/js/pprof.svg
[npm-url]: https://npmjs.org/package/pprof
[pprof-url]: https://github.com/google/pprof
[snyk-image]: https://snyk.io/test/github/google/pprof-nodejs/badge.svg
[snyk-url]: https://snyk.io/test/github/google/pprof-nodejs
