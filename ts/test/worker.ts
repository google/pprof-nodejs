import {Worker, isMainThread, workerData, parentPort} from 'worker_threads';
import {pbkdf2} from 'crypto';
import {time} from '../src/index';
import {Profile, ValueType} from 'pprof-format';
import {getAndVerifyPresence, getAndVerifyString} from './profiles-for-tests';

import assert from 'assert';

const DURATION_MILLIS = 1000;
const intervalMicros = 10000;
const withContexts =
  process.platform === 'darwin' || process.platform === 'linux';

function createWorker(durationMs: number): Promise<Profile[]> {
  return new Promise((resolve, reject) => {
    const profiles: Profile[] = [];
    new Worker(__filename, {workerData: {durationMs}})
      .on('exit', exitCode => {
        if (exitCode !== 0) reject();
        setTimeout(() => {
          // Run a second worker after the first one exited to test for proper
          // cleanup after first worker. This used to segfault.
          new Worker(__filename, {workerData: {durationMs}})
            .on('exit', exitCode => {
              if (exitCode !== 0) reject();
              resolve(profiles);
            })
            .on('error', reject)
            .on('message', profile => {
              profiles.push(profile);
            });
        }, Math.floor(Math.random() * durationMs));
      })
      .on('error', reject)
      .on('message', profile => {
        profiles.push(profile);
      });
  });
}

async function executeWorkers(nbWorkers: number, durationMs: number) {
  const workers = [];
  for (let i = 0; i < nbWorkers; i++) {
    workers.push(createWorker(durationMs));
  }
  return Promise.all(workers).then(profiles => profiles.flat());
}

function getCpuUsage() {
  const cpu = process.cpuUsage();
  return cpu.user + cpu.system;
}

async function main(durationMs: number) {
  time.start({
    durationMillis: durationMs * 3,
    intervalMicros,
    withContexts,
    collectCpuTime: withContexts,
  });

  const cpu0 = getCpuUsage();
  const nbWorkers = Number(process.argv[2] || 2);

  // start workers
  const workers = executeWorkers(nbWorkers, durationMs);

  const deadline = Date.now() + durationMs;
  // wait for all work to finish
  await Promise.all([bar(deadline), foo(deadline)]);
  const workerProfiles = await workers;

  // restart and check profile
  const profile1 = time.stop(true);
  const cpu1 = getCpuUsage();

  workerProfiles.forEach(checkProfile);
  checkProfile(profile1);
  if (withContexts) {
    checkCpuTime(profile1, cpu1 - cpu0, workerProfiles);
  }
  const newDeadline = Date.now() + durationMs;
  await Promise.all([bar(newDeadline), foo(newDeadline)]);

  const profile2 = time.stop();
  const cpu2 = getCpuUsage();
  checkProfile(profile2);
  if (withContexts) {
    checkCpuTime(profile2, cpu2 - cpu1);
  }
}

async function worker(durationMs: number) {
  time.start({
    durationMillis: durationMs,
    intervalMicros,
    withContexts,
    collectCpuTime: withContexts,
  });

  const deadline = Date.now() + durationMs;
  await Promise.all([bar(deadline), foo(deadline)]);

  const profile = time.stop();
  parentPort?.postMessage(profile);
}

if (isMainThread) {
  main(DURATION_MILLIS);
} else {
  worker(workerData.durationMs);
}

function valueName(profile: Profile, vt: ValueType) {
  const type = getAndVerifyString(profile.stringTable!, vt, 'type');
  const unit = getAndVerifyString(profile.stringTable!, vt, 'unit');
  return `${type}/${unit}`;
}

function sampleName(profile: Profile, sampleType: ValueType[]) {
  return sampleType.map(valueName.bind(null, profile));
}

function getCpuTime(profile: Profile) {
  let jsCpuTime = 0;
  let nonJsCpuTime = 0;
  if (!withContexts) return {jsCpuTime, nonJsCpuTime};
  for (const sample of profile.sample!) {
    const locationId = sample.locationId[0];
    const location = getAndVerifyPresence(
      profile.location!,
      locationId as number
    );
    const functionId = location.line![0].functionId;
    const fn = getAndVerifyPresence(profile.function!, functionId as number);
    const fn_name = profile.stringTable.strings[fn.name as number];
    if (fn_name === time.constants.NON_JS_THREADS_FUNCTION_NAME) {
      nonJsCpuTime += sample.value![2] as number;
      assert.strictEqual(sample.value![0], 0);
      assert.strictEqual(sample.value![1], 0);
    } else {
      jsCpuTime += sample.value![2] as number;
    }
  }

  return {jsCpuTime, nonJsCpuTime};
}

function checkCpuTime(
  profile: Profile,
  processCpuTimeMicros: number,
  workerProfiles: Profile[] = [],
  maxRelativeError = 0.1
) {
  let workersJsCpuTime = 0;
  let workersNonJsCpuTime = 0;

  for (const workerProfile of workerProfiles) {
    const {jsCpuTime, nonJsCpuTime} = getCpuTime(workerProfile);
    workersJsCpuTime += jsCpuTime;
    workersNonJsCpuTime += nonJsCpuTime;
  }

  const {jsCpuTime: mainJsCpuTime, nonJsCpuTime: mainNonJsCpuTime} =
    getCpuTime(profile);

  // workers should not report non-JS CPU time
  assert.strictEqual(
    workersNonJsCpuTime,
    0,
    'worker non-JS CPU time should be null'
  );

  const totalCpuTimeMicros =
    (mainJsCpuTime + mainNonJsCpuTime + workersJsCpuTime) / 1000;
  const err =
    Math.abs(totalCpuTimeMicros - processCpuTimeMicros) / processCpuTimeMicros;
  const msg = `process cpu time: ${
    processCpuTimeMicros / 1000
  }ms\ntotal profile cpu time: ${
    totalCpuTimeMicros / 1000
  }ms\nmain JS cpu time: ${mainJsCpuTime / 1000000}ms\nworker JS cpu time: ${
    workersJsCpuTime / 1000000
  }\nnon-JS cpu time: ${mainNonJsCpuTime / 1000000}ms\nerror: ${err}`;
  assert.ok(
    err <= maxRelativeError,
    `total profile CPU time should be close to process cpu time:\n${msg}`
  );
}

function checkProfile(profile: Profile) {
  assert.deepStrictEqual(sampleName(profile, profile.sampleType!), [
    'sample/count',
    'wall/nanoseconds',
    ...(withContexts ? ['cpu/nanoseconds'] : []),
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

async function bar(deadline: number) {
  let done = false;
  setTimeout(() => {
    done = true;
  }, deadline - Date.now());
  while (!done) {
    await new Promise<void>(resolve => {
      pbkdf2('secret', 'salt', 100000, 64, 'sha512', () => {
        resolve();
      });
    });
  }
}

function fooWork() {
  let sum = 0;
  for (let i = 0; i < 1e7; i++) {
    sum += sum;
  }
  return sum;
}

async function foo(deadline: number) {
  let done = false;
  setTimeout(() => {
    done = true;
  }, deadline - Date.now());

  while (!done) {
    await new Promise<void>(resolve => {
      fooWork();
      setImmediate(() => resolve());
    });
  }
}
