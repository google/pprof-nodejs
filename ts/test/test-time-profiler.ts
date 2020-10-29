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

import delay from 'delay';
import * as sinon from 'sinon';
import * as time from '../src/time-profiler';
import * as v8TimeProfiler from '../src/time-profiler-bindings';
import {timeProfile, v8TimeProfile} from './profiles-for-tests';

const assert = require('assert');

const PROFILE_OPTIONS = {
  durationMillis: 500,
  intervalMicros: 1000,
};

describe('Time Profiler', () => {
  describe('profile', () => {
    it('should detect program or idle time', async () => {
      const profile = await time.profile(PROFILE_OPTIONS);
      assert.ok(profile.stringTable);
      assert.notDeepEqual(
        [
          profile.stringTable!.indexOf('(program)'),
          profile.stringTable!.indexOf('(idle)'),
        ],
        [-1, -1]
      );
    });
  });

  describe('profile (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    before(() => {
      sinonStubs.push(sinon.stub(v8TimeProfiler, 'startProfiling'));
      sinonStubs.push(
        sinon.stub(v8TimeProfiler, 'stopProfiling').returns(v8TimeProfile)
      );
      sinonStubs.push(sinon.stub(v8TimeProfiler, 'setSamplingInterval'));
      sinonStubs.push(sinon.stub(Date, 'now').returns(0));
    });

    after(() => {
      sinonStubs.forEach(stub => {
        stub.restore();
      });
    });

    it('should profile during duration and finish profiling after duration', async () => {
      let isProfiling = true;
      time.profile(PROFILE_OPTIONS).then(() => {
        isProfiling = false;
      });
      await delay(2 * PROFILE_OPTIONS.durationMillis);
      assert.strictEqual(false, isProfiling, 'profiler is still running');
    });

    it('should return a profile equal to the expected profile', async () => {
      const profile = await time.profile(PROFILE_OPTIONS);
      assert.deepEqual(timeProfile, profile);
    });
  });
});
