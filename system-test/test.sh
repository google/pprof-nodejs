#!/bin/bash

trap "cd $(dirname $0)/.. && npm run clean" EXIT
trap "echo '** TEST FAILED **'" ERR

. $(dirname $0)/../tools/retry.sh

function timeout_after() {
  # timeout on Node 11 alpine image requires -t to specify time.
  if [[ -f /bin/busybox ]] &&  [[ $(node -v) =~ ^v11.* ]]; then
    timeout -t "${@}"
  else
    timeout "${@}"
  fi
}

npm_install() {
  timeout_after 60 npm install --quiet "${@}"
}

set -eox pipefail
cd $(dirname $0)/..

# Install supported Python version to build Node.js binaries with node-gyp.
sudo apt-get update && sudo apt-get install python3.6
alias python3=python3.6

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
npm pack --quiet
VERSION=$(node -e "console.log(require('./package.json').version);")
PROFILER="$PWD/pprof-$VERSION.tgz"

if [[ "$VERIFY_TIME_LINE_NUMBERS" == "true" ]]; then
  BENCHDIR="$PWD/system-test/busybench-js"
  BENCHPATH="src/busybench.js"
else
  BENCHDIR="$PWD/system-test/busybench"
  BENCHPATH="build/src/busybench.js"
fi

TESTDIR=$(mktemp -d)
cp -r "$BENCHDIR" "$TESTDIR/busybench"
cd "$TESTDIR/busybench"

retry npm_install pify @types/pify typescript gts @types/node >/dev/null
retry npm_install --nodedir="$NODEDIR" \
    $([ -z "$BINARY_HOST" ] && echo "--build-from-source=pprof" \
        || echo "--pprof_binary_host_mirror=$BINARY_HOST")\
    "$PROFILER">/dev/null

if [[ "$VERIFY_TIME_LINE_NUMBERS" != "true" ]]; then
  npm run compile
fi

node -v
node --trace-warnings "$BENCHPATH" 10 $VERIFY_TIME_LINE_NUMBERS

if [[ "$VERIFY_TIME_LINE_NUMBERS" == "true" ]]; then
  pprof -lines -top -nodecount=2 time.pb.gz
  pprof -lines -top -nodecount=2 time.pb.gz | \
      grep "busyLoop.*src/busybench.js:3[3-5]"
  pprof -filefunctions -top -nodecount=2 heap.pb.gz
  pprof -filefunctions -top -nodecount=2 heap.pb.gz | \
      grep "busyLoop.*src/busybench.js"
else
  pprof -filefunctions -top -nodecount=2 time.pb.gz
  pprof -filefunctions -top -nodecount=2 time.pb.gz | \
      grep "busyLoop.*src/busybench.ts"
  pprof -filefunctions -top -nodecount=2 heap.pb.gz
  pprof -filefunctions -top -nodecount=2 heap.pb.gz | \
      grep "busyLoop.*src/busybench.ts"
fi


echo '** TEST PASSED **'
