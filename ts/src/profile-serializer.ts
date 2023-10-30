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

import {
  Function,
  Label,
  LabelInput,
  Line,
  Location,
  Profile,
  Sample,
  ValueType,
  StringTable,
  ProfileInput,
} from 'pprof-format';
import {
  GeneratedLocation,
  SourceLocation,
  SourceMapper,
} from './sourcemapper/sourcemapper';
import {
  AllocationProfileNode,
  LabelSet,
  ProfileNode,
  TimeProfile,
  TimeProfileNode,
  TimeProfileNodeContext,
} from './v8-types';

/**
 * A stack of function IDs.
 */
type Stack = number[];

/**
 * A function which converts entry into one or more samples, then
 * appends those sample(s) to samples.
 */
type AppendEntryToSamples<T extends ProfileNode> = (
  entry: Entry<T>,
  samples: Sample[]
) => void;

/**
 * Profile node and stack trace to that node.
 */
interface Entry<T extends ProfileNode> {
  node: T;
  stack: Stack;
}

function isGeneratedLocation(
  location: SourceLocation
): location is GeneratedLocation {
  return (
    location.column !== undefined &&
    location.line !== undefined &&
    location.line > 0
  );
}

/**
 * Takes v8 profile and populates sample, location, and function fields of
 * profile.proto.
 *
 * @param profile - profile.proto with empty sample, location, and function
 * fields.
 * @param root - root of v8 profile tree describing samples to be appended
 * to profile.
 * @param appendToSamples - function which converts entry to sample(s)  and
 * appends these to end of an array of samples.
 * @param stringTable - string table for the existing profile.
 */
function serialize<T extends ProfileNode>(
  profile: ProfileInput,
  root: T,
  appendToSamples: AppendEntryToSamples<T>,
  stringTable: StringTable,
  ignoreSamplesPath?: string,
  sourceMapper?: SourceMapper
) {
  const samples: Sample[] = [];
  const locations: Location[] = [];
  const functions: Function[] = [];
  const functionIdMap = new Map<string, number>();
  const locationIdMap = new Map<string, number>();

  const entries: Array<Entry<T>> = (root.children as T[]).map((n: T) => ({
    node: n,
    stack: [],
  }));
  while (entries.length > 0) {
    const entry = entries.pop()!;
    const node = entry.node;
    if (ignoreSamplesPath && node.scriptName.indexOf(ignoreSamplesPath) > -1) {
      continue;
    }
    if (node.name === '(idle)' || node.name === '(program)') continue;
    const stack = entry.stack;
    const location = getLocation(node, sourceMapper);
    stack.unshift(location.id as number);
    appendToSamples(entry, samples);
    for (const child of node.children as T[]) {
      entries.push({node: child, stack: stack.slice()});
    }
  }

  profile.sample = samples;
  profile.location = locations;
  profile.function = functions;
  profile.stringTable = stringTable;

  function getLocation(
    node: ProfileNode,
    sourceMapper?: SourceMapper
  ): Location {
    let profLoc: SourceLocation = {
      file: node.scriptName || '',
      line: node.lineNumber,
      column: node.columnNumber,
      name: node.name,
    };

    if (profLoc.line) {
      if (sourceMapper && isGeneratedLocation(profLoc)) {
        profLoc = sourceMapper.mappingInfo(profLoc);
      }
    }
    const keyStr = `${node.scriptId}:${profLoc.line}:${profLoc.column}:${profLoc.name}`;
    let id = locationIdMap.get(keyStr);
    if (id !== undefined) {
      // id is index+1, since 0 is not valid id.
      return locations[id - 1];
    }
    id = locations.length + 1;
    locationIdMap.set(keyStr, id);
    const line = getLine(profLoc, node.scriptId);
    const location = new Location({id, line: [line]});
    locations.push(location);
    return location;
  }

  function getLine(loc: SourceLocation, scriptId?: number): Line {
    return new Line({
      functionId: getFunction(loc, scriptId).id,
      line: loc.line,
    });
  }

  function getFunction(loc: SourceLocation, scriptId?: number): Function {
    const keyStr = `${scriptId}:${loc.name}`;
    let id = functionIdMap.get(keyStr);
    if (id !== undefined) {
      // id is index+1, since 0 is not valid id.
      return functions[id - 1];
    }
    id = functions.length + 1;
    functionIdMap.set(keyStr, id);
    let name = loc.name;
    if (!name) {
      if (loc.line) {
        if (loc.column) {
          name = `(anonymous@L${loc.line}:C${loc.column})`;
        } else {
          name = `(anonymous@L${loc.line})`;
        }
      } else {
        name = '(anonymous)';
      }
    }
    const nameId = stringTable.dedup(name);
    const f = new Function({
      id,
      name: nameId,
      systemName: nameId,
      filename: stringTable.dedup(loc.file || ''),
    });
    functions.push(f);
    return f;
  }
}

/**
 * @return value type for sample counts (type:sample, units:count), and
 * adds strings used in this value type to the table.
 */
function createSampleCountValueType(table: StringTable): ValueType {
  return new ValueType({
    type: table.dedup('sample'),
    unit: table.dedup('count'),
  });
}

/**
 * @return value type for time samples (type:wall, units:nanoseconds), and
 * adds strings used in this value type to the table.
 */
function createTimeValueType(table: StringTable): ValueType {
  return new ValueType({
    type: table.dedup('wall'),
    unit: table.dedup('nanoseconds'),
  });
}

/**
 * @return value type for object counts (type:objects, units:count), and
 * adds strings used in this value type to the table.
 */
