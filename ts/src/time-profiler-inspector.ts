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

import {TimeProfile, TimeProfileNode} from './v8-types';
import * as inspector from 'node:inspector';

const session = new inspector.Session();
session.connect();

// Wrappers around inspector functions
export function startProfiling(): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    session.post('Profiler.enable', err => {
      if (err !== null) {
        reject(err);
        return;
      }
      session.post('Profiler.start', err => {
        if (err !== null) {
          reject(err);
          return;
        }
        resolve();
      });
    });
  });
}

export function stopProfiling(): Promise<TimeProfile> {
  // return profiler.timeProfiler.stopProfiling(runName, includeLineInfo || false);
  return new Promise<TimeProfile>((resolve, reject) => {
    session.post('Profiler.stop', (err, {profile}) => {
      if (err !== null) {
        reject(err);
        return;
      }
      resolve(translateToTimeProfile(profile));
    });
  });
}

function translateToTimeProfile(
  profile: inspector.Profiler.Profile
): TimeProfile {
  const root: inspector.Profiler.ProfileNode | undefined = profile.nodes[0];
  // Not sure if this could ever happen...
  if (root === undefined) {
    return {
      endTime: profile.endTime,
      startTime: profile.startTime,
      topDownRoot: {
        children: [],
        hitCount: 0,
        scriptName: '',
      },
    };
  }

  const nodesById: {[key: number]: inspector.Profiler.ProfileNode} = {};
  profile.nodes.forEach(node => (nodesById[node.id] = node));

  function translateNode({
    hitCount,
    children,
    callFrame: {columnNumber, functionName, lineNumber, scriptId, url},
  }: inspector.Profiler.ProfileNode): TimeProfileNode {
    const parsedScriptId = parseInt(scriptId);
    return {
      name: functionName,
      scriptName: url,

      // Add 1 because these are zero-based
      columnNumber: columnNumber + 1,
      lineNumber: lineNumber + 1,

      hitCount: hitCount ?? 0,
      scriptId: Number.isNaN(parsedScriptId) ? 0 : parsedScriptId,
      children:
        children?.map(childId => translateNode(nodesById[childId])) ?? [],
    };
  }

  return {
    endTime: profile.endTime,
    startTime: profile.startTime,
    topDownRoot: translateNode(root),
  };
}

export function setSamplingInterval(intervalMicros: number): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    session.post(
      'Profiler.setSamplingInterval',
      {interval: intervalMicros},
      err => {
        if (err !== null) {
          reject(err);
          return;
        }
        resolve();
      }
    );
  });
}
