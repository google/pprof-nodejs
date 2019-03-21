#!/bin/bash

trap "echo '** TEST FAILED **'" ERR

retry() {
  "${@}" || "${@}" || "${@}" || return 1
}

set -eox pipefail
cd $(dirname $0)/..

NODEDIR=$(dirname $(dirname $(which node)))

# TODO: Remove when a new version of nan (current version 2.12.1) is released.
# For v8-canary tests, we need to use the version of NAN on github, which
# contains unreleased fixes that allow the native component to be compiled
# with Node's V8 canary build.
[ -z $NVM_NODEJS_ORG_MIRROR ] || retry npm install https://github.com/nodejs/nan.git

retry npm install --nodedir="$NODEDIR" >/dev/null

npm run compile
npm pack >/dev/null
VERSION=$(node -e "console.log(require('./package.json').version);")
PROFILER="$PWD/pprof-$VERSION.tgz"

TESTDIR=$(mktemp -d)
cp -r "$PWD/system-test/busybench" "$TESTDIR"
cd "$TESTDIR/busybench"

retry npm install pify @types/pify typescript gts @types/node >/dev/null
retry npm install --nodedir="$NODEDIR" "$PROFILER" >/dev/null

npm run compile >/dev/null

node -v
node --trace-warnings build/src/busybench.js 10
ls -l

pprof -filefunctions -top -nodecount=2 time.pb.gz | grep "busyLoop.*build/src/busybench.js"
pprof -filefunctions -top -nodecount=2 heap.pb.gz | grep "busyLoop.*build/src/busybench.js"
echo '** TEST PASSED **'
