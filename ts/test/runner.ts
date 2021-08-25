#!/usr/bin/env node

// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {Worker, isMainThread, workerData} from 'worker_threads';
import * as Mocha from 'mocha';

import {readdir} from 'fs';
import {join} from 'path';

const testDir = './out/test/';

if (isMainThread) {
  readdir(testDir, (err, files) => {
    if (err) throw err;

    for (const file of files) {
      if (file.substr(-3) !== '.js') continue;
      if (file.substr(0, 5) !== 'test-') continue;

      new Worker(__filename, {
        workerData: join(testDir, file),
      });
    }
  });
} else {
  const mocha = new Mocha();
  mocha.addFile(workerData);
  mocha.run();
}
