/**
 * Copyright 2017 Google Inc. All Rights Reserved.
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

import delay from 'delay';
import type {perftools} from '../../proto/profile';

import {serializeTimeProfile} from './profile-serializer';
import {SourceMapper} from './sourcemapper/sourcemapper';
import {
  setSamplingInterval,
  startProfiling,
  stopProfiling,
} from './time-profiler-inspector';

let profiling = false;

const DEFAULT_INTERVAL_MICROS: Microseconds = 1000;

type Microseconds = number;
type Milliseconds = number;

export interface TimeProfilerOptions {
  /** time in milliseconds for which to collect profile. */
  durationMillis: Milliseconds;
  /** average time in microseconds between samples */
  intervalMicros?: Microseconds;
  sourceMapper?: SourceMapper;
}

export async function profile(
  options: TimeProfilerOptions
): Promise<perftools.profiles.IProfile> {
  const stop = await start(
    options.intervalMicros || DEFAULT_INTERVAL_MICROS,
    options.sourceMapper
  );
  await delay(options.durationMillis);
  return await stop();
}

export async function start(
  intervalMicros: Microseconds = DEFAULT_INTERVAL_MICROS,
  sourceMapper?: SourceMapper
): Promise<() => Promise<perftools.profiles.IProfile>> {
  if (profiling) {
    throw new Error('already profiling');
  }

  profiling = true;
  await setSamplingInterval(intervalMicros);
  // Node.js contains an undocumented API for reporting idle status to V8.
  // This lets the profiler distinguish idle time from time spent in native
  // code. Ideally this should be default behavior. Until then, use the
  // undocumented API.
  // See https://github.com/nodejs/node/issues/19009#issuecomment-403161559.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  (process as any)._startProfilerIdleNotifier();
  await startProfiling();
  return async function stop() {
    profiling = false;
    const result = await stopProfiling();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (process as any)._stopProfilerIdleNotifier();
    const profile = serializeTimeProfile(result, intervalMicros, sourceMapper);
    return profile;
  };
}
