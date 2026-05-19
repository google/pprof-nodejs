import {TimeProfiler} from '../src/time-profiler-bindings';
import {ProfileNode, TimeProfile, TimeProfileNode} from '../src/v8-types';
import {computeTotalHitCount} from '../src/profile-serializer';

const gc = (global as NodeJS.Global & {gc?: () => void}).gc;
if (!gc) {
  throw new Error('Run with --expose-gc flag');
}

const SCRIPT_PADDING = 'a'.repeat(250);

function createUniqueFunctions(count: number): Array<() => void> {
  const fns: Array<() => void> = [];
  for (let i = 0; i < count; i++) {
    const fn = new Function(
      `//# sourceURL=wide_fn_${i}_${SCRIPT_PADDING}.js\n` +
        `var x${i}=0,e${i}=Date.now()+1;while(Date.now()<e${i}){x${i}++;}`,
    ) as () => void;
    fns.push(fn);
  }
  return fns;
}

function createDeepCallChain(chainId: number, depth: number): () => void {
  let innermost: (() => void) | null = null;
  for (let i = depth - 1; i >= 0; i--) {
    const next = innermost;
    innermost = new Function(
      'next',
      `//# sourceURL=chain_${chainId}_d${i}_${SCRIPT_PADDING}.js\n` +
        'var c=0,e=Date.now()+1;while(Date.now()<e){c++;} if(next)next();',
    ).bind(null, next) as () => void;
  }
  return innermost!;
}

const CHAIN_STRIDE = 30;

function generateCpuWork(
  wideFns: Array<() => void>,
  deepChains: Array<() => void>,
  durationMs: number,
): void {
  const deadline = Date.now() + durationMs;
  let i = 0;
  while (Date.now() < deadline) {
    wideFns[i % wideFns.length]();
    if (i % CHAIN_STRIDE === 0) {
      deepChains[(i / CHAIN_STRIDE) % deepChains.length]();
    }
    i++;
  }
}

const WIDE_FN_COUNT = 5000;
const CHAIN_COUNT = 100;
const CHAIN_DEPTH = 60;

const PROFILER_OPTIONS = {
  intervalMicros: 50,
  durationMillis: 20_000,
  lineNumbers: true,
  withContexts: false,
  workaroundV8Bug: false,
  collectCpuTime: false,
  collectAsyncId: false,
  useCPED: false,
  isMainThread: true,
};

function buildWorkload() {
  const wideFns = createUniqueFunctions(WIDE_FN_COUNT);
  const deepChains: Array<() => void> = [];
  for (let c = 0; c < CHAIN_COUNT; c++) {
    deepChains.push(createDeepCallChain(c, CHAIN_DEPTH));
  }
  return {wideFns, deepChains};
}

function traverseTree(root: TimeProfileNode): void {
  const stack: ProfileNode[] = [root];
  while (stack.length > 0) {
    const node = stack.pop()!;
    for (const child of node.children) {
      stack.push(child);
    }
  }
}

interface MemoryResult {
  initial: number;
  afterTraversal: number;
  afterHitCount: number;
}

function measureV1(): MemoryResult {
  const {wideFns, deepChains} = buildWorkload();
  const profiler = new TimeProfiler(PROFILER_OPTIONS);
  profiler.start();
  generateCpuWork(wideFns, deepChains, PROFILER_OPTIONS.durationMillis);

  gc!();
  const baseline = process.memoryUsage().heapUsed;

  const profile: TimeProfile = profiler.stop(false);
  const initial = process.memoryUsage().heapUsed - baseline;

  traverseTree(profile.topDownRoot);
  const afterTraversal = process.memoryUsage().heapUsed - baseline;

  // V1: computeTotalHitCount triggers children getters on every node,
  // creating JS wrapper objects for a second full tree traversal.
  computeTotalHitCount(profile.topDownRoot);
  const afterHitCount = process.memoryUsage().heapUsed - baseline;

  profiler.dispose();
  return {initial, afterTraversal, afterHitCount};
}

function measureV2(): MemoryResult {
  const {wideFns, deepChains} = buildWorkload();
  const profiler = new TimeProfiler(PROFILER_OPTIONS);
  profiler.start();
  generateCpuWork(wideFns, deepChains, PROFILER_OPTIONS.durationMillis);

  gc!();
  const baseline = process.memoryUsage().heapUsed;

  const result = profiler.stopAndCollect(
    false,
    (profile: TimeProfile): MemoryResult => {
      const initial = process.memoryUsage().heapUsed - baseline;

      traverseTree(profile.topDownRoot);
      const afterTraversal = process.memoryUsage().heapUsed - baseline;

      // V2: totalHitCount is pre-computed in C++ — just a property read,
      // no JS tree traversal, no wrapper objects created.
      void profile.totalHitCount;
      const afterHitCount = process.memoryUsage().heapUsed - baseline;

      return {initial, afterTraversal, afterHitCount};
    },
  );

  profiler.dispose();
  return result;
}

process.on('message', (version: 'v1' | 'v2') => {
  const result = version === 'v1' ? measureV1() : measureV2();
  process.send!(result);
});
