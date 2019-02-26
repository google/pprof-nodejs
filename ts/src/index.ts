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
import * as heapProfiler from './heap-profiler';
import * as timeProfiler from './time-profiler';
import { serializeHeapProfile, serializeTimeProfile } from './profile-serializer';

export {SourceMapper} from './sourcemapper/sourcemapper';

export const time = {
  profile: timeProfiler.profile,
  start: timeProfiler.start,
  serialize: serializeTimeProfile
};

export const heap = {
  start: heapProfiler.start,
  stop: heapProfiler.stop,
  profile: heapProfiler.profile,
  serialize: serializeHeapProfile
};
