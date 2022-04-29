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

# Fail on any error.
set -e pipefail

# Display commands
set -x

cd $(dirname $0)/../..
BASE_DIR=$PWD

ARTIFACTS_OUT="${BASE_DIR}/artifacts"
mkdir -p "$ARTIFACTS_OUT"

npm install --quiet

for version in 12.0.0 14.0.0 16.0.0 17.0.1
do
  ./node_modules/.bin/node-pre-gyp configure rebuild package \
      --target=$version --target_arch="x64"
  cp -r build/stage/* "${ARTIFACTS_OUT}/"
  rm -rf build
done

# Remove node_modules directory. When this script is run in a docker container
# with  user root, then a system test running after this script cannot run npm
# install.
rm -r node_modules
