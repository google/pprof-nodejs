// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {execFile} from 'child_process';
import {promisify} from 'util';

const exec = promisify(execFile);

describe('Worker Threads', () => {
  // eslint-ignore-next-line prefer-array-callback
  it('should work', function () {
    this.timeout(20000);
    const nbWorkers = 2;
    return exec('node', ['./out/test/worker.js', String(nbWorkers)]);
  });
});
