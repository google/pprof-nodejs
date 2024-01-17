'use strict'

const { execSync } = require('child_process')
const { existsSync } = require('fs')
const { join } = require('path')

const name = process.argv[2] || 'test_dd_pprof'

const cmd = [
  'node-gyp',
  'configure',
  'build',
  '--build_tests'
].join(' ')

execSync(cmd, { stdio: [0, 1, 2] })

function findBuild (mode) {
  const path = join(__dirname, '..', 'build', mode, name) + '.node'
  if (!existsSync(path)) {
    // eslint-disable-next-line no-console
    console.warn(`No ${mode} binary found for ${name} at: ${path}`)
    return
  }
  return path
}

const path = findBuild('Release') || findBuild('Debug')
if (!path) {
  // eslint-disable-next-line no-console
  console.error(`No ${name} build found`)
  process.exitCode = 1
} else {
  execSync(`node ${path}`, { stdio: [0, 1, 2] })
}
