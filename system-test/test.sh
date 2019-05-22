#!/bin/bash

trap "echo '** TEST FAILED **'" ERR

retry() {
  "${@}" || "${@}" || "${@}" || return 1
}

function timeout_after() {
  if [ -f /bin/busybox ]; then
    timeout -t "${@}"
  else
    timeout "${@}"
  fi
}

npm_install() {
  timeout_after 60 npm install "${@}"
}

set -eox pipefail
cd $(dirname $0)/..

NODEDIR=$(dirname $(dirname $(which node)))

# TODO: Remove when a new version of nan (current version 2.12.1) is released.
# For v8-canary tests, we need to use the version of NAN on github, which
# contains unreleased fixes that allow the native component to be compiled
# with Node's V8 canary build.
[ -z $NVM_NODEJS_ORG_MIRROR ] \
    || retry npm_install https://github.com/nodejs/nan.git

retry npm_install --nodedir="$NODEDIR" \
    ${BINARY_HOST:+--pprof_binary_host_mirror=$BINARY_HOST} >/dev/null

npm run compile
npm pack >/dev/null
VERSION=$(node -e "console.log(require('./package.json').version);")
PROFILER="$PWD/pprof-$VERSION.tgz"

TESTDIR=$(mktemp -d)
cp -r "$PWD/system-test/busybench" "$TESTDIR"
cd "$TESTDIR/busybench"

retry npm_install pify @types/pify typescript gts @types/node >/dev/null
retry npm_install --nodedir="$NODEDIR" \
    ${BINARY_HOST:+--pprof_binary_host_mirror=$BINARY_HOST} \
    "$PROFILER">/dev/null

npm run compile >/dev/null

node -v
node --trace-warnings build/src/busybench.js 10
ls -l

pprof -filefunctions -top -nodecount=2 time.pb.gz | \
    grep "busyLoop.*src/busybench.ts"
pprof -filefunctions -top -nodecount=2 heap.pb.gz | \
    grep "busyLoop.*src/busybench.ts"
echo '** TEST PASSED **'
