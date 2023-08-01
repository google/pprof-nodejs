// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {execFile} from 'child_process';
import {promisify} from 'util';

const exec = promisify(execFile);

describe('Worker Threads', () => {
  // eslint-ignore-next-line prefer-array-callback
  it('should work when propagated to workers through -r flag', function () {
    this.timeout(10000);
    const nbWorkers = 4;
    return exec('node', ['./out/test/worker.js', String(nbWorkers)]);
  });
});
