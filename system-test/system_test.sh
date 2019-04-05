#!/bin/bash

# Trap all errors.
trap "echo '** AT LEAST ONE OF TESTS FAILED **'" ERR

# Fail on any error, show commands run.
set -eox pipefail

retry() {
  "${@}" || "${@}" || "${@}" || exit $?
}

cd $(dirname $0)

if [[ -z "$BINARY_HOST" ]]; then
  ADDITIONAL_PACKAGES="python g++ make"
fi

if [[ "$RUN_ONLY_V8_CANARY_TEST" == "true" ]]; then
  NVM_NODEJS_ORG_MIRROR="https://nodejs.org/download/v8-canary"
  NODE_VERSIONS=(node)
else
  NODE_VERSIONS=(6 8 10 11)
fi

for i in ${NODE_VERSIONS[@]}; do
  # Test Linux support for the given node version.
  retry docker build -f Dockerfile.linux --build-arg NODE_VERSION=$i \
      --build-arg ADDITIONAL_PACKAGES="$ADDITIONAL_PACKAGES" \
      --build-arg  NVM_NODEJS_ORG_MIRROR="$NVM_NODEJS_ORG_MIRROR" \
      -t node$i-linux .

  docker run  -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" node$i-linux \
      /src/system-test/test.sh

  # Skip running on alpine if NVM_NODEJS_ORG_MIRROR is specified.
  if [[ -z "$NVM_NODEJS_ORG_MIRROR" ]]; then
    continue
  fi

  # Test Alpine support for the given node version.
  retry docker build -f Dockerfile.node$i-alpine \
      --build-arg ADDITIONAL_PACKAGES="$ADDITIONAL_PACKAGES" -t node$i-alpine .

  docker run -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" node$i-alpine \
      /src/system-test/test.sh
done

echo '** ALL TESTS PASSED **'
