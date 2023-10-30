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

import * as fs from 'fs';
import * as path from 'path';
import * as tmp from 'tmp';
import {SourceMapGenerator} from 'source-map';

import {Function, Location, Profile, Sample, ValueType} from 'pprof-format';

import {TimeProfile} from '../src/v8-types';
import {StringTable} from 'pprof-format';

function buildStringTable(values: string[]): StringTable {
  const table = new StringTable();
  for (const value of values) {
    table.dedup(value);
  }
  return table;
}

const timeLeaf1 = {
  name: 'function1',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 10,
  columnNumber: 5,
  hitCount: 1,
  children: [],
};

const timeLeaf2 = {
  name: 'function1',
  scriptName: 'script2',
  scriptId: 2,
  lineNumber: 15,
  columnNumber: 3,
  hitCount: 2,
  children: [],
};

const timeLeaf3 = {
  name: 'function1',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 5,
  columnNumber: 3,
  hitCount: 1,
  children: [],
};

const timeNode1 = {
  name: 'function1',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 5,
  columnNumber: 3,
  hitCount: 3,
  children: [timeLeaf1, timeLeaf2],
};

const timeNode2 = {
  name: 'function2',
  scriptName: 'script2',
  scriptId: 2,
  lineNumber: 1,
  columnNumber: 5,
  hitCount: 0,
  children: [timeLeaf3],
};

const timeRoot = {
  name: '(root)',
  scriptName: 'root',
  scriptId: 0,
  lineNumber: 0,
  columnNumber: 0,
  hitCount: 0,
  children: [timeNode1, timeNode2],
};

export const v8TimeProfile: TimeProfile = Object.freeze({
  startTime: 0,
  endTime: 7 * 1000,
  topDownRoot: timeRoot,
});

const timeLines = [
  {functionId: 1, line: 1},
  {functionId: 2, line: 5},
  {functionId: 3, line: 15},
  {functionId: 2, line: 10},
];

const timeFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
  new Function({
    id: 2,
    name: 7,
    systemName: 7,
    filename: 8,
  }),
  new Function({
    id: 3,
    name: 7,
    systemName: 7,
    filename: 6,
  }),
];

const timeLocations = [
  new Location({
    line: [timeLines[0]],
    id: 1,
  }),
  new Location({
    line: [timeLines[1]],
    id: 2,
  }),
  new Location({
    line: [timeLines[2]],
    id: 3,
  }),
  new Location({
    line: [timeLines[3]],
    id: 4,
  }),
];

export const timeProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [2, 1],
      value: [1, 1000000],
      label: [],
    }),
    new Sample({
      locationId: [2],
      value: [3, 3000000],
      label: [],
    }),
    new Sample({
      locationId: [3, 2],
      value: [2, 2000000],
      label: [],
    }),
    new Sample({
      locationId: [4, 2],
      value: [1, 1000000],
      label: [],
    }),
  ],
  location: timeLocations,
  function: timeFunctions,
  stringTable: buildStringTable([
    'sample',
    'count',
    'wall',
    'nanoseconds',
    'function2',
    'script2',
    'function1',
    'script1',
  ]),
  timeNanos: 0,
  durationNanos: 7 * 1000 * 1000,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 1000000,
});

// timeProfile is encoded then decoded to convert numbers to longs, in
// decodedTimeProfile
const encodedTimeProfile = timeProfile.encode();
export const decodedTimeProfile = Object.freeze(
  Profile.decode(encodedTimeProfile)
);

const heapLeaf1 = {
  name: 'function2',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 8,
  columnNumber: 5,
  allocations: [{count: 5, sizeBytes: 1024}],
  children: [],
};

const heapLeaf2 = {
  name: 'function3',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 10,
  columnNumber: 5,
  allocations: [
    {count: 8, sizeBytes: 10},
    {count: 15, sizeBytes: 72},
  ],
  children: [],
};

