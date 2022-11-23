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

import {serializeCpuProfile} from './profile-serializer';
import {CpuProfiler as NativeCpuProfiler} from './cpu-profiler-bindings';
import {
  CodeEvent,
  CpuProfile,
  CpuProfileNode,
  InitialCpuProfile,
} from './v8-types';

function isNodeEqual(a: CpuProfileNode, b: CpuProfileNode) {
  if (a.name !== b.name) return false;
  if (a.scriptName !== b.scriptName) return false;
  if (a.scriptId !== b.scriptId) return false;
  if (a.lineNumber !== b.lineNumber) return false;
  if (a.columnNumber !== b.columnNumber) return false;
  return true;
}

function makeNode(location: CodeEvent): CpuProfileNode {
  return {
    name: location.comment || location.functionName,
    scriptName: location.scriptName || '',
    scriptId: location.scriptId,
    lineNumber: location.line,
    columnNumber: location.column,
    hitCount: 0,
    cpuTime: 0,
    labelSets: [],
    children: [],
  };
}

export default class CpuProfiler extends NativeCpuProfiler {
  profile() {
    if (this.frequency === 0) return;

    const profile: InitialCpuProfile = super.profile();

    const timeProfile: CpuProfile = {
      startTime: profile.startTime,
      endTime: profile.endTime,
      topDownRoot: {
        name: '(root)',
        scriptName: '',
        scriptId: 0,
        lineNumber: 0,
        columnNumber: 0,
        hitCount: 0,
        cpuTime: 0,
        labelSets: [],
        children: [],
      },
    };

    let targetNode = timeProfile.topDownRoot;

    for (const sample of profile.samples) {
      if (!sample) continue;
      locations: for (const location of sample.locations) {
        const node = makeNode(location);

        for (const found of targetNode.children) {
          const foundNode = found as CpuProfileNode;
          if (isNodeEqual(node, foundNode)) {
            targetNode = foundNode;
            continue locations;
          }
        }

        targetNode.children.push(node);
        targetNode = node;
      }

      targetNode.cpuTime += sample.cpuTime;
      targetNode.hitCount++;
      if (sample.labels) {
        targetNode.labelSets.push(sample.labels);
      }

      targetNode = timeProfile.topDownRoot;
    }

    const intervalMicros = 1000000 / (this.frequency as number);
    return serializeCpuProfile(timeProfile, intervalMicros);
  }
}
