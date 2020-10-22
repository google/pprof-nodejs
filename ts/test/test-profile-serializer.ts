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
  serializeHeapProfile,
  serializeTimeProfile,
} from '../src/profile-serializer';
import {SourceMapper} from '../src/sourcemapper/sourcemapper';

import {
  anonymousFunctionHeapProfile,
  anonymousFunctionTimeProfile,
  heapProfile,
  heapSourceProfile,
  mapDirPath,
  timeProfile,
  timeSourceProfile,
  v8AnonymousFunctionHeapProfile,
  v8AnonymousFunctionTimeProfile,
  v8HeapGeneratedProfile,
  v8HeapProfile,
  v8TimeGeneratedProfile,
  v8TimeProfile,
} from './profiles-for-tests';

const assert = require('assert');

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
    it('should produce expected profile when there is anyonmous function', () => {
      const timeProfileOut = serializeTimeProfile(
        v8AnonymousFunctionTimeProfile,
        1000
      );
      assert.deepEqual(timeProfileOut, anonymousFunctionTimeProfile);
    });
  });

  describe('serializeHeapProfile', () => {
    it('should produce expected profile', () => {
      const heapProfileOut = serializeHeapProfile(v8HeapProfile, 0, 512 * 1024);
      assert.deepEqual(heapProfileOut, heapProfile);
    });
    it('should produce expected profile when there is anyonmous function', () => {
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
