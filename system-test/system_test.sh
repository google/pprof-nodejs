#!/bin/bash

# Trap all errors.
trap "echo '** AT LEAST ONE OF TESTS FAILED **'" ERR

# Fail on any error, show commands run.
set -eox pipefail

. $(dirname $0)/../tools/retry.sh

cd $(dirname $0)

if [[ -z "$BINARY_HOST" ]]; then
  ADDITIONAL_PACKAGES="python3 g++ make"
  NODE_VERSIONS=(12 14 16 17 node)
else
  NODE_VERSIONS=(12 14 16 17)
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
  retry docker build -f Dockerfile.node$i-alpine \
      --build-arg ADDITIONAL_PACKAGES="$ADDITIONAL_PACKAGES" -t node$i-alpine .

  docker run -v $PWD/..:/src -e BINARY_HOST="$BINARY_HOST" node$i-alpine \
      /src/system-test/test.sh
done

echo '** ALL TESTS PASSED **'
