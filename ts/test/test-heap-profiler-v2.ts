import {strict as assert} from 'assert';
import {fork} from 'child_process';
import * as heapProfiler from '../src/heap-profiler';
import * as v8HeapProfiler from '../src/heap-profiler-bindings';

function generateAllocations(): object[] {
  const allocations: object[] = [];
  for (let i = 0; i < 1000; i++) {
    allocations.push({data: new Array(100).fill(i)});
  }
  return allocations;
}

describe('HeapProfiler V2 API', () => {
  let keepAlive: object[] = [];

  before(() => {
    heapProfiler.start(512, 64);
    keepAlive = generateAllocations();
  });

  after(() => {
    heapProfiler.stop();
    keepAlive.length = 0;
  });

  describe('v8ProfileV2', () => {
    it('should return AllocationProfileNode', () => {
      heapProfiler.v8ProfileV2(root => {
        assert.equal(typeof root.name, 'string');
        assert.equal(typeof root.scriptName, 'string');
        assert.equal(typeof root.scriptId, 'number');
        assert.equal(typeof root.lineNumber, 'number');
        assert.equal(typeof root.columnNumber, 'number');
        assert.ok(Array.isArray(root.allocations));

        assert.ok(Array.isArray(root.children));
        assert.equal(typeof root.children.length, 'number');

        if (root.children.length > 0) {
          const child = root.children[0];
          assert.equal(typeof child.name, 'string');
          assert.ok(Array.isArray(child.children));
          assert.ok(Array.isArray(child.allocations));
        }
      });
    });

    it('should throw error when profiler not started', () => {
      heapProfiler.stop();
      assert.throws(
        () => {
          heapProfiler.v8ProfileV2(() => {});
        },
        (err: Error) => {
          return err.message === 'Heap profiler is not enabled.';
        },
      );
      heapProfiler.start(512, 64);
    });
  });

  describe('mapAllocationProfile', () => {
    it('should return AllocationProfileNode directly', () => {
      v8HeapProfiler.mapAllocationProfile(root => {
        assert.equal(typeof root.name, 'string');
        assert.equal(typeof root.scriptName, 'string');
        assert.ok(Array.isArray(root.children));
        assert.ok(Array.isArray(root.allocations));
      });
    });
  });

  describe('profileV2', () => {
    it('should produce valid pprof Profile', () => {
      const profile = heapProfiler.profileV2();

      assert.ok(profile.sampleType);
      assert.ok(profile.sample);
      assert.ok(profile.location);
      assert.ok(profile.function);
      assert.ok(profile.stringTable);
    });
  });

  describe('Memory comparison', () => {
    interface MemoryResult {
      initial: number;
      afterTraversal: number;
    }

    function measureMemoryInWorker(
      version: 'v1' | 'v2',
    ): Promise<MemoryResult> {
      return new Promise((resolve, reject) => {
        const child = fork('./out/test/heap-memory-worker.js', [], {
          execArgv: ['--expose-gc'],
        });

        child.on('message', (result: MemoryResult) => {
          resolve(result);
          child.kill();
        });

        child.on('error', reject);
        child.send(version);
      });
    }

    it('mapAllocationProfile should use less initial memory than getAllocationProfile', async () => {
      const v1MemoryUsage = await measureMemoryInWorker('v1');
      const v2MemoryUsage = await measureMemoryInWorker('v2');

      console.log(
        ` V1 initial: ${v1MemoryUsage.initial}, afterTraversal: ${v1MemoryUsage.afterTraversal} 
        | V2 initial: ${v2MemoryUsage.initial}, afterTraversal: ${v2MemoryUsage.afterTraversal}`,
      );

      assert.ok(
        v2MemoryUsage.initial < v1MemoryUsage.initial,
        `V2 initial should be less: V1=${v1MemoryUsage.initial}, V2=${v2MemoryUsage.initial}`,
      );

      assert.ok(
        v2MemoryUsage.afterTraversal < v1MemoryUsage.afterTraversal,
        `V2 afterTraversal should be less: V1=${v1MemoryUsage.afterTraversal}, V2=${v2MemoryUsage.afterTraversal}`,
      );
    }).timeout(100_000);
  });
});
