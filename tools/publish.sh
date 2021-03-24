#!/bin/bash

# Copyright 2018 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

. $(dirname $0)/retry.sh

set -eo pipefail

# Install desired version of Node.js
retry curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.37.2/install.sh | bash >/dev/null
export NVM_DIR="$HOME/.nvm" >/dev/null
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" >/dev/null

retry nvm install 10 &>/dev/null

cd $(dirname $0)/..

NPM_TOKEN=$(cat $KOKORO_KEYSTORE_DIR/72935_pprof-npm-token)
echo "//wombat-dressing-room.appspot.com/:_authToken=${NPM_TOKEN}" > ~/.npmrc

retry npm install --quiet
npm publish --access=public \
    --registry=https://wombat-dressing-room.appspot.com