const heapNode2 = {
  name: 'function1',
  scriptName: 'script1',
  scriptId: 1,
  lineNumber: 5,
  columnNumber: 5,
  allocations: [],
  children: [heapLeaf1, heapLeaf2],
};

const heapNode1 = {
  name: 'main',
  scriptName: 'main',
  scriptId: 0,
  lineNumber: 1,
  columnNumber: 5,
  allocations: [
    {count: 1, sizeBytes: 5},
    {count: 3, sizeBytes: 7},
  ],
  children: [heapNode2],
};

export const v8HeapProfile = Object.freeze({
  name: '(root)',
  scriptName: '(root)',
  scriptId: 10000,
  lineNumber: 0,
  columnNumber: 5,
  allocations: [],
  children: [heapNode1],
});

const heapLines = [
  {functionId: 1, line: 1},
  {functionId: 2, line: 5},
  {functionId: 3, line: 10},
  {functionId: 4, line: 8},
];

const heapFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 5,
  }),
  new Function({
    id: 2,
    name: 6,
    systemName: 6,
    filename: 7,
  }),
  new Function({
    id: 3,
    name: 8,
    systemName: 8,
    filename: 7,
  }),
  new Function({
    id: 4,
    name: 9,
    systemName: 9,
    filename: 7,
  }),
];

const heapLocations = [
  new Location({line: [heapLines[0]], id: 1}),
  new Location({line: [heapLines[1]], id: 2}),
  new Location({line: [heapLines[2]], id: 3}),
  new Location({line: [heapLines[3]], id: 4}),
];

export const heapProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [1],
      value: [1, 5],
      label: [],
    }),
    new Sample({
      locationId: [1],
      value: [3, 21],
      label: [],
    }),
    new Sample({
      locationId: [3, 2, 1],
      value: [8, 80],
      label: [],
    }),
    new Sample({
      locationId: [3, 2, 1],
      value: [15, 15 * 72],
      label: [],
    }),
    new Sample({
      locationId: [4, 2, 1],
      value: [5, 5 * 1024],
      label: [],
    }),
  ],
  location: heapLocations,
  function: heapFunctions,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    'main',
    'function1',
    'script1',
    'function3',
    'function2',
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

// heapProfile is encoded then decoded to convert numbers to longs, in
// decodedHeapProfile
const encodedHeapProfile = heapProfile.encode();
export const decodedHeapProfile = Object.freeze(
  Profile.decode(encodedHeapProfile)
);

const heapLinesWithExternal = [
  {functionId: 1},
  {functionId: 2, line: 1},
  {functionId: 3, line: 5},
  {functionId: 4, line: 10},
  {functionId: 5, line: 8},
];

const heapFunctionsWithExternal = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 0,
  }),
  new Function({
    id: 2,
    name: 6,
    systemName: 6,
    filename: 6,
  }),
  new Function({
    id: 3,
    name: 7,
    systemName: 7,
    filename: 8,
  }),
  new Function({
    id: 4,
    name: 9,
    systemName: 9,
    filename: 8,
  }),
  new Function({
    id: 5,
    name: 10,
    systemName: 10,
    filename: 8,
  }),
];

const heapLocationsWithExternal = [
  new Location({line: [heapLinesWithExternal[0]], id: 1}),
  new Location({line: [heapLinesWithExternal[1]], id: 2}),
  new Location({line: [heapLinesWithExternal[2]], id: 3}),
  new Location({line: [heapLinesWithExternal[3]], id: 4}),
  new Location({line: [heapLinesWithExternal[4]], id: 5}),
];

export const heapProfileWithExternal = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [1],
      value: [1, 1024],
      label: [],
    }),
    new Sample({
      locationId: [2],
      value: [1, 5],
      label: [],
    }),
    new Sample({
      locationId: [2],
      value: [3, 21],
      label: [],
    }),
    new Sample({
      locationId: [4, 3, 2],
      value: [8, 80],
      label: [],
    }),
    new Sample({
      locationId: [4, 3, 2],
      value: [15, 15 * 72],
      label: [],
    }),
    new Sample({
      locationId: [5, 3, 2],
      value: [5, 5 * 1024],
      label: [],
    }),
  ],
  location: heapLocationsWithExternal,
  function: heapFunctionsWithExternal,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    '(external)',
    'main',
    'function1',
    'script1',
    'function3',
    'function2',
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

