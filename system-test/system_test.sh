#!/bin/bash

# Trap all errors.
trap "echo '** AT LEAST ONE OF TESTS FAILED **'" ERR

# Fail on any error, show commands run.
set -eox pipefail

retry() {
  "${@}" || "${@}" || "${@}" || exit $?
}

cd $(dirname $0)

for i in 6 8 10 11; do
  # Test Linux support for the given node version.
  retry docker build -f Dockerfile.linux --build-arg NODE_VERSION=$i -t node$i-linux .
  docker run -v $PWD/..:/src node$i-linux /src/system-test/test.sh
  # Test Alpine support for the given node version.
  docker build -f Dockerfile.node$i-alpine -t node$i-alpine .
  docker run -v $PWD/..:/src node$i-alpine /src/system-test/test.sh
done

echo '** ALL TESTS PASSED **'
