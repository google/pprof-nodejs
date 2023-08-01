// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {Worker, isMainThread} from 'worker_threads';
import {time} from '../src/index';
import {Profile, StringTable, ValueType} from 'pprof-format';

const assert = require('assert');

const {hasOwnProperty} = Object.prototype;

const durationMillis = 300;
const intervalMicros = 1000;
const withContexts =
  process.platform === 'darwin' || process.platform === 'linux';

function createWorker(): Promise<void> {
  return new Promise((resolve, reject) => {
    new Worker(__filename)
      .on('exit', exitCode => {
        if (exitCode !== 0) reject();
        setTimeout(() => {
          // Run a second worker after the first one exited to test for proper
          // cleanup after first worker. This used to segfault.
          new Worker(__filename)
            .on('exit', exitCode => {
              if (exitCode !== 0) reject();
              resolve();
            })
            .on('error', reject);
        }, Math.floor(Math.random() * durationMillis));
      })
      .on('error', reject);
  });
}

async function executeWorkers(nbWorkers: number) {
  return Promise.all(Array.from({length: nbWorkers}, createWorker));
}

async function main() {
  time.start({
    durationMillis: durationMillis * 3,
    intervalMicros,
    withContexts,
  });

  const nbWorkers = Number(process.argv[2] || 4);
  // start workers
  const workers = executeWorkers(nbWorkers);
  // do some work
  foo(durationMillis);
  // wait for all workers to finish
  await workers;
  // restart and check profile
  const profile = time.stop(true);
  checkProfile(profile);
  foo(durationMillis);

  const profile2 = time.stop();
  checkProfile(profile2);
}

function worker() {
  const p = time.profile({
    durationMillis,
    intervalMicros,
    withContexts,
  });

  foo(durationMillis);

  p.then(profile => {
    checkProfile(profile);
  });
}

if (isMainThread) {
  main();
} else {
  worker();
}

function valueName(profile: Profile, vt: ValueType) {
  const type = getAndVerifyString(profile.stringTable!, vt, 'type');
  const unit = getAndVerifyString(profile.stringTable!, vt, 'unit');
  return `${type}/${unit}`;
}

function sampleName(profile: Profile, sampleType: ValueType[]) {
  return sampleType.map(valueName.bind(null, profile));
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function getAndVerifyPresence(list: any[], id: number, zeroIndex = false) {
  assert.strictEqual(typeof id, 'number', 'has id');
  const index = id - (zeroIndex ? 0 : 1);
  assert.ok(list.length > index, 'exists in list');
  return list[index];
}

function getAndVerifyString(
  stringTable: StringTable,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  source: any,
  field: string
) {
  assert.ok(hasOwnProperty.call(source, field), 'has id field');
  const str = getAndVerifyPresence(
    stringTable.strings,
    source[field] as number,
    true
  );
  assert.strictEqual(typeof str, 'string', 'is a string');
  return str;
}

function checkProfile(profile: Profile) {
  assert.deepStrictEqual(sampleName(profile, profile.sampleType!), [
    'sample/count',
    'wall/nanoseconds',
  ]);
  assert.strictEqual(typeof profile.timeNanos, 'number');
  assert.strictEqual(typeof profile.durationNanos, 'number');
  assert.strictEqual(typeof profile.period, 'number');
  assert.strictEqual(
    valueName(profile, profile.periodType!),
    'wall/nanoseconds'
  );

  assert.ok(profile.sample.length > 0, 'No samples');

  for (const sample of profile.sample!) {
    assert.deepStrictEqual(sample.label, []);

    for (const value of sample.value!) {
      assert.strictEqual(typeof value, 'number');
    }

    for (const locationId of sample.locationId!) {
      const location = getAndVerifyPresence(
        profile.location!,
        locationId as number
      );

      for (const {functionId, line} of location.line!) {
        const fn = getAndVerifyPresence(
          profile.function!,
          functionId as number
        );

        getAndVerifyString(profile.stringTable!, fn, 'name');
        getAndVerifyString(profile.stringTable!, fn, 'systemName');
        getAndVerifyString(profile.stringTable!, fn, 'filename');
        assert.strictEqual(typeof line, 'number');
      }
    }
  }
}

function foo(ms: number) {
  const now = Date.now();
  while (Date.now() - now < ms) {
    undefined;
  }
}
