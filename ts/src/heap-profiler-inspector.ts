import * as inspector from 'node:inspector';
import {AllocationProfileNode, Allocation} from './v8-types';

const session = new inspector.Session();

export interface SamplingHeapProfileSample {
  size: number;
  nodeId: number;
  ordinal: number;
}

export interface SamplingHeapProfileNode {
  callFrame: inspector.Runtime.CallFrame;
  selfSize: number;
  id: number;
  children: SamplingHeapProfileNode[];
}

/**
 * Need to create this interface since the type definitions file for node inspector
 * at https://github.com/DefinitelyTyped/DefinitelyTyped/blob/master/types/node/inspector.d.ts
 * has not been updated with the latest changes yet.
 *
 * The types defined through this interface are in sync with the documentation found at -
 * https://chromedevtools.github.io/devtools-protocol/v8/HeapProfiler/
 */
export interface CompatibleSamplingHeapProfile {
  head: SamplingHeapProfileNode;
  samples: SamplingHeapProfileSample[];
}

export function startSamplingHeapProfiler(
  heapIntervalBytes: number,
  stackDepth: number
): Promise<void> {
  session.connect();
  return new Promise<void>((resolve, reject) => {
    session.post(
      'HeapProfiler.startSampling',
      {heapIntervalBytes},
      (err: Error | null): void => {
        if (err !== null) {
          console.error(`Error starting heap sampling: ${err}`);
          reject(err);
          return;
        }
        console.log(
          `Started Heap Sampling with interval bytes ${heapIntervalBytes}`
        );
        resolve();
      }
    );
  });
}

/**
 * Stops the sampling heap profile and discards the current profile.
 */
export function stopSamplingHeapProfiler(): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    session.post(
      'HeapProfiler.stopSampling',
      (
        err: Error | null,
        profile: inspector.HeapProfiler.StopSamplingReturnType
      ) => {
        if (err !== null) {
          console.error(`Error stopping heap sampling ${err}`);
          reject(err);
          return;
        }
        console.log(
          `Stopped sampling heap, discarding current profile: ${profile}`
        );
        session.disconnect();
        console.log('Disconnected from current profiling session');
        resolve();
      }
    );
  });
}

export async function getAllocationProfile(): Promise<AllocationProfileNode> {
  return new Promise<AllocationProfileNode>((resolve, reject) => {
    session.post(
      'HeapProfiler.getSamplingProfile',
      (
        err: Error | null,
        result: inspector.HeapProfiler.GetSamplingProfileReturnType
      ) => {
        if (err !== null) {
          console.error(`Error getting sampling profile ${err}`);
          reject(err);
          return;
        }
        const compatibleHeapProfile =
          result.profile as CompatibleSamplingHeapProfile;
        resolve(
          translateToAllocationProfileNode(
            compatibleHeapProfile.head,
            compatibleHeapProfile.samples
          )
        );
      }
    );
  });
}

function translateToAllocationProfileNode(
  node: SamplingHeapProfileNode,
  samples: SamplingHeapProfileSample[]
): AllocationProfileNode {
  const allocationProfileNode: AllocationProfileNode = {
    allocations: [],
    name: node.callFrame.functionName,
    scriptName: node.callFrame.url,
    scriptId: Number(node.callFrame.scriptId),
    lineNumber: node.callFrame.lineNumber,
    columnNumber: node.callFrame.columnNumber,
    children: [],
  };

  const children: AllocationProfileNode[] = new Array<AllocationProfileNode>(
    node.children.length
  );
  for (let i = 0; i < node.children.length; i++) {
    children.splice(
      i,
      1,
      translateToAllocationProfileNode(node.children[i], samples)
    );
  }
  allocationProfileNode.children = children;

  // find all samples belonging to this node Id
  const samplesForCurrentNodeId: SamplingHeapProfileSample[] =
    filterSamplesBasedOnNodeId(node.id, samples);
  const mappedAllocationsForNodeId: Allocation[] =
    createAllocationsFromSamplesForNode(samplesForCurrentNodeId);

  allocationProfileNode.allocations = mappedAllocationsForNodeId;
  return allocationProfileNode;
}

function filterSamplesBasedOnNodeId(
  nodeId: number,
  samples: SamplingHeapProfileSample[]
): SamplingHeapProfileSample[] {
  const filtered = samples.filter((sample: SamplingHeapProfileSample) => {
    return sample.nodeId === nodeId;
  });
  return filtered;
}

function createAllocationsFromSamplesForNode(
  samplesForNode: SamplingHeapProfileSample[]
): Allocation[] {
  const sampleSizeToCountMap = new Map<number, number>();
  samplesForNode.forEach((sample: SamplingHeapProfileSample) => {
    const currentCountForSize: number | undefined = sampleSizeToCountMap.get(
      sample.size
    );
    if (currentCountForSize !== undefined) {
      sampleSizeToCountMap.set(sample.size, currentCountForSize + 1);
    } else {
      sampleSizeToCountMap.set(sample.size, 1);
    }
  });

  const mappedAllocations: Allocation[] = [];
  sampleSizeToCountMap.forEach((size: number, count: number) => {
    const mappedAllocation: Allocation = {
      sizeBytes: size,
      count: count,
    };
    mappedAllocations.push(mappedAllocation);
  });

  return mappedAllocations;
}
