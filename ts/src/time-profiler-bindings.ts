/**
 * Copyright 2018 Google Inc. All Rights Reserved.
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
import * as path from 'path';
import {TimeProfile} from './v8-types';

const binary = require('node-pre-gyp');
const bindingPath =
    binary.find(path.resolve(path.join(__dirname, '../../package.json')));
const profiler = require(bindingPath);


// Wrappers around native time profiler functions.
export function startProfiling(runName: string, includeLineInfo?: boolean) {
  profiler.timeProfiler.startProfiling(runName, includeLineInfo || false);
}

export function stopProfiling(
    runName: string, includeLineInfo?: boolean): TimeProfile {
  return profiler.timeProfiler.stopProfiling(runName, includeLineInfo || false);
}

export function setSamplingInterval(intervalMicros: number) {
  profiler.timeProfiler.setSamplingInterval(intervalMicros);
}
