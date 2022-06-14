/**
 * Copyright 2019 Google Inc. All Rights Reserved.
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

import fs from 'fs';
import pprof from 'pprof';

const startTime = Date.now();
const testArr = [];

/**
 * Fills several arrays, then calls itself with setTimeout.
 * It continues to do this until durationSeconds after the startTime.
 */
function busyLoop(durationSeconds) {
  for (let i = 0; i < testArr.length; i++) {
    for (let j = 0; j < testArr[i].length; j++) {
      testArr[i][j] = Math.sqrt(j * testArr[i][j]);
    }
  }
  if (Date.now() - startTime < 1000 * durationSeconds) {
    setTimeout(() => busyLoop(durationSeconds), 5);
  }
}

function benchmark(durationSeconds) {
  // Allocate 16 MiB in 64 KiB chunks.
  for (let i = 0; i < 16 * 16; i++) {
    testArr[i] = new Array(64 * 1024);
  }
  busyLoop(durationSeconds);
}

async function collectAndSaveTimeProfile(
  durationSeconds,
  sourceMapper,
  lineNumbers
) {
  const profile = await pprof.time.profile({
    durationMillis: 1000 * durationSeconds,
    lineNumbers: lineNumbers,
    sourceMapper: sourceMapper,
  });
  const buf = await pprof.encode(profile);
  await fs.promises.writeFile('time.pb.gz', buf);
}

async function collectAndSaveHeapProfile(sourceMapper) {
  const profile = pprof.heap.profile(undefined, sourceMapper);
  const buf = await pprof.encode(profile);
  await fs.promises.writeFile('heap.pb.gz', buf);
}

async function collectAndSaveProfiles(collectLineNumberTimeProfile) {
  const sourceMapper = await pprof.SourceMapper.create([process.cwd()]);
  collectAndSaveHeapProfile(sourceMapper);
  collectAndSaveTimeProfile(
    durationSeconds / 2,
    sourceMapper,
    collectLineNumberTimeProfile
  );
}

const durationSeconds = Number(process.argv.length > 2 ? process.argv[2] : 30);
const collectLineNumberTimeProfile = Boolean(
  process.argv.length > 3 ? process.argv[3] : false
);

pprof.heap.start(512 * 1024, 64);
benchmark(durationSeconds);

collectAndSaveProfiles(collectLineNumberTimeProfile);
