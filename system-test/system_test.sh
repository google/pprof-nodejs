#!/bin/bash

retry() {
  for i in {1..3}; do
    "${@}" && return 0
  done
  return 1
}

# Fail on any error.
set -eo pipefail

# Display commands being run.
set -x

# Record directory of pprof-nodejs.
cd $(dirname $0)/..
BASE_DIR=$(pwd)

RUN_ONLY_V8_CANARY_TEST="${RUN_ONLY_V8_CANARY_TEST:-false}"
echo "$RUN_ONLY_V8_CANARY_TEST"

RUN_SYSTEM_TEST_ON="${RUN_SYSTEM_TEST_ON:-local}"
echo "$RUN_SYSTEM_TEST_ON"

# Move system test to separate directory to run.
TESTDIR="$BASE_DIR/run-system-test"
cp -R "system-test" "$TESTDIR"

# Note docker API version for system test.
export DOCKER_API_VERSION=$(docker version -f '{{.Client.APIVersion}}')

docker pull node:10
docker pull node:10-alpine


# Run test.
cd "$TESTDIR"
retry go get -t -d .
go test -v -timeout=10m -run TestAgentIntegration -pprof_nodejs_path="$BASE_DIR" -run_only_v8_canary_test="$RUN_ONLY_V8_CANARY_TEST" -binary_host="$BINARY_HOST" -run_on="$RUN_SYSTEM_TEST_ON"
