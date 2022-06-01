'use strict'

const { default: CpuProfiler } = require('../../../out/src/cpu-profiler')
const { createServer, request } = require('http')

const concurrency = Number(process.env.CONCURRENCY || '10')
const requestFrequency = Number(process.env.REQUEST_FREQUENCY || '15')
const sampleFrequency = Number(process.env.SAMPLE_FREQUENCY || '999')

const server = createServer((req, res) => {
  setImmediate(() => {
    res.end('hello')
  })
})

function get (options) {
  return new Promise((resolve, reject) => {
    const req = request(options, (res) => {
      const chunks = []
      res.on('error', reject)
      res.on('data', chunks.push.bind(chunks))
      res.on('end', () => {
        resolve(Buffer.concat(chunks))
      })
    })
    req.on('error', reject)
    req.end()
  })
}

function delay (ms) {
  return new Promise(resolve => setTimeout(resolve, ms))
}

async function storm (requestFrequency, task) {
  const gap = (1 / requestFrequency) * 1e9
  while (server.listening) {
    const start = process.hrtime.bigint()
    try {
      await task()
    } catch (e) {
      // Ignore ECONNRESET if server is shutting down
      if (e.code !== 'ECONNRESET' || server.listening) {
        throw e
      }
    }
    const end = process.hrtime.bigint()
    const remainder = gap - Number(end - start)
    await delay(Math.max(0, remainder / 1e6))
  }
}

server.listen(8080, '0.0.0.0', async () => {
  if (!concurrency) return
  const { address, port } = server.address()
  const getter = get.bind(null, {
    hostname: address,
    path: '/',
    port
  })
  const task = storm.bind(null, requestFrequency, getter)
  const tasks = Array.from({ length: concurrency }, task)
  await Promise.all(tasks)
})

let profiler
if (sampleFrequency !== 0) {
  profiler = new CpuProfiler()
  profiler.start(sampleFrequency)
}

setTimeout(() => {
  if (profiler) {
    profiler.profile()
    profiler.stop()
  }
  server.close()
}, 1000)
