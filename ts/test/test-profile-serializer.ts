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
import * as tmp from 'tmp';

import {
  NON_JS_THREADS_FUNCTION_NAME,
  serializeHeapProfile,
  serializeTimeProfile,
} from '../src/profile-serializer';
import {SourceMapper} from '../src/sourcemapper/sourcemapper';
import {Label, Profile} from 'pprof-format';
import {TimeProfile} from '../src/v8-types';
import {
  anonymousFunctionHeapProfile,
  getAndVerifyPresence,
  heapProfile,
  heapSourceProfile,
  labelEncodingProfile,
  mapDirPath,
  timeProfile,
  timeSourceProfile,
  v8AnonymousFunctionHeapProfile,
  v8HeapGeneratedProfile,
  v8HeapProfile,
  v8TimeGeneratedProfile,
  v8TimeProfile,
} from './profiles-for-tests';

const assert = require('assert');

function getNonJSThreadsSample(profile: Profile): Number[] | null {
  for (const sample of profile.sample!) {
    const locationId = sample.locationId[0];
    const location = getAndVerifyPresence(
      profile.location!,
      locationId as number
    );
    const functionId = location.line![0].functionId;
    const fn = getAndVerifyPresence(profile.function!, functionId as number);
    const fn_name = profile.stringTable.strings[fn.name as number];
    if (fn_name === NON_JS_THREADS_FUNCTION_NAME) {
      return sample.value as Number[];
    }
  }

  return null;
}

describe('profile-serializer', () => {
  let dateStub: sinon.SinonStub<[], number>;

  before(() => {
    dateStub = sinon.stub(Date, 'now').returns(0);
  });
  after(() => {
    dateStub.restore();
  });

  describe('serializeTimeProfile', () => {
    it('should produce expected profile', () => {
      const timeProfileOut = serializeTimeProfile(v8TimeProfile, 1000);
      assert.deepEqual(timeProfileOut, timeProfile);
    });

    it('should omit non-jS threads CPU time when profile has no CPU time', () => {
      const timeProfile: TimeProfile = {
        startTime: 0,
        endTime: 10 * 1000 * 1000,
        hasCpuTime: false,
        nonJSThreadsCpuTime: 1000,
        topDownRoot: {
          name: '(root)',
          scriptName: 'root',
          scriptId: 0,
          lineNumber: 0,
          columnNumber: 0,
          hitCount: 0,
          children: [],
        },
      };
      const timeProfileOut = serializeTimeProfile(timeProfile, 1000);
      assert.equal(getNonJSThreadsSample(timeProfileOut), null);
      const timeProfileOutWithLabels = serializeTimeProfile(
        timeProfile,
        1000,
        undefined,
        false,
        () => {
          return {foo: 'bar'};
        }
      );
      assert.equal(getNonJSThreadsSample(timeProfileOutWithLabels), null);
    });

    it('should omit non-jS threads CPU time when it is zero', () => {
      const timeProfile: TimeProfile = {
        startTime: 0,
        endTime: 10 * 1000 * 1000,
        hasCpuTime: true,
        nonJSThreadsCpuTime: 0,
        topDownRoot: {
          name: '(root)',
          scriptName: 'root',
          scriptId: 0,
          lineNumber: 0,
          columnNumber: 0,
          hitCount: 0,
          children: [],
        },
      };
      const timeProfileOut = serializeTimeProfile(timeProfile, 1000);
      assert.equal(getNonJSThreadsSample(timeProfileOut), null);
      const timeProfileOutWithLabels = serializeTimeProfile(
        timeProfile,
        1000,
        undefined,
        false,
        () => {
          return {foo: 'bar'};
        }
      );
      assert.equal(getNonJSThreadsSample(timeProfileOutWithLabels), null);
    });

    it('should produce Non-JS thread sample with zero wall time', () => {
      const timeProfile: TimeProfile = {
        startTime: 0,
        endTime: 10 * 1000 * 1000,
        hasCpuTime: true,
        nonJSThreadsCpuTime: 1000,
        topDownRoot: {
          name: '(root)',
          scriptName: 'root',
          scriptId: 0,
          lineNumber: 0,
          columnNumber: 0,
          hitCount: 0,
          children: [],
        },
      };
      const timeProfileOut = serializeTimeProfile(timeProfile, 1000);
      const values = getNonJSThreadsSample(timeProfileOut);
      assert.notEqual(values, null);
      assert.equal(values![0], 0);
      assert.equal(values![1], 0);
      assert.equal(values![2], 1000);
      const timeProfileOutWithLabels = serializeTimeProfile(
        timeProfile,
        1000,
        undefined,
        false,
        () => {
          return {foo: 'bar'};
        }
      );
      const valuesWithLabels = getNonJSThreadsSample(timeProfileOutWithLabels);
      assert.notEqual(valuesWithLabels, null);
      assert.equal(valuesWithLabels![0], 0);
      assert.equal(valuesWithLabels![1], 0);
      assert.equal(valuesWithLabels![2], 1000);
    });
  });

  describe('label builder', () => {
    it('should accept strings, numbers, and bigints', () => {
      const profileOut = serializeTimeProfile(labelEncodingProfile, 1000);
      const st = profileOut.stringTable;
      assert.deepEqual(profileOut.sample[0].label, [
        new Label({key: st.dedup('someStr'), str: st.dedup('foo')}),
        new Label({key: st.dedup('someNum'), num: 42}),
        new Label({key: st.dedup('someBigint'), num: 18446744073709551557n}),
      ]);
    });
  });

  describe('serializeHeapProfile', () => {
    it('should produce expected profile', () => {
      const heapProfileOut = serializeHeapProfile(v8HeapProfile, 0, 512 * 1024);
      assert.deepEqual(heapProfileOut, heapProfile);
    });
    it('should produce expected profile when there is anonymous function', () => {
      const heapProfileOut = serializeHeapProfile(
        v8AnonymousFunctionHeapProfile,
        0,
        512 * 1024
      );
      assert.deepEqual(heapProfileOut, anonymousFunctionHeapProfile);
    });
  });

  describe('source map specified', () => {
    let sourceMapper: SourceMapper;
    before(async () => {
      const sourceMapFiles = [mapDirPath];
      sourceMapper = await SourceMapper.create(sourceMapFiles);
    });

    describe('serializeHeapProfile', () => {
      it('should produce expected profile', () => {
        const heapProfileOut = serializeHeapProfile(
          v8HeapGeneratedProfile,
          0,
          512 * 1024,
          undefined,
          sourceMapper
        );
        assert.deepEqual(heapProfileOut, heapSourceProfile);
      });
    });

    describe('serializeTimeProfile', () => {
      it('should produce expected profile', () => {
        const timeProfileOut = serializeTimeProfile(
          v8TimeGeneratedProfile,
          1000,
          sourceMapper
        );
        assert.deepEqual(timeProfileOut, timeSourceProfile);
      });
    });

    after(() => {
      tmp.setGracefulCleanup();
    });
  });
});
