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

import * as sinon from 'sinon';
import CpuProfiler from '../src/cpu-profiler';
import * as v8CpuProfiler from '../src/cpu-profiler-bindings';
import {perftools} from '../../proto/profile';

const assert = require('assert');

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function str(profile: perftools.profiles.IProfile, index: any) {
  return profile.stringTable![index as number];
}

function verifyValueType(
  profile: perftools.profiles.IProfile,
  valueType: perftools.profiles.IValueType,
  name: string
) {
  const type = str(profile, valueType.type!);
  const unit = str(profile, valueType.unit!);

  assert.strictEqual(
    `${type}/${unit}`,
    name,
    'has expected type and unit for value type'
  );
}

function verifySampleType(
  profile: perftools.profiles.IProfile,
  index: number,
  name: string
) {
  const sampleType = profile.sampleType![index];
  verifyValueType(profile, sampleType, name);
}

function verifyPeriodType(profile: perftools.profiles.IProfile, name: string) {
  verifyValueType(profile, profile.periodType!, name);
}

function verifyFunction(profile: perftools.profiles.IProfile, index: number) {
  const fn = profile.function![index];
  assert.ok(fn, 'has function matching function id');
  assert.ok(fn.id! >= 0, 'has id for function');

  assert.strictEqual(
    typeof str(profile, fn.name),
    'string',
    'has name in string table for function'
  );
  assert.strictEqual(
    typeof str(profile, fn.systemName),
    'string',
    'has systemName in string table for function'
  );
  assert.strictEqual(
    typeof str(profile, fn.filename),
    'string',
    'has filename in string table for function'
  );
}

function verifyLocation(profile: perftools.profiles.IProfile, index: number) {
  const location = profile.location![index];
  assert.ok(location, 'has location matching location id');
  assert.ok(location.id! > 0, 'has id for location');

  for (const line of location.line!) {
    assert.ok(line.line! >= 0, 'has line number for line record');
    verifyFunction(profile, (line.functionId! as number) - 1);
  }
}

function verifySample(profile: perftools.profiles.IProfile, index: number) {
  const sample = profile.sample![index];
  for (const locationId of sample.locationId!) {
    verifyLocation(profile, (locationId as number) - 1);
  }
  assert.strictEqual(
    sample.value!.length,
    2,
    'has expected number of values in sample'
  );
}

function busyWait(ms: number) {
  return new Promise(resolve => {
    let done = false;
    function work() {
      if (done) return;
      let sum = 0;
      for (let i = 0; i < 1e6; i++) {
        sum += sum;
      }
      setImmediate(work, sum);
    }
    setImmediate(work);
    setTimeout(() => {
      done = true;
      resolve(undefined);
    }, ms);
  });
}

describe('CPU Profiler', () => {
  describe('profile (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    const cpuProfilerStub = {
      start: sinon.stub(),
      stop: sinon.stub().returns({}),
    };

    before(() => {
      sinonStubs.push(
        sinon.stub(v8CpuProfiler, 'CpuProfiler').returns(cpuProfilerStub)
      );
    });

    after(() => {
      sinonStubs.forEach(stub => {
        stub.restore();
      });
    });

    it('should have valid basic structure', async () => {
      const data = {str: 'foo', num: 123};
      const cpu = new CpuProfiler();
      cpu.start(99);
      cpu.labels = data;
      await busyWait(100);
      const profile = cpu.profile()!;
      cpu.stop();

      verifySampleType(profile, 0, 'sample/count');
      verifySampleType(profile, 1, 'cpu/nanoseconds');
      // verifySampleType(profile, 2, 'wall/nanoseconds');
      verifyPeriodType(profile, 'cpu/nanoseconds');

      assert.strictEqual(profile.period, 1000 / 99);
      assert.ok(profile.durationNanos! > 0);
      assert.ok(profile.timeNanos! > 0);

      assert.ok(profile.sample!.length > 0);
      assert.ok(profile.location!.length > 0);
      assert.ok(profile.function!.length > 0);

      verifySample(profile, 0);

      const {label = []} = profile.sample![0];
      assert.strictEqual(label.length, 2);
      assert.strictEqual(str(profile, label[0].key! as number), 'str');
      assert.strictEqual(str(profile, label[0].str! as number), 'foo');
      assert.strictEqual(str(profile, label[1].key! as number), 'num');
      assert.strictEqual(label[1].num!, 123);
    });

    it('should have timeNanos gap that roughly matches durationNanos', async () => {
      const wait = 100;
      // Need 10% wiggle room due to precision loss in timeNanos
      const minimumDuration = wait * 1e6 * 0.9;
      const cpu = new CpuProfiler();
      cpu.start(99);

      await busyWait(wait);
      const first = cpu.profile()!;
      assert.ok(first.durationNanos! >= minimumDuration);

      await busyWait(wait);
      const second = cpu.profile()!;
      assert.ok(
        second.timeNanos! >= (first.timeNanos! as number) + minimumDuration
      );
      assert.ok(second.durationNanos! >= minimumDuration);
      cpu.stop();
    });
  });
});
