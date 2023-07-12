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
import {hrtime} from 'process';
import {Label, Profile} from 'pprof-format';
import {AssertionError} from 'assert';

const assert = require('assert');

const PROFILE_OPTIONS = {
  durationMillis: 500,
  intervalMicros: 1000,
};

describe('Time Profiler', () => {
  describe('profile', () => {
    it('should exclude program and idle time', async () => {
      const profile = await time.profile(PROFILE_OPTIONS);
      assert.ok(profile.stringTable);
      assert.deepEqual(
        [
          profile.stringTable.strings!.indexOf('(program)'),
          profile.stringTable.strings!.indexOf('(idle)'),
        ],
        [-1, -1]
      );
    });

    it('should update state', function () {
      if (process.platform !== 'darwin' && process.platform !== 'linux') {
        this.skip();
      }
      time.start({
        intervalMicros: 20 * 1_000,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        lineNumbers: false,
      });
      const initialContext: {[key: string]: string} = {};
      time.setContext(initialContext);
      const kSampleCount = time.constants.kSampleCount;
      const state = time.getState();
      assert.equal(state[kSampleCount], 0, 'Initial state should be 0');
      const deadline = Date.now() + 1000;
      while (state[kSampleCount] === 0) {
        if (Date.now() > deadline) {
          assert.fail('State did not change');
        }
      }
      assert(state[kSampleCount] >= 1, 'Unexpected number of samples');

      let checked = false;
      initialContext['aaa'] = 'bbb';

      time.stop(false, (context: object) => {
        assert.deepEqual(context, initialContext, 'Unexpected context');
        checked = true;
        return {};
      });
      assert(checked, 'No context found');
    });

    it('should assign labels', function () {
      if (process.platform !== 'darwin' && process.platform !== 'linux') {
        this.skip();
      }
      this.timeout(3000);

      const intervalNanos = PROFILE_OPTIONS.intervalMicros * 1_000;
      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        lineNumbers: false,
      });
      // By repeating the test few times, we also exercise the profiler
      // start-stop overlap behavior.
      const repeats = 3;
      const rootSpanId = '1234';
      const endPointLabel = 'trace endpoint';
      const rootSpanIdLabel = 'local root span id';
      const endPoint = 'foo';
      let enableEndPoint = false;
      const label0 = {label: 'value0'};
      const label1 = {label: 'value1', [rootSpanIdLabel]: rootSpanId};

      for (let i = 0; i < repeats; ++i) {
        loop();
        enableEndPoint = i % 2 === 0;
        validateProfile(
          time.stop(
            i < repeats - 1,
            enableEndPoint ? generateLabels : undefined
          )
        );
      }

      function generateLabels(context: object) {
        const labels: time.LabelSet = {};
        for (const [key, value] of Object.entries(context)) {
          if (typeof value === 'string') {
            labels[key] = value;
            if (
              enableEndPoint &&
              key === 'local root span id' &&
              value === rootSpanId
            ) {
              labels[endPointLabel] = endPoint;
            }
          }
        }
        return labels;
      }

      // Each of fn0, fn1, fn2 loops busily for one or two profiling intervals.
      // fn0 resets the label; fn1 and fn2 don't. Label for fn1
      // is reset in the loop. This ensures the following invariants that we
      // test for:
      // label0 can be observed in loop or fn0
      // label1 can be observed in loop or fn1
      // fn0 might be observed with no label
      // fn1 must always be observed with label1
      // fn2 must never be observed with a label
      function fn0() {
        const start = hrtime.bigint();
        while (hrtime.bigint() - start < intervalNanos);
        time.setContext(undefined);
      }

      function fn1() {
        const start = hrtime.bigint();
        while (hrtime.bigint() - start < intervalNanos);
      }

      function fn2() {
        const start = hrtime.bigint();
        while (hrtime.bigint() - start < intervalNanos);
      }

      function loop() {
        const durationNanos = PROFILE_OPTIONS.durationMillis * 1_000_000;
        const start = hrtime.bigint();
        while (hrtime.bigint() - start < durationNanos) {
          time.setContext(label0);
          fn0();
          time.setContext(label1);
          fn1();
          time.setContext(undefined);
          fn2();
        }
      }

      function validateProfile(profile: Profile) {
        // Get string table indices for strings we're interested in
        const stringTable = profile.stringTable;
        const [loopIdx, fn0Idx, fn1Idx, fn2Idx] = [
          'loop',
          'fn0',
          'fn1',
          'fn2',
        ].map(x => stringTable.dedup(x));

        function getString(n: number | bigint): string {
          if (typeof n === 'number') {
            return stringTable.strings[n];
          }
          throw new AssertionError({message: 'Expected a number'});
        }

        function labelIs(l: Label, key: string, str: string) {
          return getString(l.key) === key && getString(l.str) === str;
        }

        function idx(n: number | bigint): number {
          if (typeof n === 'number') {
            // We want a 0-based array index, but IDs start from 1.
            return n - 1;
          }
          throw new AssertionError({message: 'Expected a number'});
        }

        function labelStr(label: Label) {
          return label ? stringTable.strings[idx(label.str) + 1] : 'undefined';
        }

        function getLabels(labels: Label[]) {
          const labelObj: {[key: string]: string} = {};
          labels.forEach(label => {
            labelObj[getString(label.key)] = getString(label.str);
          });
          return labelObj;
        }

        let fn0ObservedWithLabel0 = false;
        let fn1ObservedWithLabel1 = false;
        let fn2ObservedWithoutLabels = false;
        profile.sample.forEach(sample => {
          const locIdx = idx(sample.locationId[0]);
          const loc = profile.location[locIdx];
          const fnIdx = idx(loc.line[0].functionId);
          const fn = profile.function[fnIdx];
          const fnName = fn.name;
          const labels = sample.label;

          switch (fnName) {
            case loopIdx:
              if (enableEndPoint) {
                assert(
                  labels.length < 4,
                  'loop can have at most one label and one endpoint'
                );
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value0') ||
                      labelIs(label, 'label', 'value1') ||
                      labelIs(label, endPointLabel, endPoint) ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'loop can be observed with value0 or value1 or root span id or endpoint'
                  );
                });
              } else {
                if (labels.length >= 3) {
                  console.log(getLabels(labels));
                }

                assert(labels.length < 3, 'loop can have at most one label');
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value0') ||
                      labelIs(label, 'label', 'value1') ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'loop can be observed with value0 or value1 or root span id'
                  );
                });
              }

              break;
            case fn0Idx:
              assert(labels.length < 2, 'fn0 can have at most one label');
              labels.forEach(label => {
                if (labelIs(label, 'label', 'value0')) {
                  fn0ObservedWithLabel0 = true;
                } else {
                  throw new AssertionError({
                    message:
                      'Only value0 can be observed with fn0. Observed instead ' +
                      labelStr(label),
                  });
                }
              });
              break;
            case fn1Idx:
              if (enableEndPoint) {
                assert(
                  labels.length === 3,
                  'fn1 must be observed with a label, a root span id and an endpoint'
                );
                const labelMap = getLabels(labels);
                assert.deepEqual(labelMap, {
                  ...label1,
                  [endPointLabel]: endPoint,
                });
              } else {
                assert(
                  labels.length === 2,
                  'fn1 must be observed with a label'
                );
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value1') ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'Only value1 can be observed with fn1'
                  );
                });
              }
              fn1ObservedWithLabel1 = true;
              break;
            case fn2Idx:
              assert(
                labels.length === 0,
                'fn2 must be observed with no labels. Observed instead with ' +
                  labelStr(labels[0])
              );
              fn2ObservedWithoutLabels = true;
              break;
            default:
            // Make no assumptions about other functions; we can just as well
            // capture internals of time-profiler.ts, GC, etc.
          }
        });
        assert(fn0ObservedWithLabel0, 'fn0 was not observed with value0');
        assert(fn1ObservedWithLabel1, 'fn1 was not observed with value1');
        assert(
          fn2ObservedWithoutLabels,
          'fn2 was not observed without a label'
        );
      }
    });
  });

  describe('profile (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    const timeProfilerStub = {
      start: sinon.stub(),
      stop: sinon.stub().returns(v8TimeProfile),
    };

    before(() => {
      sinonStubs.push(
        sinon.stub(v8TimeProfiler, 'TimeProfiler').returns(timeProfilerStub)
      );
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

    it('should be able to restart when stopping', async () => {
      time.start({intervalMicros: PROFILE_OPTIONS.intervalMicros});
      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stop.resetHistory();

      assert.deepEqual(timeProfile, time.stop(true));

      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stop);

      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stop.resetHistory();

      assert.deepEqual(timeProfile, time.stop());

      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stop);
    });
  });
});
