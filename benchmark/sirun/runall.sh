#!/usr/bin/env bash

set -euo pipefail

if [ -n "${MAJOR_NODE_VERSION:-}" ]; then
    if test -f ~/.nvm/nvm.sh; then
        source ~/.nvm/nvm.sh
    else
        source "${NVM_DIR:-usr/local/nvm}/nvm.sh"
    fi

    nvm use "${MAJOR_NODE_VERSION}"

    pushd ../../
    npm install
    popd
fi

VERSION=$(node -v)
echo "using Node.js ${VERSION}"

for d in *; do
    if [ -d "${d}" ]; then
        pushd "$d"
        time node ../run-all-variants.js >> ../results.ndjson
        popd
    fi
done

if [ "${DEBUG_RESULTS:-false}" == "true" ]; then
  echo "Benchmark Results:"
  cat ./results.ndjson
fi

echo "all tests for ${VERSION} have now completed."
