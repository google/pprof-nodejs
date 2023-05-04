// eslint-disable-next-line node/no-unsupported-features/node-builtins
import {execFile} from 'child_process';
import {promisify} from 'util';

const exec = promisify(execFile);

const assert = require('assert');

describe('Worker Threads', () => {
  // eslint-ignore-next-line prefer-array-callback
  it('should work when propagated to workers through -r flag', function () {
    this.timeout(5000);
    return exec('node', ['./out/test/worker.js']).then(({stdout}) => {
      assert.strictEqual(stdout, 'it works!\nit works!\nit works!\n');
    });
  });
});
