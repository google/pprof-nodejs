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

import {setTimeout} from 'timers/promises';

import {Profile} from 'pprof-format';
import {
  serializeTimeProfile,
  GARBAGE_COLLECTION_FUNCTION_NAME,
  NON_JS_THREADS_FUNCTION_NAME,
} from './profile-serializer';
import {SourceMapper} from './sourcemapper/sourcemapper';
import {
  TimeProfiler,
  getNativeThreadId,
  constants as profilerConstants,
} from './time-profiler-bindings';
import {
  GenerateTimeLabelsFunction,
  TimeProfile,
  TimeProfilerMetrics,
} from './v8-types';
import {isMainThread} from 'worker_threads';
import {AsyncLocalStorage} from 'async_hooks';
const {kSampleCount} = profilerConstants;

const DEFAULT_INTERVAL_MICROS: Microseconds = 1000;
const DEFAULT_DURATION_MILLIS: Milliseconds = 60000;

type Microseconds = number;
type Milliseconds = number;

type NativeTimeProfiler = InstanceType<typeof TimeProfiler> & {
  stopAndCollect?: <T>(
    restart: boolean,
    callback: (profile: TimeProfile) => T,
  ) => T;
};

let gProfiler: NativeTimeProfiler | undefined;
let gStore: AsyncLocalStorage<unknown> | undefined;
let gSourceMapper: SourceMapper | undefined;
let gIntervalMicros: Microseconds;
let gV8ProfilerStuckEventLoopDetected = 0;

function handleStopRestart() {
  if (!gProfiler) {
    return;
  }
  gV8ProfilerStuckEventLoopDetected =
    gProfiler.v8ProfilerStuckEventLoopDetected();
  // Workaround for v8 bug, where profiler event processor thread is stuck in
  // a loop eating 100% CPU, leading to empty profiles.
  // Fully stop and restart the profiler to reset the profile to a valid state.
  if (gV8ProfilerStuckEventLoopDetected > 0) {
    gProfiler.stop(false);
    gProfiler.start();
  }
}

function handleStopNoRestart() {
  gV8ProfilerStuckEventLoopDetected = 0;
  gProfiler?.dispose();
  gProfiler = undefined;
  gSourceMapper = undefined;
  if (gStore !== undefined) {
    gStore.disable();
    gStore = undefined;
  }
}

/** Make sure to stop profiler before node shuts down, otherwise profiling
 * signal might cause a crash if it occurs during shutdown */
process.once('exit', () => {
  if (isStarted()) stop();
});

export interface TimeProfilerOptions {
  /** time in milliseconds for which to collect profile. */
  durationMillis?: Milliseconds;
  /** average time in microseconds between samples */
  intervalMicros?: Microseconds;
  sourceMapper?: SourceMapper;

  /**
   * This configuration option is experimental.
   * When set to true, functions will be aggregated at the line level, rather
   * than at the function level.
   * This defaults to false.
   */
  lineNumbers?: boolean;
  withContexts?: boolean;
  workaroundV8Bug?: boolean;
  collectCpuTime?: boolean;
  collectAsyncId?: boolean;
  useCPED?: boolean;
}

const DEFAULT_OPTIONS: TimeProfilerOptions = {
  durationMillis: DEFAULT_DURATION_MILLIS,
  intervalMicros: DEFAULT_INTERVAL_MICROS,
  lineNumbers: false,
  withContexts: false,
  workaroundV8Bug: true,
  collectCpuTime: false,
  collectAsyncId: false,
  useCPED: false,
};

export async function profile(
  options: TimeProfilerOptions = {},
): Promise<Profile> {
  options = {...DEFAULT_OPTIONS, ...options};
  start(options);
  await setTimeout(options.durationMillis!);
  return stop();
}

export async function profileV2(options: TimeProfilerOptions = {}) {
  options = {...DEFAULT_OPTIONS, ...options};
  start(options);
  await setTimeout(options.durationMillis!);
  return stopV2();
}

// Temporarily retained for backwards compatibility with older tracer
export function start(options: TimeProfilerOptions = {}) {
  options = {...DEFAULT_OPTIONS, ...options};
  if (gProfiler) {
    throw new Error('Wall profiler is already started');
  }

  const store = options.useCPED === true ? new AsyncLocalStorage() : undefined;
  gProfiler = new TimeProfiler({...options, CPEDKey: store, isMainThread});
  gSourceMapper = options.sourceMapper;
  gIntervalMicros = options.intervalMicros!;
  gV8ProfilerStuckEventLoopDetected = 0;

  gProfiler.start();
  gStore = store;

  // If contexts are enabled without using CPED, set an initial empty context
  if (options.withContexts && !options.useCPED) {
    setContext({});
  }
}

export function stop(
  restart = false,
  generateLabels?: GenerateTimeLabelsFunction,
  lowCardinalityLabels?: string[],
): Profile {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }

  const profile = gProfiler.stop(restart);
  if (restart) {
    handleStopRestart();
  } else {
    handleStopNoRestart();
  }

  const serializedProfile = serializeTimeProfile(
    profile,
    gIntervalMicros,
    gSourceMapper,
    true,
    generateLabels,
    lowCardinalityLabels,
  );
  return serializedProfile;
}

/**
 * Same as stop() but uses the lazy callback path: serialization happens inside
 * a native callback while the V8 profile is still alive.
 * This reduces memory overhead.
 */
export function stopV2(
  restart = false,
  generateLabels?: GenerateTimeLabelsFunction,
  lowCardinalityLabels?: string[],
) {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }

  const serializedProfile = gProfiler.stopAndCollect(
    restart,
    (profile: TimeProfile) =>
      serializeTimeProfile(
        profile,
        gIntervalMicros,
        gSourceMapper,
        true,
        generateLabels,
        lowCardinalityLabels,
      ),
  );
  if (restart) {
    handleStopRestart();
  } else {
    handleStopNoRestart();
  }
  return serializedProfile;
}

export function getState() {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }
  return gProfiler.state;
}

export function setContext(context?: object) {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }
  gProfiler.context = context;
}

export function runWithContext<R, TArgs extends unknown[]>(
  context: object,
  f: (...args: TArgs) => R,
  ...args: TArgs
): R {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  } else if (!gStore) {
    throw new Error('Can only use runWithContext with AsyncContextFrame');
  }
  return gStore.run(gProfiler.createContextHolder(context), f, ...args);
}

export function getContext() {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }
  return gProfiler.context;
}

export function getMetrics(): TimeProfilerMetrics {
  if (!gProfiler) {
    throw new Error('Wall profiler is not started');
  }
  return gProfiler.metrics as TimeProfilerMetrics;
}

export function isStarted() {
  return !!gProfiler;
}

// Return 0 if no issue detected, 1 if possible issue, 2 if issue detected for certain
export function v8ProfilerStuckEventLoopDetected() {
  return gV8ProfilerStuckEventLoopDetected;
}

export const constants = {
  kSampleCount,
  GARBAGE_COLLECTION_FUNCTION_NAME,
  NON_JS_THREADS_FUNCTION_NAME,
};
export {getNativeThreadId};
