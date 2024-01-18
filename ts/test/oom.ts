'use strict';

/* eslint-disable no-console */
import {Worker, isMainThread, threadId} from 'worker_threads';
import {heap} from '../src/index';
import path from 'path';

const nworkers = Number(process.argv[2] || 0);
const workerMaxOldGenerationSizeMb = process.argv[3];
const maxCount = Number(process.argv[4] || 12);
const sleepMs = Number(process.argv[5] || 50);
const sizeQuantum = Number(process.argv[6] || 5 * 1024 * 1024);

console.log(`${isMainThread ? 'Main thread' : `Worker ${threadId}`}: \
nworkers=${nworkers} workerMaxOldGenerationSizeMb=${workerMaxOldGenerationSizeMb} \
maxCount=${maxCount} sleepMs=${sleepMs} sizeQuantum=${sizeQuantum}`);

heap.start(1024 * 1024, 64);
heap.monitorOutOfMemory(0, 0, false, [
  process.execPath,
  path.join(__dirname, 'check_profile.js'),
]);

if (isMainThread) {
  for (let i = 0; i < nworkers; i++) {
    const worker = new Worker(__filename, {
      argv: [0, ...process.argv.slice(3)],
      ...(workerMaxOldGenerationSizeMb
        ? {resourceLimits: {maxOldGenerationSizeMb: 50}}
        : {}),
    });
    const threadId = worker.threadId;
    worker
      .on('error', err => {
        console.log(`Worker ${threadId} error: ${err}`);
      })
      .on('exit', code => {
        console.log(`Worker ${threadId} exit: ${code}`);
      });
  }
}

const leak: number[][] = [];
let count = 0;

function foo(size: number) {
  count += 1;
  const n = size / 8;
  const x: number[] = [];
  x.length = n;
  for (let i = 0; i < n; i++) {
    x[i] = Math.random();
  }
  leak.push(x);

  if (count < maxCount) {
    setTimeout(() => foo(size), sleepMs);
  }
}

setTimeout(() => foo(sizeQuantum), sleepMs);