// heapProfile is encoded then decoded to convert numbers to longs, in
// decodedHeapProfile
const encodedHeapProfileWithExternal = heapProfile.encode();
export const decodedHeapProfileWithExternal = Object.freeze(
  Profile.decode(encodedHeapProfileWithExternal)
);

const anonymousHeapNode = {
  scriptName: 'main',
  scriptId: 0,
  lineNumber: 1,
  columnNumber: 5,
  allocations: [{count: 1, sizeBytes: 5}],
  children: [],
};

export const v8AnonymousFunctionHeapProfile = Object.freeze({
  name: '(root)',
  scriptName: '(root)',
  scriptId: 10000,
  lineNumber: 0,
  columnNumber: 5,
  allocations: [],
  children: [anonymousHeapNode],
});

const anonymousFunctionHeapLines = [{functionId: 1, line: 1}];

const anonymousFunctionHeapFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
];

const anonymousFunctionHeapLocations = [
  new Location({
    line: [anonymousFunctionHeapLines[0]],
    id: 1,
  }),
];

export const anonymousFunctionHeapProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [1],
      value: [1, 5],
      label: [],
    }),
  ],
  location: anonymousFunctionHeapLocations,
  function: anonymousFunctionHeapFunctions,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    '(anonymous@L1:C5)',
    'main',
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

const anonymousFunctionTimeNode = {
  scriptName: 'main',
  scriptId: 2,
  lineNumber: 1,
  columnNumber: 5,
  hitCount: 1,
  children: [],
};

const anonymousFunctionTimeRoot = {
  name: '(root)',
  scriptName: 'root',
  scriptId: 0,
  lineNumber: 0,
  columnNumber: 0,
  hitCount: 0,
  children: [anonymousFunctionTimeNode],
};

export const v8AnonymousFunctionTimeProfile: TimeProfile = Object.freeze({
  startTime: 0,
  endTime: 10 * 1000 * 1000,
  topDownRoot: anonymousFunctionTimeRoot,
});

const anonymousFunctionTimeLines = [{functionId: 1, line: 1}];

const anonymousFunctionTimeFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
];

const anonymousFunctionTimeLocations = [
  new Location({
    line: [anonymousFunctionTimeLines[0]],
    id: 1,
  }),
];

export const anonymousFunctionTimeProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [1],
      value: [1, 1000000],
      label: [],
    }),
  ],
  location: anonymousFunctionTimeLocations,
  function: anonymousFunctionTimeFunctions,
  stringTable: buildStringTable([
    'sample',
    'count',
    'wall',
    'nanoseconds',
    '(anonymous@L1:C5)',
    'main',
  ]),
  timeNanos: 0,
  durationNanos: 10 * 1000 * 1000 * 1000,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 1000000,
});

const heapWithPathLeaf1 = {
  name: 'foo2',
  scriptName: 'foo.ts',
  scriptId: 0,
  lineNumber: 3,
  columnNumber: 3,
  allocations: [{count: 1, sizeBytes: 2}],
  children: [],
};

const heapWithPathLeaf2 = {
  name: 'bar',
  scriptName: '@google-cloud/profiler/profiler.ts',
  scriptId: 1,
  lineNumber: 10,
  columnNumber: 5,
  allocations: [{count: 2, sizeBytes: 2}],
  children: [],
};

const heapWithPathLeaf3 = {
  name: 'bar',
  scriptName: 'bar.ts',
  scriptId: 2,
  lineNumber: 3,
  columnNumber: 3,
  allocations: [{count: 3, sizeBytes: 2}],
  children: [],
};

