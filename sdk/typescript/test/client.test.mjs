// The client against a fake service: a WebSocket server speaking the
// protocol, so connect / hello / subscribe ordering / reconnect / stream
// records are exercised without OCTAVE. Node 22's WebSocket is the client;
// the server is a minimal RFC 6455 implementation on node:net (text and
// binary frames, unmasked server-to-client), enough for the test.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import net from 'node:net'
import { createHash } from 'node:crypto'
import { OctaveClient, buildRecord, F_KEY, obj } from '../dist/index.js'

function frame(opcode, data) {
  const len = data.length
  const head = len < 126 ? Buffer.from([0x80 | opcode, len]) : len < 65536 ? Buffer.concat([Buffer.from([0x80 | opcode, 126]), Buffer.from([len >> 8, len & 255])]) : Buffer.concat([Buffer.from([0x80 | opcode, 127, 0, 0, 0, 0]), Buffer.from([(len >>> 24) & 255, (len >>> 16) & 255, (len >>> 8) & 255, len & 255])])
  return Buffer.concat([head, data])
}
function* unmask(buf) {
  let i = 0
  while (i + 2 <= buf.length) {
    const op = buf[i] & 0x0f
    let len = buf[i + 1] & 0x7f, off = i + 2
    if (len === 126) { len = buf.readUInt16BE(off); off += 2 } else if (len === 127) { len = Number(buf.readBigUInt64BE(off)); off += 8 }
    const mask = buf.subarray(off, off + 4); off += 4
    if (off + len > buf.length) return
    const data = Buffer.from(buf.subarray(off, off + len)).map((b, k) => b ^ mask[k % 4])
    yield { op, data }
    i = off + len
  }
}

/** A fake OCTAVE: answers hello / get / call / subscribe / stream like the real one. */
function fakeService() {
  const state = { clients: new Set(), sends: [], subs: 0 }
  const server = net.createServer((sock) => {
    let upgraded = false
    sock.on('data', (buf) => {
      if (!upgraded) {
        const key = /sec-websocket-key: (.+)\r\n/i.exec(buf.toString())?.[1]?.trim()
        const accept = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64')
        sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`)
        upgraded = true
        state.clients.add(sock)
        return
      }
      for (const f of unmask(buf)) {
        if (f.op === 8) { sock.end(); return }
        if (f.op !== 1) continue
        const m = JSON.parse(f.data.toString())
        state.sends.push(m)
        const reply = (result) => sock.write(frame(1, Buffer.from(JSON.stringify({ id: m.id, ok: true, result }))))
        switch (m.op) {
          case 'hello': reply({ protocolVersion: 1, app: { version: 'fake', build: 'test', headless: true }, objects: ['volumeController'] }); break
          case 'get': reply(m.property === 'percent' ? 32 : null); break
          case 'call': reply(null); break
          case 'subscribe': {
            const sub = ++state.subs
            reply({ sub })
            // the current value follows the reply at once, in the same write
            for (const p of m.properties ?? []) sock.write(frame(1, Buffer.from(JSON.stringify({ event: 'property', sub, object: m.object, name: p, value: p === 'percent' ? 32 : false }))))
            break
          }
          case 'stream': reply({ stream: 7, codec: 'avc1.42c01e', width: 640, height: 360, format: 'annexb' }); sock.write(frame(2, Buffer.from(buildRecord(7, F_KEY, 1000, Uint8Array.from([1, 2, 3]))))); break
          default: sock.write(frame(1, Buffer.from(JSON.stringify({ id: m.id, ok: false, error: `no op ${m.op}` }))))
        }
      }
    })
    sock.on('close', () => state.clients.delete(sock))
    sock.on('error', () => {})
  })
  const stop = () => new Promise((r) => { for (const c of state.clients) c.destroy(); server.close(() => r()) })
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({ port: server.address().port, server, state, stop })))
}

test('connect, hello, typed call, subscribe with the value at once, a stream record', { timeout: 10000 }, async () => {
  const { port, state, stop } = await fakeService()
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/` })
  client.connect()
  await client.whenOnline(3000)
  assert.equal(client.hello.app.version, 'fake')
  const vol = obj(client, 'volumeController')
  assert.equal(await vol.get('percent'), 32)
  await vol.call('applyVolume', 40)
  assert.deepEqual(state.sends.at(-1).args, [40])
  const seen = await new Promise((resolve) => { const off = vol.property('percent', (v) => { off(); resolve(v) }) })
  assert.equal(seen, 32, 'the first event, sent right after the subscribe reply, must reach the handler')
  const rec = await new Promise((resolve) => client.stream('phoneMirrorManager', 'video', { onRecord: resolve }))
  assert.equal(rec.stream, 7)
  assert.equal(rec.keyframe, true)
  assert.deepEqual([...rec.payload], [1, 2, 3])
  client.close()
  await stop()
})

test('reconnects after the service goes away and re-issues subscriptions', { timeout: 10000 }, async () => {
  const { port, state, stop } = await fakeService()
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/`, minBackoffMs: 50, maxBackoffMs: 100 })
  const values = []
  client.property('volumeController', 'muted', (v) => values.push(v))
  client.connect()
  await client.whenOnline(3000)
  await new Promise((r) => setTimeout(r, 100))
  const statuses = []
  client.on('status', (s) => statuses.push(s))
  for (const c of state.clients) c.destroy()
  await new Promise((r) => setTimeout(r, 400))
  assert.ok(statuses.includes('offline'), `went offline: ${statuses}`)
  await client.whenOnline(3000)
  await new Promise((r) => setTimeout(r, 100))
  assert.equal(state.sends.filter((m) => m.op === 'hello').length, 2, 'a second hello')
  assert.equal(values.length, 2, 'one value per connection')
  assert.equal(state.sends.filter((m) => m.op === 'subscribe').length, 2)
  client.close()
  await stop()
})
