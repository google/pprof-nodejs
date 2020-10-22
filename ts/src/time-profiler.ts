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

import {serializeTimeProfile} from './profile-serializer';
import {SourceMapper} from './sourcemapper/sourcemapper';
import {
  setSamplingInterval,
  startProfiling,
  stopProfiling,
} from './time-profiler-bindings';

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
  name?: string;

  /**
   * This configuration option is experimental.
   * When set to true, functions will be aggregated at the line level, rather
   * than at the function level.
   * This defaults to false.
   */
  lineNumbers?: boolean;
}

export async function profile(options: TimeProfilerOptions) {
  const stop = start(
    options.intervalMicros || DEFAULT_INTERVAL_MICROS,
    options.name,
    options.sourceMapper,
    options.lineNumbers
  );
  await delay(options.durationMillis);
  return stop();
}

export function start(
  intervalMicros: Microseconds = DEFAULT_INTERVAL_MICROS,
  name?: string,
  sourceMapper?: SourceMapper,
  lineNumbers?: boolean
) {
  if (profiling) {
    throw new Error('already profiling');
  }

  profiling = true;
  const runName = name || `pprof-${Date.now()}-${Math.random()}`;
  setSamplingInterval(intervalMicros);
  // Node.js contains an undocumented API for reporting idle status to V8.
  // This lets the profiler distinguish idle time from time spent in native
  // code. Ideally this should be default behavior. Until then, use the
  // undocumented API.
  // See https://github.com/nodejs/node/issues/19009#issuecomment-403161559.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  (process as any)._startProfilerIdleNotifier();
  startProfiling(runName, lineNumbers);
  return function stop() {
    profiling = false;
    const result = stopProfiling(runName, lineNumbers);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (process as any)._stopProfilerIdleNotifier();
    const profile = serializeTimeProfile(result, intervalMicros, sourceMapper);
    return profile;
  };
}