const heapWithPathNode2 = {
  name: 'baz',
  scriptName: 'foo.ts',
  scriptId: 0,
  lineNumber: 1,
  columnNumber: 5,
  allocations: [],
  children: [heapWithPathLeaf1, heapWithPathLeaf2],
};

const heapWithPathNode1 = {
  name: 'foo1',
  scriptName: 'node_modules/@google-cloud/profiler/profiler.ts',
  scriptId: 3,
  lineNumber: 2,
  columnNumber: 5,
  allocations: [],
  children: [heapWithPathLeaf3],
};

export const v8HeapWithPathProfile = Object.freeze({
  name: '(root)',
  scriptName: '(root)',
  scriptId: 10000,
  lineNumber: 0,
  columnNumber: 5,
  allocations: [],
  children: [heapWithPathNode1, heapWithPathNode2],
});

const heapIncludePathFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
  new Function({
    id: 2,
    name: 7,
    systemName: 7,
    filename: 8,
  }),
  new Function({
    id: 3,
    name: 9,
    systemName: 9,
    filename: 6,
  }),
  new Function({
    id: 4,
    name: 10,
    systemName: 10,
    filename: 11,
  }),
  new Function({
    id: 5,
    name: 7,
    systemName: 7,
    filename: 12,
  }),
];

const heapIncludePathLocations = [
  new Location({
    line: [{functionId: 1, line: 1}],
    id: 1,
  }),
  new Location({
    line: [{functionId: 2, line: 10}],
    id: 2,
  }),
  new Location({
    line: [{functionId: 3, line: 3}],
    id: 3,
  }),
  new Location({
    line: [{functionId: 4, line: 2}],
    id: 4,
  }),
  new Location({
    line: [{functionId: 5, line: 3}],
    id: 5,
  }),
];

export const heapProfileIncludePath = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [2, 1],
      value: [2, 4],
      label: [],
    }),
    new Sample({
      locationId: [3, 1],
      value: [1, 2],
      label: [],
    }),
    new Sample({
      locationId: [5, 4],
      value: [3, 6],
      label: [],
    }),
  ],
  location: heapIncludePathLocations,
  function: heapIncludePathFunctions,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    'baz',
    'foo.ts',
    'bar',
    '@google-cloud/profiler/profiler.ts',
    'foo2',
    'foo1',
    'node_modules/@google-cloud/profiler/profiler.ts',
    'bar.ts',
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

// heapProfile is encoded then decoded to convert numbers to longs, in
// decodedHeapProfile
const encodedHeapProfileIncludePath = heapProfileIncludePath.encode();
export const decodedHeapProfileIncludePath = Object.freeze(
  Profile.decode(encodedHeapProfileIncludePath)
);

const heapExcludePathFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
  new Function({
    id: 2,
    name: 7,
    systemName: 7,
    filename: 6,
  }),
];

const heapExcludePathLocations = [
  new Location({
    line: [{functionId: 1, line: 1}],
    id: 1,
  }),
  new Location({
    line: [{functionId: 2, line: 3}],
    id: 2,
  }),
];

export const heapProfileExcludePath = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [2, 1],
      value: [1, 2],
      label: [],
    }),
  ],
  location: heapExcludePathLocations,
  function: heapExcludePathFunctions,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    'baz',
    'foo.ts',
    'foo2',
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

// heapProfile is encoded then decoded to convert numbers to longs, in
// decodedHeapProfile
const encodedHeapProfileExcludePath = heapProfileExcludePath.encode();
export const decodedHeapProfileExcludePath = Object.freeze(
  Profile.decode(encodedHeapProfileExcludePath)
);

const mapDir = tmp.dirSync();
export const mapDirPath = mapDir.name;

