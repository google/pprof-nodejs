#!/bin/bash

# Copyright 2018 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

. $(dirname $0)/../retry.sh

# Fail on any error.
set -e pipefail

# Display commands
set -x

if [[ -z "$BUILD_TYPE" ]]; then
  case $KOKORO_JOB_TYPE in
    CONTINUOUS_INTEGRATION)
      BUILD_TYPE=continuous
      ;;
    PRESUBMIT_GITHUB)
      BUILD_TYPE=presubmit
      ;;
    RELEASE)
      BUILD_TYPE=release
      ;;
    *)
      echo "Unknown build type: ${KOKORO_JOB_TYPE}"
      exit 1
      ;;
  esac
fi

cd $(dirname $0)/../..
BASE_DIR=$PWD

retry docker build -t build-linux -f tools/build/Dockerfile.linux tools/build
retry docker run -v "${BASE_DIR}":"${BASE_DIR}" build-linux \
    "${BASE_DIR}/tools/build/build.sh"

retry docker build -t build-alpine -f tools/build/Dockerfile.alpine tools/build
retry docker run -v "${BASE_DIR}":"${BASE_DIR}" build-alpine \
    "${BASE_DIR}/tools/build/build.sh"

GCS_LOCATION="cprof-e2e-nodejs-artifacts/pprof-nodejs/kokoro/${BUILD_TYPE}/${KOKORO_BUILD_NUMBER}"
retry gcloud auth activate-service-account  \
    --key-file="${KOKORO_KEYSTORE_DIR}/72935_cloud-profiler-e2e-service-account-key"

retry gsutil cp -r "${BASE_DIR}/artifacts/." "gs://${GCS_LOCATION}/"

# Test the agent
export BINARY_HOST="https://storage.googleapis.com/${GCS_LOCATION}"
"${BASE_DIR}/system-test/system_test.sh"

if [ "$BUILD_TYPE" == "release" ]; then
  retry gsutil cp -r "${BASE_DIR}/artifacts/." "gs://cloud-profiler/pprof-nodejs/release"
fi
