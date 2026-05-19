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
import {time, getNativeThreadId} from '../src';
import {profileV2, stopV2} from '../src/time-profiler';
import * as v8TimeProfiler from '../src/time-profiler-bindings';
import {timeProfile, v8TimeProfile} from './profiles-for-tests';
import {hrtime} from 'process';
import {Label, Profile} from 'pprof-format';
import {AssertionError} from 'assert';
import {GenerateTimeLabelsArgs, LabelSet} from '../src/v8-types';
import {satisfies} from 'semver';
import {setTimeout as setTimeoutPromise} from 'timers/promises';
import {fork} from 'child_process';

import assert from 'assert';

const useCPED =
  (satisfies(process.versions.node, '>=24.0.0') &&
    !process.execArgv.includes('--no-async-context-frame')) ||
  (satisfies(process.versions.node, '>=22.7.0') &&
    process.execArgv.includes('--experimental-async-context-frame'));

const collectAsyncId = satisfies(process.versions.node, '>=24.0.0');

const unsupportedPlatform =
  process.platform !== 'darwin' && process.platform !== 'linux';
const shouldSkipCPEDTests = !useCPED || unsupportedPlatform;

const PROFILE_OPTIONS = {
  durationMillis: 500,
  intervalMicros: 1000,
};

describe('Time Profiler', () => {
  describe('profile', () => {
    it('should exclude program and idle time', async () => {
      const profile = await time.profile(PROFILE_OPTIONS);
      assert.ok(profile.stringTable);
      assert.equal(profile.stringTable.strings!.indexOf('(program)'), -1);
    });

    it('should update state', function shouldUpdateState() {
      if (unsupportedPlatform) {
        this.skip();
      }
      const startTime = BigInt(Date.now()) * 1000n;
      time.start({
        intervalMicros: 20 * 1_000,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        lineNumbers: false,
        useCPED,
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

      let endTime = 0n;
      time.stop(false, ({node, context}: GenerateTimeLabelsArgs) => {
        if (node.name === time.constants.NON_JS_THREADS_FUNCTION_NAME) {
          return {};
        }
        assert.ok(context !== null, 'Context should not be null');
        if (!endTime) {
          endTime = BigInt(Date.now()) * 1000n;
        }

        assert.deepEqual(
          context!.context,
          initialContext,
          'Unexpected context',
        );

        assert.ok(context!.timestamp >= startTime);
        assert.ok(context!.timestamp <= endTime);
        checked = true;
        return {...context!.context};
      });
      assert(checked, 'No context found');
    });

    it('should have labels', function shouldHaveLabels() {
      if (unsupportedPlatform) {
        this.skip();
      }
      this.timeout(3000);

      const intervalNanos = PROFILE_OPTIONS.intervalMicros * 1_000;
      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        collectAsyncId: collectAsyncId,
        lineNumbers: false,
        useCPED,
      });
      // By repeating the test few times, we also exercise the profiler
      // start-stop overlap behavior.
      const repeats = 3;
      const rootSpanId = '1234';
      const endPointLabel = 'trace endpoint';
      const rootSpanIdLabel = 'local root span id';
      const asyncIdLabel = 'async id';
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
            enableEndPoint || collectAsyncId ? generateLabels : undefined,
          ),
        );
      }

      function generateLabels({context}: GenerateTimeLabelsArgs) {
        if (!context) {
          return {};
        }
        const labels: LabelSet = {};
        if (typeof context.asyncId !== 'undefined') {
          assert(collectAsyncId);
          labels[asyncIdLabel] = context.asyncId;
        }
        for (const [key, value] of Object.entries(context.context ?? {})) {
          if (typeof value === 'string') {
            labels[key] = value;
            if (
              enableEndPoint &&
              key === rootSpanIdLabel &&
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
        // With node 22, many deopt events are generated by `setContext` call above.
        // On MacOS, `v8::TimeTicks::Now` has a resolution of ~42us because
        // `mach_absolute_time` ticks (a tick is ~42ns) conversion to microseconds
        // is done in such a way that drops the 3 least significant digits
        // (https://github.com/nodejs/node/blob/v22.x/deps/v8/src/base/platform/time.cc#L745-L746).
        // This two facts lead to samples having identical timestamps, and
        // incorrectly matched contexts.
        // Workaround here just ensures that after deopt event caused by `setContext`,
        // no sample in `fn1` is immediately taken.
        const start2 = hrtime.bigint();
        while (hrtime.bigint() - start2 < intervalNanos);
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
        const [
          loopIdx,
          fn0Idx,
          fn1Idx,
          fn2Idx,
          hrtimeBigIntIdx,
          asyncIdLabelIdx,
        ] = ['loop', 'fn0', 'fn1', 'fn2', 'hrtimeBigInt', asyncIdLabel].map(x =>
          stringTable.dedup(x),
        );

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
          return label
            ? `${getString(label.key)}=${getString(label.str)}`
            : 'undefined';
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
        let observedAsyncId = false;
        profile.sample.forEach(sample => {
          let fnName;
          for (const locationId of sample.locationId) {
            const locIdx = idx(locationId);
            const loc = profile.location[locIdx];
            const fnIdx = idx(loc.line[0].functionId);
            const fn = profile.function[fnIdx];
            fnName = fn.name;
            if (fnName !== hrtimeBigIntIdx) {
              break;
            }
          }
          const labels = sample.label;
          if (collectAsyncId) {
            const idx = labels.findIndex(
              label => label.key === asyncIdLabelIdx,
            );
            if (idx !== -1) {
              // Remove async ID label so it doesn't confuse the assertions on
              // labels further below.
              labels.splice(idx, 1);
              observedAsyncId = true;
            }
          }
          switch (fnName) {
            case loopIdx:
              if (enableEndPoint) {
                assert(
                  labels.length < 4,
                  'loop can have at most two labels and one endpoint',
                );
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value0') ||
                      labelIs(label, 'label', 'value1') ||
                      labelIs(label, endPointLabel, endPoint) ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'loop can be observed with value0 or value1 or root span id or endpoint',
                  );
                });
              } else {
                assert(labels.length < 3, 'loop can have at most one label');
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value0') ||
                      labelIs(label, 'label', 'value1') ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'loop can be observed with value0 or value1 or root span id',
                  );
                });
              }

              break;
            case fn0Idx:
              assert(
                labels.length < 2,
                `fn0 can have at most one label, instead got: ${labels.map(
                  labelStr,
                )}`,
              );
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
                  'fn1 must be observed with a label, a root span id and an endpoint',
                );
                const labelMap = getLabels(labels);
                assert.deepEqual(labelMap, {
                  ...label1,
                  [endPointLabel]: endPoint,
                });
              } else {
                assert(
                  labels.length === 2,
                  'fn1 must be observed with a label',
                );
                labels.forEach(label => {
                  assert(
                    labelIs(label, 'label', 'value1') ||
                      labelIs(label, rootSpanIdLabel, rootSpanId),
                    'Only value1 can be observed with fn1',
                  );
                });
              }
              fn1ObservedWithLabel1 = true;
              break;
            case fn2Idx:
              assert(
                labels.length === 0,
                'fn2 must be observed with no labels. Observed instead with ' +
                  labelStr(labels[0]),
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
          'fn2 was not observed without a label',
        );
        assert(!collectAsyncId || observedAsyncId, 'Async ID was not observed');
      }
    });
  });

  it('should have async IDs when enabled', async function shouldCollectAsyncIDs() {
    if (!(collectAsyncId && ['darwin', 'linux'].includes(process.platform))) {
      this.skip();
    }
    this.timeout(3000);

    time.start({
      intervalMicros: PROFILE_OPTIONS.intervalMicros,
      durationMillis: PROFILE_OPTIONS.durationMillis,
      withContexts: true,
      lineNumbers: false,
      collectAsyncId: true,
    });
    let setDone: () => void;
    const done = new Promise<void>(resolve => {
      setDone = resolve;
    });

    const testStart = hrtime.bigint();
    const testDurationNanos = PROFILE_OPTIONS.durationMillis * 1_000_000;
    setTimeout(loop, 0);

    function loop() {
      const loopDurationNanos = PROFILE_OPTIONS.intervalMicros * 1_000;
      const loopStart = hrtime.bigint();
      while (hrtime.bigint() - loopStart < loopDurationNanos);
      if (hrtime.bigint() - testStart < testDurationNanos) {
        setTimeout(loop, 0);
      } else {
        setDone();
      }
    }

    await done;

    let asyncIdObserved = false;
    time.stop(false, ({context}: GenerateTimeLabelsArgs) => {
      if (!asyncIdObserved && typeof context?.asyncId === 'number') {
        asyncIdObserved = context?.asyncId !== -1;
      }
      return {};
    });
    assert(asyncIdObserved, 'Async ID was not observed');
  });

  describe('profile (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    const timeProfilerStub = {
      start: sinon.stub(),
      stop: sinon.stub().returns(v8TimeProfile),
      dispose: sinon.stub(),
      v8ProfilerStuckEventLoopDetected: sinon.stub().returns(0),
    };

    before(() => {
      sinonStubs.push(
        sinon.stub(v8TimeProfiler, 'TimeProfiler').returns(timeProfilerStub),
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
      void time.profile(PROFILE_OPTIONS).then(() => {
        isProfiling = false;
      });
      await setTimeoutPromise(2 * PROFILE_OPTIONS.durationMillis);
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
      assert.equal(
        time.v8ProfilerStuckEventLoopDetected(),
        0,
        'v8 bug detected',
      );

      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stop);

      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stop.resetHistory();

      assert.deepEqual(timeProfile, time.stop());

      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stop);
    });
  });

  describe('profileV2', () => {
    it('should exclude program and idle time', async () => {
      const profile = await time.profileV2(PROFILE_OPTIONS);
      assert.ok(profile.stringTable);
      assert.equal(profile.stringTable.strings!.indexOf('(program)'), -1);
    });

    it('should preserve line-number root children metadata in lazy view', function () {
      if (unsupportedPlatform) {
        this.skip();
      }

      function hotPath() {
        const end = hrtime.bigint() + 2_000_000n;
        while (hrtime.bigint() < end);
      }

      const profiler = new v8TimeProfiler.TimeProfiler({
        intervalMicros: 100,
        durationMillis: 200,
        lineNumbers: true,
        withContexts: false,
        workaroundV8Bug: false,
        collectCpuTime: false,
        collectAsyncId: false,
        useCPED: false,
        isMainThread: true,
      });

      profiler.start();
      try {
        const deadline = Date.now() + 200;
        while (Date.now() < deadline) {
          hotPath();
        }

        let sawRootChildren = false;
        let sawChildWithNonRootMetadata = false;

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        profiler.stopAndCollect(false, (profile: any) => {
          const root = profile.topDownRoot as {
            name: string;
            scriptName: string;
            scriptId: number;
            children: Array<{
              name: string;
              scriptName: string;
              scriptId: number;
            }>;
          };
          const children = root.children;

          sawRootChildren = children.length > 0;
          sawChildWithNonRootMetadata = children.some(
            child =>
              child.name !== root.name ||
              child.scriptName !== root.scriptName ||
              child.scriptId !== root.scriptId,
          );
          return undefined;
        });

        assert(sawRootChildren, 'Expected root to have children');
        assert(
          sawChildWithNonRootMetadata,
          'Line-number lazy root children should not collapse to root metadata',
        );
      } finally {
        profiler.dispose();
      }
    });
  });

  describe('profileV2 (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    const timeProfilerStub = {
      start: sinon.stub(),
      // stopAndCollect invokes the callback synchronously with the raw profile,
      // mirroring what the native binding does.
      stopAndCollect: sinon
        .stub()
        .callsFake(
          (_restart: boolean, cb: (p: typeof v8TimeProfile) => unknown) =>
            cb(v8TimeProfile),
        ),
      dispose: sinon.stub(),
      v8ProfilerStuckEventLoopDetected: sinon.stub().returns(0),
    };

    before(() => {
      sinonStubs.push(
        sinon.stub(v8TimeProfiler, 'TimeProfiler').returns(timeProfilerStub),
      );
      sinonStubs.push(sinon.stub(Date, 'now').returns(0));
    });

    after(() => {
      sinonStubs.forEach(stub => stub.restore());
    });

    it('should profile during duration and finish profiling after duration', async () => {
      let isProfiling = true;
      void profileV2(PROFILE_OPTIONS).then(() => {
        isProfiling = false;
      });
      await setTimeoutPromise(2 * PROFILE_OPTIONS.durationMillis);
      assert.strictEqual(false, isProfiling, 'profiler is still running');
    });

    it('should return a profile equal to the expected profile', async () => {
      const profile = await profileV2(PROFILE_OPTIONS);
      assert.deepEqual(timeProfile, profile);
    });

    it('should be able to restart when stopping', async () => {
      time.start({intervalMicros: PROFILE_OPTIONS.intervalMicros});
      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stopAndCollect.resetHistory();

      assert.deepEqual(timeProfile, stopV2(true));
      assert.equal(
        time.v8ProfilerStuckEventLoopDetected(),
        0,
        'v8 bug detected',
      );
      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stopAndCollect);

      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stopAndCollect.resetHistory();

      assert.deepEqual(timeProfile, stopV2());
      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stopAndCollect);
    });
  });

  describe('v8BugWorkaround (w/ stubs)', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const sinonStubs: Array<sinon.SinonStub<any, any>> = [];
    const timeProfilerStub = {
      start: sinon.stub(),
      stop: sinon.stub().returns(v8TimeProfile),
      dispose: sinon.stub(),
      v8ProfilerStuckEventLoopDetected: sinon.stub().returns(2),
    };

    before(() => {
      sinonStubs.push(
        sinon.stub(v8TimeProfiler, 'TimeProfiler').returns(timeProfilerStub),
      );
      sinonStubs.push(sinon.stub(Date, 'now').returns(0));
    });

    after(() => {
      sinonStubs.forEach(stub => {
        stub.restore();
      });
    });

    it('should reset profiler when empty profile is returned and restart is requested', () => {
      time.start(PROFILE_OPTIONS);
      time.stop(true);
      sinon.assert.calledTwice(timeProfilerStub.start);
      sinon.assert.calledTwice(timeProfilerStub.stop);

      assert.equal(
        time.v8ProfilerStuckEventLoopDetected(),
        2,
        'v8 bug not detected',
      );
      timeProfilerStub.start.resetHistory();
      timeProfilerStub.stop.resetHistory();

      time.stop(false);
      sinon.assert.notCalled(timeProfilerStub.start);
      sinon.assert.calledOnce(timeProfilerStub.stop);
    });
  });

  describe('lowCardinalityLabels', () => {
    it('should handle lowCardinalityLabels parameter in stop function', async function testLowCardinalityLabels() {
      if (unsupportedPlatform) {
        this.skip();
      }
      this.timeout(3000);

      // Set up some contexts with labels that we'll mark as low cardinality
      const lowCardLabel = 'service_name';
      const highCardLabel = 'trace_id';
      const lowCardValues = ['web-service', 'api-service']; // Low cardinality values
      const context1 = {
        [lowCardLabel]: lowCardValues[0],
        [highCardLabel]: '12345',
      };
      const context2 = {
        [lowCardLabel]: lowCardValues[1],
        [highCardLabel]: '67890',
      };
      const context3 = {
        [lowCardLabel]: lowCardValues[0],
        [highCardLabel]: '54321',
      }; // Reuse low card value

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        lineNumbers: false,
        useCPED,
      });

      // Run busy loop with context switching for profile duration
      const profileStart = Date.now();
      let iterationCount = 0;

      while (Date.now() - profileStart < PROFILE_OPTIONS.durationMillis) {
        const start = hrtime.bigint();
        const durationNanos = PROFILE_OPTIONS.intervalMicros * 1000;
        while (hrtime.bigint() - start < durationNanos) {
          // Busy loop
        }

        // Cycle through different contexts
        const contexts = [context1, context2, context3];
        time.setContext(contexts[iterationCount % contexts.length]);
        iterationCount++;

        // Allow other tasks to run
        await new Promise(resolve => setImmediate(resolve));
      }

      let labelsCollected = false;
      const lowCardinalityArray = [lowCardLabel];

      const generateLabelsFunc = ({context}: GenerateTimeLabelsArgs) => {
        if (!context) {
          return {};
        }
        labelsCollected = true;
        // Generate labels from context
        const labels: LabelSet = {};
        for (const [key, value] of Object.entries(context.context ?? {})) {
          if (typeof value === 'string') {
            labels[key] = value;
          }
        }
        return labels;
      };

      const profile = time.stop(false, generateLabelsFunc, lowCardinalityArray);

      // Verify that labels were collected and the profile is valid
      assert(labelsCollected, 'Labels should have been collected');
      assert.ok(profile, 'Profile should be generated');
      assert.ok(profile.stringTable, 'Profile should have string table');
      assert(profile.sample.length > 0, 'Profile should have samples');

      // Check that samples have the expected labels and collect low cardinality labels
      let foundLowCardLabel = false;
      let foundHighCardLabel = false;
      const lowCardinalityLabels: Label[] = [];

      profile.sample.forEach(sample => {
        if (sample.label && sample.label.length > 0) {
          sample.label.forEach(label => {
            const keyStr = profile.stringTable.strings[Number(label.key)];
            const valueStr = profile.stringTable.strings[Number(label.str)];

            if (keyStr === lowCardLabel && lowCardValues.includes(valueStr)) {
              foundLowCardLabel = true;
              lowCardinalityLabels.push(label);
            }
            if (keyStr === highCardLabel) {
              foundHighCardLabel = true;
            }
          });
        }
      });

      assert(foundLowCardLabel, 'Should find low cardinality label in samples');
      assert(
        foundHighCardLabel,
        'Should find high cardinality label in samples',
      );

      // Verify that the lowCardinalityLabels parameter is working correctly
      // This tests that the stop() function accepts and processes the lowCardinalityLabels parameter

      // Group labels by value and count them
      const labelsByValue = new Map<string, Label[]>();
      lowCardinalityLabels.forEach(label => {
        const valueStr = profile.stringTable.strings[Number(label.str)];
        if (!labelsByValue.has(valueStr)) {
          labelsByValue.set(valueStr, []);
        }
        labelsByValue.get(valueStr)!.push(label);
      });

      // We should have exactly 2 distinct values (web-service and api-service)
      assert(
        labelsByValue.size === 2,
        `Expected exactly 2 distinct low cardinality label values, found ${
          labelsByValue.size
        }. Values: ${Array.from(labelsByValue.keys()).join(', ')}`,
      );

      // Verify we found both expected values
      assert(
        labelsByValue.has('web-service'),
        'Should find web-service labels',
      );
      assert(
        labelsByValue.has('api-service'),
        'Should find api-service labels',
      );

      // Verify that the lowCardinalityLabels parameter was properly used
      // This tests that labels are being processed with the low cardinality configuration
      labelsByValue.forEach((labels, value) => {
        assert(
          labels.length > 0,
          `Should have at least one label with value '${value}'`,
        );

        // Check that all labels have the same key (service_name)
        labels.forEach(label => {
          const keyStr = profile.stringTable.strings[Number(label.key)];
          assert(
            keyStr === lowCardLabel,
            `Expected label key to be '${lowCardLabel}', got '${keyStr}'`,
          );
        });
      });

      // Test that the Set of all low cardinality labels contains exactly 2 unique values
      // This verifies that the lowCardinalityLabels parameter is properly handled
      const allUniqueValues = new Set(
        lowCardinalityLabels.map(
          label => profile.stringTable.strings[Number(label.str)],
        ),
      );
      assert(
        allUniqueValues.size === 2,
        `Expected exactly 2 unique low cardinality label values across all samples, found ${allUniqueValues.size}`,
      );
      assert(
        allUniqueValues.has('web-service') &&
          allUniqueValues.has('api-service'),
        'Should find both web-service and api-service values in the low cardinality labels',
      );

      // Verify that low cardinality labels with the same value are the same object
      // This tests the deduplication behavior as requested by the user
      labelsByValue.forEach((labels, value) => {
        const uniqueObjects = new Set(labels);
        assert(
          uniqueObjects.size === 1,
          `All labels with value '${value}' should be the same object, found ${uniqueObjects.size} different objects. ` +
            'The lowCardinalityLabels parameter should enable deduplication of Label objects with identical key/value pairs.',
        );
      });
    });
  });

  describe('Memory comparison', () => {
    interface WorkerMemoryResult {
      initial: number;
      afterTraversal: number;
      afterHitCount: number;
    }

    function measureMemoryInWorker(
      version: 'v1' | 'v2',
    ): Promise<WorkerMemoryResult> {
      return new Promise((resolve, reject) => {
        const child = fork('./out/test/time-memory-worker.js', [], {
          execArgv: ['--expose-gc'],
        });

        child.on('message', (result: WorkerMemoryResult) => {
          resolve(result);
          child.kill();
        });

        child.on('error', reject);
        child.send(version);
      });
    }

    it('stopAndCollect should use less memory than stop when profile is large', async function () {
      if (unsupportedPlatform) {
        this.skip();
      }

      const v1 = await measureMemoryInWorker('v1');
      const v2 = await measureMemoryInWorker('v2');

      console.log('v1 : ', v1.initial, v1.afterTraversal, v1.afterHitCount);
      console.log('v2 : ', v2.initial, v2.afterTraversal, v2.afterHitCount);

      // V2 creates almost nothing upfront — lazy wrappers vs full eager tree.
      assert.ok(
        v2.initial < v1.initial,
        `V2 initial should be less: V1=${v1.initial}, V2=${v2.initial}`,
      );
    }).timeout(120_000);
  });

  describe('getNativeThreadId', () => {
    it('should return a number', () => {
      const threadId = getNativeThreadId();
      assert.ok(typeof threadId === 'number');
      assert.ok(threadId > 0);
    });
  });

  describe('runWithContext', () => {
    it('should throw when profiler is not started', () => {
      assert.throws(() => {
        time.runWithContext({label: 'test'}, () => {});
      }, /Wall profiler is not started/);
    });

    it('should throw when useCPED is not enabled', function testNoCPED() {
      if (unsupportedPlatform) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: false,
      });

      try {
        assert.throws(() => {
          time.runWithContext({label: 'test'}, () => {});
        }, /Can only use runWithContext with AsyncContextFrame/);
      } finally {
        time.stop();
      }
    });

    it('should run function with context when useCPED is enabled', function testRunWithContext() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const testContext = {label: 'test-value', id: '123'};
        let contextInsideFunction;

        time.runWithContext(testContext, () => {
          contextInsideFunction = time.getContext();
        });

        assert.deepEqual(
          contextInsideFunction,
          testContext,
          'Context should be accessible within function',
        );
      } finally {
        time.stop();
      }
    });

    it('should pass arguments to function correctly', function testArguments() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const testContext = {label: 'test'};
        const result = time.runWithContext(
          testContext,
          (a: number, b: string, c: boolean) => {
            return {a, b, c};
          },
          42,
          'hello',
          true,
        );

        assert.deepEqual(
          result,
          {a: 42, b: 'hello', c: true},
          'Arguments should be passed correctly',
        );
      } finally {
        time.stop();
      }
    });

    it('should return function result', function testReturnValue() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const testContext = {label: 'test'};
        const result = time.runWithContext(testContext, () => {
          return 'test-result';
        });

        assert.strictEqual(
          result,
          'test-result',
          'Function result should be returned',
        );
      } finally {
        time.stop();
      }
    });

    it('should handle nested runWithContext calls', function testNestedCalls() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const outerContext = {label: 'outer'};
        const innerContext = {label: 'inner'};
        const results: string[] = [];

        time.runWithContext(outerContext, () => {
          const ctx1 = time.getContext();
          results.push((ctx1 as Record<string, string>).label);

          time.runWithContext(innerContext, () => {
            const ctx2 = time.getContext();
            results.push((ctx2 as Record<string, string>).label);
          });

          const ctx3 = time.getContext();
          results.push((ctx3 as Record<string, string>).label);
        });

        assert.deepEqual(
          results,
          ['outer', 'inner', 'outer'],
          'Nested contexts should be properly isolated and restored',
        );
      } finally {
        time.stop();
      }
    });

    it('should isolate context from outside runWithContext', function testContextIsolation() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const runWithContextContext = {label: 'inside'};
        let contextInside;

        time.runWithContext(runWithContextContext, () => {
          contextInside = time.getContext();
        });

        // Context outside runWithContext should be undefined since we're using CPED
        const contextOutside = time.getContext();

        assert.deepEqual(
          contextInside,
          runWithContextContext,
          'Context inside should match',
        );
        assert.strictEqual(
          contextOutside,
          undefined,
          'Context outside should be undefined with CPED',
        );
      } finally {
        time.stop();
      }
    });

    it('should work with async functions', async function testAsyncFunction() {
      if (shouldSkipCPEDTests) {
        this.skip();
      }

      time.start({
        intervalMicros: PROFILE_OPTIONS.intervalMicros,
        durationMillis: PROFILE_OPTIONS.durationMillis,
        withContexts: true,
        useCPED: true,
      });

      try {
        const testContext = {label: 'async-test'};

        const result = await time.runWithContext(testContext, async () => {
          const ctx1 = time.getContext();
          await setTimeoutPromise(10);
          const ctx2 = time.getContext();
          return {ctx1, ctx2};
        });

        assert.deepEqual(
          result.ctx1,
          testContext,
          'Context should be available before await',
        );
        assert.deepEqual(
          result.ctx2,
          testContext,
          'Context should be preserved after await',
        );
      } finally {
        time.stop();
      }
    });
  });
});