export const mapFoo = new SourceMapGenerator({file: 'foo.js'});
mapFoo.addMapping({
  source: path.join(mapDirPath, 'foo.ts'),
  name: 'foo1',
  generated: {line: 1, column: 3},
  original: {line: 10, column: 0},
});
mapFoo.addMapping({
  source: path.join(mapDirPath, 'foo.ts'),
  name: 'foo2',
  generated: {line: 5, column: 5},
  original: {line: 20, column: 0},
});

export const mapBaz = new SourceMapGenerator({file: 'baz.js'});
mapBaz.addMapping({
  source: path.join(mapDirPath, 'baz.ts'),
  name: 'baz',
  generated: {line: 3, column: 0},
  original: {line: 5, column: 0},
});

fs.writeFileSync(path.join(mapDirPath, 'foo.js.map'), mapFoo.toString());
fs.writeFileSync(path.join(mapDirPath, 'foo.js'), '');
fs.writeFileSync(path.join(mapDirPath, 'baz.js.map'), mapBaz.toString());
fs.writeFileSync(path.join(mapDirPath, 'baz.js'), '');

const heapGeneratedLeaf1 = {
  name: 'foo2',
  scriptName: path.join(mapDirPath, 'foo.js'),
  scriptId: 1,
  lineNumber: 5,
  columnNumber: 6,
  allocations: [{count: 3, sizeBytes: 2}],
  children: [],
};

const heapGeneratedLeaf2 = {
  name: 'baz',
  scriptName: path.join(mapDirPath, 'baz.js'),
  scriptId: 3,
  lineNumber: 3,
  columnNumber: 1,
  allocations: [{count: 5, sizeBytes: 5}],
  children: [],
};

const heapGeneratedNode2 = {
  name: 'bar',
  scriptName: path.join(mapDirPath, 'bar.js'),
  scriptId: 2,
  lineNumber: 10,
  columnNumber: 1,
  allocations: [],
  children: [heapGeneratedLeaf2],
};

const heapGeneratedNode1 = {
  name: 'foo1',
  scriptName: path.join(mapDirPath, 'foo.js'),
  scriptId: 1,
  lineNumber: 1,
  columnNumber: 4,
  allocations: [],
  children: [heapGeneratedNode2, heapGeneratedLeaf1],
};

export const v8HeapGeneratedProfile = Object.freeze({
  name: '(root)',
  scriptName: '(root)',
  scriptId: 10000,
  lineNumber: 0,
  columnNumber: 0,
  allocations: [],
  children: [heapGeneratedNode1],
});

const heapSourceFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
  new Function({
    id: 2,
    name: 7,
    systemName: 7,
    filename: 6,
  }),
  new Function({
    id: 3,
    name: 8,
    systemName: 8,
    filename: 9,
  }),
  new Function({
    id: 4,
    name: 10,
    systemName: 10,
    filename: 11,
  }),
];

const heapSourceLocations = [
  new Location({
    line: [{functionId: 1, line: 10}],
    id: 1,
  }),
  new Location({
    line: [{functionId: 2, line: 20}],
    id: 2,
  }),
  new Location({
    line: [{functionId: 3, line: 10}],
    id: 3,
  }),
  new Location({
    line: [{functionId: 4, line: 5}],
    id: 4,
  }),
];

export const heapSourceProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [2, 1],
      value: [3, 6],
      label: [],
    }),
    new Sample({
      locationId: [4, 3, 1],
      value: [5, 25],
      label: [],
    }),
  ],
  location: heapSourceLocations,
  function: heapSourceFunctions,
  stringTable: buildStringTable([
    'objects',
    'count',
    'space',
    'bytes',
    'foo1',
    path.join(mapDirPath, 'foo.ts'),
    'foo2',
    'bar',
    path.join(mapDirPath, 'bar.js'),
    'baz',
    path.join(mapDirPath, 'baz.ts'),
  ]),
  timeNanos: 0,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 524288,
});

const timeGeneratedLeaf1 = {
  name: 'foo',
  scriptName: path.join(mapDirPath, 'foo.js'),
  scriptId: 1,
  lineNumber: 5,
  columnNumber: 6,
  hitCount: 5,
  children: [],
};

