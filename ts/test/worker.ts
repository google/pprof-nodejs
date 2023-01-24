// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {Worker, isMainThread} from 'worker_threads';
import {time} from '../src/index';
import {Profile, StringTable, ValueType} from 'pprof-format';

const assert = require('assert');

const {hasOwnProperty} = Object.prototype;

if (isMainThread) {
  new Worker(__filename);
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

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function getAndVerifyString(
  stringTable: StringTable,
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

time
  .profile({
    durationMillis: 500,
  })
  .then(profile => {
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

    console.log('it works!');
  })
  .catch(err => {
    console.error(err.stack);
    process.exitCode = 1;
  });
