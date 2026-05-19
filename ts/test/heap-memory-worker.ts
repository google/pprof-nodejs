import * as heapProfiler from '../src/heap-profiler';
import * as v8HeapProfiler from '../src/heap-profiler-bindings';
import {AllocationProfileNode} from '../src/v8-types';

const gc = (global as NodeJS.Global & {gc?: () => void}).gc;
if (!gc) {
  throw new Error('Run with --expose-gc flag');
}

const keepAlive: object[] = [];

// Create many unique functions via new Function() to produce a large profile tree.
function createAllocatorFunctions(count: number): Array<() => void> {
  const fns: Array<() => void> = [];
  for (let i = 0; i < count; i++) {
    const fn = new Function(
      'keepAlive',
      `
      for (let j = 0; j < 100; j++) {
        keepAlive.push({
          id${i}: j,
          data${i}: new Array(64).fill('${'x'.repeat(16)}'),
        });
      }
    `,
    ) as (arr: object[]) => void;
    fns.push(() => fn(keepAlive));
  }
  return fns;
}

function createDeepChain(depth: number): Array<(arr: object[]) => void> {
  const fns: Array<(arr: object[]) => void> = [];
  for (let i = depth - 1; i >= 0; i--) {
    const next = i < depth - 1 ? fns[fns.length - 1] : null;
    const fn = new Function(
      'keepAlive',
      'next',
      `
      for (let j = 0; j < 50; j++) {
        keepAlive.push({ arr${i}: new Array(32).fill(j) });
      }
      if (next) next(keepAlive, null);
    `,
    ) as (arr: object[], next: unknown) => void;
    fns.push((arr: object[]) => fn(arr, next));
  }
  return fns;
}

function generateAllocations(): void {
  const wideFns = createAllocatorFunctions(5000);
  for (const fn of wideFns) {
    fn();
  }

  for (let chain = 0; chain < 200; chain++) {
    const deepFns = createDeepChain(50);
    deepFns[deepFns.length - 1](keepAlive);
  }
}

function traverseTree(root: AllocationProfileNode): void {
  const stack: AllocationProfileNode[] = [root];
  while (stack.length > 0) {
    const node = stack.pop()!;
    if (node.children) {
      for (const child of node.children) {
        stack.push(child);
      }
    }
  }
}

function measureV1(): {initial: number; afterTraversal: number} {
  gc!();
  gc!();
  const baseline = process.memoryUsage().heapUsed;

  const profile = v8HeapProfiler.getAllocationProfile();
  const initial = process.memoryUsage().heapUsed - baseline;
  traverseTree(profile);
  const afterTraversal = process.memoryUsage().heapUsed - baseline;

  return {initial, afterTraversal};
}

function measureV2(): {initial: number; afterTraversal: number} {
  gc!();
  gc!();
  const baseline = process.memoryUsage().heapUsed;

  return v8HeapProfiler.mapAllocationProfile(root => {
    const initial = process.memoryUsage().heapUsed - baseline;
    traverseTree(root);
    const afterTraversal = process.memoryUsage().heapUsed - baseline;
    return {initial, afterTraversal};
  });
}

process.on('message', (version: 'v1' | 'v2') => {
  heapProfiler.start(128, 128);
  generateAllocations();

  const {initial, afterTraversal} =
    version === 'v1' ? measureV1() : measureV2();

  heapProfiler.stop();
  keepAlive.length = 0;

  process.send!({initial, afterTraversal});
});
