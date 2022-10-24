/**
 * Copyright 2019 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {promisify} from 'util';
import {gzip, gzipSync} from 'zlib';

import {perftools} from '../../proto/profile';

const gzipPromise = promisify(gzip);

export async function encode(
  profile: perftools.profiles.IProfile
): Promise<Buffer> {
  const buffer = perftools.profiles.Profile.encode(profile).finish();
  return gzipPromise(buffer);
}

export function encodeSync(profile: perftools.profiles.IProfile): Buffer {
  const buffer = perftools.profiles.Profile.encode(profile).finish();
  return gzipSync(buffer);
}
