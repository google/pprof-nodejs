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
import {TimeProfiler} from './time-profiler-bindings';

const DEFAULT_INTERVAL_MICROS: Microseconds = 1000;

const majorVersion = process.version.slice(1).split('.').map(Number)[0];

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

function ensureRunName(name?: string) {
  return name || `pprof-${Date.now()}-${Math.random()}`;
}

// NOTE: refreshing doesn't work if giving a profile name.
export function start(
  intervalMicros: Microseconds = DEFAULT_INTERVAL_MICROS,
  name?: string,
  sourceMapper?: SourceMapper,
  lineNumbers = true
) {
  const profiler = new TimeProfiler(intervalMicros);
  let runName = start();
  return majorVersion < 16 ? stopOld : stop;

  function start() {
    const runName = ensureRunName(name);
    profiler.start(runName, lineNumbers);
    return runName;
  }

  // Node.js versions prior to v16 leak memory if not disposed and recreated
  // between each profile. As disposing deletes current profile data too,
  // we must stop then dispose then start.
  function stopOld(restart = false) {
    const result = profiler.stop(runName, lineNumbers);
    profiler.dispose();
    if (restart) {
      runName = start();
    }
    return serializeTimeProfile(result, intervalMicros, sourceMapper);
  }

  // For Node.js v16+, we want to start the next profile before we stop the
  // current one as otherwise the active profile count could reach zero which
  // means V8 might tear down the symbolizer thread and need to start it again.
  function stop(restart = false) {
    let nextRunName;
    if (restart) {
      nextRunName = start();
    }
    const result = profiler.stop(runName, lineNumbers);
    if (nextRunName) {
      runName = nextRunName;
    }
    if (!restart) profiler.dispose();
    return serializeTimeProfile(result, intervalMicros, sourceMapper);
  }
}
