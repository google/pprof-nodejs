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
import {writeFileSync} from 'fs';

import * as heapProfiler from './heap-profiler';
import {encodeSync} from './profile-encoder';
import * as timeProfiler from './time-profiler';
export {AllocationProfileNode, TimeProfileNode, ProfileNode} from './v8-types';

export {encode, encodeSync} from './profile-encoder';
export {SourceMapper} from './sourcemapper/sourcemapper';
export {setLogger} from './logger';

export const time = {
  profile: timeProfiler.profile,
  start: timeProfiler.start,
  stop: timeProfiler.stop,
  setContext: timeProfiler.setContext,
  isStarted: timeProfiler.isStarted,
};

export const heap = {
  start: heapProfiler.start,
  stop: heapProfiler.stop,
  profile: heapProfiler.profile,
  convertProfile: heapProfiler.convertProfile,
  v8Profile: heapProfiler.v8Profile,
  monitorOutOfMemory: heapProfiler.monitorOutOfMemory,
  CallbackMode: heapProfiler.CallbackMode,
};

// If loaded with --require, start profiling.
if (module.parent && module.parent.id === 'internal/preload') {
  time.start({});
  process.on('exit', () => {
    // The process is going to terminate imminently. All work here needs to
    // be synchronous.
    const profile = time.stop();
    const buffer = encodeSync(profile);
    writeFileSync(`pprof-profile-${process.pid}.pb.gz`, buffer);
  });
}