const timeGeneratedLeaf2 = {
  name: 'baz',
  scriptName: path.join(mapDirPath, 'baz.js'),
  scriptId: 3,
  lineNumber: 3,
  columnNumber: 1,
  hitCount: 10,
  children: [],
};

const timeGeneratedNode2 = {
  name: 'bar',
  scriptName: path.join(mapDirPath, 'bar.js'),
  scriptId: 2,
  lineNumber: 10,
  columnNumber: 1,
  children: [timeGeneratedLeaf2],
};

const timeGeneratedNode1 = {
  name: 'foo1',
  scriptName: path.join(mapDirPath, 'foo.js'),
  scriptId: 1,
  lineNumber: 1,
  columnNumber: 4,
  hitCount: 0,
  children: [timeGeneratedNode2, timeGeneratedLeaf1],
};

export const timeGeneratedProfileRoot = Object.freeze({
  name: '(root)',
  scriptName: '(root)',
  scriptId: 10000,
  lineNumber: 0,
  columnNumber: 0,
  hitCount: 0,
  children: [timeGeneratedNode1],
});

export const v8TimeGeneratedProfile: TimeProfile = Object.freeze({
  startTime: 0,
  endTime: 10 * 1000 * 1000,
  topDownRoot: timeGeneratedProfileRoot,
});

const timeSourceFunctions = [
  new Function({
    id: 1,
    name: 5,
    systemName: 5,
    filename: 6,
  }),
  new Function({
    id: 2,
    name: 7,
    systemName: 7,
    filename: 6,
  }),
  new Function({
    id: 3,
    name: 8,
    systemName: 8,
    filename: 9,
  }),
  new Function({
    id: 4,
    name: 10,
    systemName: 10,
    filename: 11,
  }),
];

const timeSourceLocations = [
  new Location({
    line: [{functionId: 1, line: 10}],
    id: 1,
  }),
  new Location({
    line: [{functionId: 2, line: 20}],
    id: 2,
  }),
  new Location({
    line: [{functionId: 3, line: 10}],
    id: 3,
  }),
  new Location({
    line: [{functionId: 4, line: 5}],
    id: 4,
  }),
];

export const timeSourceProfile = new Profile({
  sampleType: [
    new ValueType({type: 1, unit: 2}),
    new ValueType({type: 3, unit: 4}),
  ],
  sample: [
    new Sample({
      locationId: [2, 1],
      value: [5, 5000000],
      label: [],
    }),
    new Sample({
      locationId: [4, 3, 1],
      value: [10, 10000000],
      label: [],
    }),
  ],
  location: timeSourceLocations,
  function: timeSourceFunctions,
  stringTable: buildStringTable([
    'sample',
    'count',
    'wall',
    'nanoseconds',
    'foo1',
    path.join(mapDirPath, 'foo.ts'),
    'foo2',
    'bar',
    path.join(mapDirPath, 'bar.js'),
    'baz',
    path.join(mapDirPath, 'baz.ts'),
  ]),
  timeNanos: 0,
  durationNanos: 10 * 1000 * 1000 * 1000,
  periodType: new ValueType({type: 3, unit: 4}),
  period: 1000000,
});

export const labelEncodingProfile = {
  startTime: 0,
  endTime: 10 * 1000 * 1000,
  topDownRoot: {
    name: '(root)',
    scriptName: '(root)',
    scriptId: 10000,
    lineNumber: 0,
    columnNumber: 0,
    hitCount: 0,
    children: [
      {
        name: 'foo1',
        scriptName: 'foo',
        scriptId: 1,
        lineNumber: 1,
        columnNumber: 4,
        hitCount: 1,
        children: [],
        contexts: [
          {
            context: {
              someStr: 'foo',
              someNum: 42,
              someBigint: 18446744073709551557n,
              ignored: {},
            },
          },
        ],
      },
    ],
  },
};
