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

import {perftools} from '../../proto/profile';
import {
  GeneratedLocation,
  SourceLocation,
  SourceMapper,
} from './sourcemapper/sourcemapper';
import {
  AllocationProfileNode,
  ProfileNode,
  TimeProfile,
  TimeProfileNode,
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
  samples: perftools.profiles.Sample[]
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
 * Used to build string table and access strings and their ids within the table
 * when serializing a profile.
 */
class StringTable {
  strings: string[];
  stringsMap: Map<string, number>;

  constructor() {
    this.strings = [];
    this.stringsMap = new Map<string, number>();
    this.getIndexOrAdd('');
  }

  /**
   * @return index of str within the table. Also adds str to string table if
   * str is not in the table already.
   */
  getIndexOrAdd(str: string): number {
    let idx = this.stringsMap.get(str);
    if (idx !== undefined) {
      return idx;
    }
    idx = this.strings.push(str) - 1;
    this.stringsMap.set(str, idx);
    return idx;
  }
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
  profile: perftools.profiles.IProfile,
  root: T,
  appendToSamples: AppendEntryToSamples<T>,
  stringTable: StringTable,
  ignoreSamplesPath?: string,
  sourceMapper?: SourceMapper
) {
  const samples: perftools.profiles.Sample[] = [];
  const locations: perftools.profiles.Location[] = [];
  const functions: perftools.profiles.Function[] = [];
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
  profile.stringTable = stringTable.strings;

  function getLocation(
    node: ProfileNode,
    sourceMapper?: SourceMapper
  ): perftools.profiles.Location {
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
    const line = getLine(
      node.scriptId,
      profLoc.file,
      profLoc.name,
      profLoc.line
    );
    const location = new perftools.profiles.Location({id, line: [line]});
    locations.push(location);
    return location;
  }

  function getLine(
    scriptId?: number,
    scriptName?: string,
    name?: string,
    line?: number
  ): perftools.profiles.Line {
    return new perftools.profiles.Line({
      functionId: getFunction(scriptId, scriptName, name).id,
      line,
    });
  }

  function getFunction(
    scriptId?: number,
    scriptName?: string,
    name?: string
  ): perftools.profiles.Function {
    const keyStr = `${scriptId}:${name}`;
    let id = functionIdMap.get(keyStr);
    if (id !== undefined) {
      // id is index+1, since 0 is not valid id.
      return functions[id - 1];
    }
    id = functions.length + 1;
    functionIdMap.set(keyStr, id);
    const nameId = stringTable.getIndexOrAdd(name || '(anonymous)');
    const f = new perftools.profiles.Function({
      id,
      name: nameId,
      systemName: nameId,
      filename: stringTable.getIndexOrAdd(scriptName || ''),
    });
    functions.push(f);
    return f;
  }
}

/**
 * @return value type for sample counts (type:sample, units:count), and
 * adds strings used in this value type to the table.
 */
function createSampleCountValueType(
  table: StringTable
): perftools.profiles.ValueType {
  return new perftools.profiles.ValueType({
    type: table.getIndexOrAdd('sample'),
    unit: table.getIndexOrAdd('count'),
  });
}

/**
 * @return value type for time samples (type:wall, units:microseconds), and
 * adds strings used in this value type to the table.
 */
function createTimeValueType(table: StringTable): perftools.profiles.ValueType {
  return new perftools.profiles.ValueType({
    type: table.getIndexOrAdd('wall'),
    unit: table.getIndexOrAdd('microseconds'),
  });
}

/**
 * @return value type for object counts (type:objects, units:count), and
 * adds strings used in this value type to the table.
 */
function createObjectCountValueType(
  table: StringTable
): perftools.profiles.ValueType {
  return new perftools.profiles.ValueType({
    type: table.getIndexOrAdd('objects'),
    unit: table.getIndexOrAdd('count'),
  });
}

/**
 * @return value type for memory allocations (type:space, units:bytes), and
 * adds strings used in this value type to the table.
 */
function createAllocationValueType(
  table: StringTable
): perftools.profiles.ValueType {
  return new perftools.profiles.ValueType({
    type: table.getIndexOrAdd('space'),
    unit: table.getIndexOrAdd('bytes'),
  });
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
  sourceMapper?: SourceMapper
): perftools.profiles.IProfile {
  const appendTimeEntryToSamples: AppendEntryToSamples<TimeProfileNode> = (
    entry: Entry<TimeProfileNode>,
    samples: perftools.profiles.Sample[]
  ) => {
    if (entry.node.hitCount > 0) {
      const sample = new perftools.profiles.Sample({
        locationId: entry.stack,
        value: [entry.node.hitCount, entry.node.hitCount * intervalMicros],
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
    period: intervalMicros,
  };

  serialize(
    profile,
    prof.topDownRoot,
    appendTimeEntryToSamples,
    stringTable,
    undefined,
    sourceMapper
  );

  return profile;
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
): perftools.profiles.IProfile {
  const appendHeapEntryToSamples: AppendEntryToSamples<AllocationProfileNode> =
    (
      entry: Entry<AllocationProfileNode>,
      samples: perftools.profiles.Sample[]
    ) => {
      if (entry.node.allocations.length > 0) {
        for (const alloc of entry.node.allocations) {
          const sample = new perftools.profiles.Sample({
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
  return profile;
}
