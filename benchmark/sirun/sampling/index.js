'use strict'

const { default: CpuProfiler } = require('../../../out/src/cpu-profiler')

let profiler
if (process.env.ENABLE_PROFILER === 'true') {
  profiler = new CpuProfiler()
}

function recurse (n) {
  if (n !== 0) return recurse(--n)
  if (!profiler) return

  profiler.captureSample()
  profiler.processSample()
}

recurse(Number(process.env.DEPTH || '100'))