function createObjectCountValueType(table: StringTable): ValueType {
  return new ValueType({
    type: table.dedup('objects'),
    unit: table.dedup('count'),
  });
}

/**
 * @return value type for memory allocations (type:space, units:bytes), and
 * adds strings used in this value type to the table.
 */
function createAllocationValueType(table: StringTable): ValueType {
  return new ValueType({
    type: table.dedup('space'),
    unit: table.dedup('bytes'),
  });
}

function computeTotalHitCount(root: TimeProfileNode): number {
  return (
    root.hitCount +
    (root.children as TimeProfileNode[]).reduce(
      (sum, node) => sum + computeTotalHitCount(node),
      0
    )
  );
}

/**
 * Converts v8 time profile into into a profile proto.
 * (https://github.com/google/pprof/blob/master/proto/profile.proto)
 *
 * @param prof - profile to be converted.
 * @param intervalMicros - average time (microseconds) between samples.
 */
export function serializeTimeProfile(
  prof: TimeProfile,
  intervalMicros: number,
  sourceMapper?: SourceMapper,
  recomputeSamplingInterval = false,
  generateLabels?: (context: TimeProfileNodeContext) => LabelSet
): Profile {
  // If requested, recompute sampling interval from profile duration and total number of hits,
  // since profile duration should be #hits x interval.
  // Recomputing an average interval is more accurate, since in practice intervals between
  // samples are larger than the requested sampling interval (eg. 12.5ms vs 10ms requested).
  // For very short durations, computation becomes meaningless (eg. if there is only one hit),
  // therefore keep intervalMicros as a lower bound and 2 * intervalMicros as upper bound.
  if (recomputeSamplingInterval) {
    const totalHitCount = computeTotalHitCount(prof.topDownRoot);
    if (totalHitCount > 0) {
      intervalMicros = Math.min(
        Math.max(
          Math.floor((prof.endTime - prof.startTime) / totalHitCount),
          intervalMicros
        ),
        2 * intervalMicros
      );
    }
  }
  const intervalNanos = intervalMicros * 1000;
  const appendTimeEntryToSamples: AppendEntryToSamples<TimeProfileNode> = (
    entry: Entry<TimeProfileNode>,
    samples: Sample[]
  ) => {
    let unlabelledHits = entry.node.hitCount;
    for (const context of entry.node.contexts || []) {
      const labels = generateLabels ? generateLabels(context) : context.context;
      if (Object.keys(labels).length > 0) {
        const sample = new Sample({
          locationId: entry.stack,
          value: [1, intervalNanos],
          label: buildLabels(labels, stringTable),
        });
        samples.push(sample);
        unlabelledHits--;
      }
    }
    if (unlabelledHits > 0) {
      const sample = new Sample({
        locationId: entry.stack,
        value: [unlabelledHits, unlabelledHits * intervalNanos],
      });
      samples.push(sample);
    }
  };

  const stringTable = new StringTable();
  const sampleValueType = createSampleCountValueType(stringTable);
  const timeValueType = createTimeValueType(stringTable);

  const profile = {
    sampleType: [sampleValueType, timeValueType],
    timeNanos: Date.now() * 1000 * 1000,
    durationNanos: (prof.endTime - prof.startTime) * 1000,
    periodType: timeValueType,
    period: intervalNanos,
  };

  serialize(
    profile,
    prof.topDownRoot,
    appendTimeEntryToSamples,
    stringTable,
    undefined,
    sourceMapper
  );

  return new Profile(profile);
}

function buildLabels(labelSet: object, stringTable: StringTable): Label[] {
  const labels: Label[] = [];

  for (const [key, value] of Object.entries(labelSet)) {
    const labelInput: LabelInput = {
      key: stringTable.dedup(key),
    };
    switch (typeof value) {
      case 'string':
        labelInput.str = stringTable.dedup(value);
        break;
      case 'number':
      case 'bigint':
        labelInput.num = value;
        break;
      default:
        continue;
    }
    labels.push(new Label(labelInput));
  }

  return labels;
}

/**
 * Converts v8 heap profile into into a profile proto.
 * (https://github.com/google/pprof/blob/master/proto/profile.proto)
 *
 * @param prof - profile to be converted.
 * @param startTimeNanos - start time of profile, in nanoseconds (POSIX time).
 * @param durationsNanos - duration of the profile (wall clock time) in
 * nanoseconds.
 * @param intervalBytes - bytes allocated between samples.
 */
export function serializeHeapProfile(
  prof: AllocationProfileNode,
  startTimeNanos: number,
  intervalBytes: number,
  ignoreSamplesPath?: string,
  sourceMapper?: SourceMapper
): Profile {
  const appendHeapEntryToSamples: AppendEntryToSamples<
    AllocationProfileNode
  > = (entry: Entry<AllocationProfileNode>, samples: Sample[]) => {
    if (entry.node.allocations.length > 0) {
      for (const alloc of entry.node.allocations) {
        const sample = new Sample({
          locationId: entry.stack,
          value: [alloc.count, alloc.sizeBytes * alloc.count],
          // TODO: add tag for allocation size
        });
        samples.push(sample);
      }
    }
  };

  const stringTable = new StringTable();
  const sampleValueType = createObjectCountValueType(stringTable);
  const allocationValueType = createAllocationValueType(stringTable);

  const profile = {
    sampleType: [sampleValueType, allocationValueType],
    timeNanos: startTimeNanos,
    periodType: allocationValueType,
    period: intervalBytes,
  };

  serialize(
    profile,
    prof,
    appendHeapEntryToSamples,
    stringTable,
    ignoreSamplesPath,
    sourceMapper
  );

  return new Profile(profile);
}
