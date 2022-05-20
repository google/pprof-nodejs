'use strict';

const {existsSync} = require('fs');
const {join} = require('path');

const name = process.argv[2] || 'test_dd_pprof';

function findBuild(mode) {
  const path = join(__dirname, '..', 'build', mode, name) + '.node';
  if (!existsSync(path)) {
    // eslint-disable-next-line no-console
    console.warn(`No ${mode} binary found for ${name} at: ${path}`);
    return;
  }
  return path;
}

const path = findBuild('Release') || findBuild('Debug');
if (!path) {
  // eslint-disable-next-line no-console
  console.error(`No ${name} build found`);
  process.exitCode = 1;
  return;
}

require(path);
