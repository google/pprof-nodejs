#!/bin/bash

# Trap all errors.
trap "echo '** AT LEAST ONE OF TESTS FAILED **'" ERR

# Fail on any error, show commands run.
set -eox pipefail

. $(dirname $0)/../tools/retry.sh

cd $(dirname $0)

# The list of tested versions below should be in sync with node's
# official releases. https://nodejs.org/en/about/releases/
if [[ -z "$BINARY_HOST" ]]; then
  ADDITIONAL_PACKAGES="python3 g++ make"
  NODE_VERSIONS=(14 16 18 19)
else
  # Tested versions for pre-built binaries are limited based on
  # what node-pre-gyp can specify as its target version.
  NODE_VERSIONS=(14 16)
fi

for i in ${NODE_VERSIONS[@]}; do
  # Test Linux support for the given node version.
  retry docker build -f Dockerfile.linux --build-arg NODE_VERSION=$i \
      --build-arg ADDITIONAL_PACKAGES="$ADDITIONAL_PACKAGES" \
      -t node$i-linux .

  docker run  -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" node$i-linux \
      /src/system-test/test.sh

  docker run  -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" \
      -e VERIFY_TIME_LINE_NUMBERS="true" node$i-linux \
      /src/system-test/test.sh

  # Test Alpine support for the given node version.
  retry docker build -f Dockerfile.node-alpine \
      --build-arg ADDITIONAL_PACKAGES="$ADDITIONAL_PACKAGES" \
      --build-arg NODE_VERSION=$i -t node$i-alpine .

  docker run -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" node$i-alpine \
      /src/system-test/test.sh
done

echo '** ALL TESTS PASSED **'
