#!/bin/bash

SRCDIR="/cloned_src"

trap "cd $SRCDIR && npm run clean" EXIT
trap "echo '** TEST FAILED **'" ERR

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
cp -r /src "$SRCDIR"
cd "$SRCDIR"
. "tools/retry.sh"

NODEDIR=$(dirname $(dirname $(which node)))

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

retry npm_install typescript gts @types/node >/dev/null
retry npm_install --nodedir="$NODEDIR" \
    $([ -z "$BINARY_HOST" ] && echo "--build-from-source=pprof" \
        || echo "--pprof_binary_host_mirror=$BINARY_HOST")\
    "$PROFILER">/dev/null

if [[ "$VERIFY_TIME_LINE_NUMBERS" != "true" ]]; then
  npm run compile
fi

NODE_VERSION=$(node -v | cut -d. -f1 | tr -d 'v')
node -v
node --trace-warnings "$BENCHPATH" 10 $VERIFY_TIME_LINE_NUMBERS

if [[ "$VERIFY_TIME_LINE_NUMBERS" == "true" ]]; then
  output=$(pprof -lines -top -nodecount=2 time.pb.gz | tee $tty)

  # Due to V8 changes in Node 21, the line numbers are different.
  # It also emits "anonymous" and "idle" statuses in the output.
  # E.G: 1877ms 74.93% 74.93%     1878ms 74.97%  (anonymous) file:/tmp/tmp.xyz/busybench/src/busybench.js:34
  if [ "$NODE_VERSION" -ge 21 ]; then
    grep "anonymous.*busybench.js:3[0-9]" <<< "$output"
  else
    grep "busyLoop.*src/busybench.js:[23][0-9]" <<< "$output"
  fi

  heap_output=$(pprof -filefunctions -top -nodecount=2 heap.pb.gz | tee $tty)
  grep "busyLoop.*src/busybench.js" <<< "$heap_output"
else
  output=$(pprof -filefunctions -top -nodecount=2 time.pb.gz | tee $tty)
  if [ "$NODE_VERSION" -ge 21 ]; then
    grep "anonymous.*busybench.ts" <<< "$output"
  else
    grep "busyLoop.*src/busybench.ts" <<< "$output"
  fi
  pprof -filefunctions -top -nodecount=2 heap.pb.gz | tee $tty | \
      grep "busyLoop.*src/busybench.ts"
fi


echo '** TEST PASSED **'
