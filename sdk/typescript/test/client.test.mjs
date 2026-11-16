// The client against a fake service: a WebSocket server speaking the
// protocol, so connect / hello / subscribe ordering / reconnect / stream
// records are exercised without OCTAVE. Node 22's WebSocket is the client;
// the server is a minimal RFC 6455 implementation on node:net (text and
// binary frames, unmasked server-to-client), enough for the test.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import net from 'node:net'
import { createHash } from 'node:crypto'
import { OctaveClient, OctaveError, PROTOCOL_VERSIONS, SPEC_PROTOCOL_VERSION, buildRecord, F_KEY, obj } from '../dist/index.js'

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

const TOKEN = 't'.repeat(64)

/** A fake OCTAVE: answers hello / get / call / subscribe / stream like the real one.
 *  With { pairing: true } a connection is untrusted until it pairs with 123456
 *  or says hello with TOKEN, as off the device (servicebridge.cpp). */
function fakeService({ pairing = false } = {}) {
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
        const raw = (o) => sock.write(frame(1, Buffer.from(JSON.stringify(o))))
        const reply = (result) => raw({ id: m.id, ok: true, result })
        const refuse = (error, more = {}) => raw({ id: m.id, ok: false, error, ...more })
        if (pairing && !sock.trusted && m.op !== 'hello' && m.op !== 'pair') { refuse('this connection is not paired', { code: 'unauthorised' }); continue }
        switch (m.op) {
          case 'hello':
            if (pairing && m.token === TOKEN) sock.trusted = true
            if (pairing && !sock.trusted) reply({ protocolVersion: 3, auth: 'untrusted', loopbackOpen: false })
            else reply({ protocolVersion: pairing ? 3 : 1, app: { version: 'fake', build: 'test', headless: true }, objects: ['volumeController'], ...(pairing ? { auth: 'trusted', scopes: ['full'] } : {}) })
            break
          case 'pair':
            if (m.code !== '123456') { refuse('wrong code'); break }
            sock.trusted = true
            reply({ token: TOKEN, scopes: ['full'] })
            break
          case 'get': reply(m.property === 'percent' ? 32 : null); break
          case 'call': reply(null); break
          case 'unsubscribe': reply(true); break
          case 'unpair': sock.trusted = false; reply(true); break
          case 'silent': break                                               // never answered
          case 'unreadable': raw({ ok: false, error: 'Invalid JSON: test' }); break   // as the service answers a line it cannot parse: no id
          case 'limited': refuse('too many ops', { code: 'rateLimited', retryAfterMs: 25 }); break
          case 'subscribe': {
            if (m.object === 'nope') { refuse('no object nope'); break }
            const sub = ++state.subs
            const answer = () => {
              reply({ sub })
              // the current value follows the reply at once, in the same write
              for (const p of m.properties ?? []) raw({ event: 'property', sub, object: m.object, name: p, value: p === 'percent' ? 32 : false })
            }
            if (m.object === 'slow') setTimeout(answer, 150); else answer()
            break
          }
          case 'stream': {
            const open = () => { reply({ stream: 7, codec: 'avc1.42c01e', width: 640, height: 360, format: 'annexb' }); sock.write(frame(2, Buffer.from(buildRecord(7, F_KEY, 1000, Uint8Array.from([1, 2, 3]))))) }
            if (m.object === 'slow') setTimeout(open, 150); else open()
            break
          }
          case 'unstream': reply(true); break
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

test('the accepted versions follow the generated one', () => {
  assert.deepEqual(PROTOCOL_VERSIONS, Array.from({ length: SPEC_PROTOCOL_VERSION }, (_, i) => i + 1))
  assert.ok(PROTOCOL_VERSIONS.includes(SPEC_PROTOCOL_VERSION))
})

test('pairing: subscriptions wait for pair(), then hello is asked again and they are issued', { timeout: 10000 }, async () => {
  const { port, state, stop } = await fakeService({ pairing: true })
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/` })
  const values = []
  const errors = []
  client.on('error', (e) => errors.push(e))
  client.property('volumeController', 'percent', (v) => values.push(v))
  client.connect()
  await client.whenOnline(3000)
  await new Promise((r) => setTimeout(r, 100))
  assert.equal(client.trusted, false)
  assert.equal(client.hello.app, undefined, 'an untrusted hello has no app')
  assert.equal(state.sends.filter((m) => m.op === 'subscribe').length, 0, 'nothing is subscribed while untrusted')
  await assert.rejects(client.pair('000000', 'test'), (e) => e instanceof OctaveError && e.message === 'wrong code')
  const token = await client.pair('123456', 'test')
  assert.equal(token, TOKEN)
  assert.equal(client.trusted, true)
  assert.equal(client.hello.app.version, 'fake', 'hello was asked again after pairing')
  await new Promise((r) => setTimeout(r, 100))
  assert.deepEqual(values, [32], 'the subscription made before pairing delivers after it')
  assert.equal(state.sends.filter((m) => m.op === 'subscribe').length, 1, 'issued once')
  assert.deepEqual(errors, [])
  client.close()
  await stop()
})

test('a refused subscription reaches onError, or on("error") without one', { timeout: 10000 }, async () => {
  const { port, stop } = await fakeService()
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/` })
  client.connect()
  await client.whenOnline(3000)
  const own = await new Promise((resolve) => client.subscribe('nope', { properties: ['x'] }, () => {}, resolve))
  assert.ok(own instanceof OctaveError)
  assert.equal(own.message, 'no object nope')
  const shared = await new Promise((resolve) => { client.on('error', (e, where) => resolve({ e, where })); client.property('nope', 'x', () => {}) })
  assert.equal(shared.e.message, 'no object nope')
  assert.deepEqual(shared.where, { object: 'nope' })
  client.close()
  await stop()
})

test('a request nothing answers times out; an id-less refusal fails what is waiting; rateLimited carries retryAfterMs', { timeout: 10000 }, async () => {
  const { port, stop } = await fakeService()
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/`, requestTimeoutMs: 150 })
  client.connect()
  await client.whenOnline(3000)
  const t0 = Date.now()
  await assert.rejects(client.request('silent', {}), (e) => e instanceof OctaveError && e.code === 'timeout')
  assert.ok(Date.now() - t0 < 2000)
  assert.equal(await client.get('volumeController', 'percent'), 32, 'the client still works after a timeout')
  await assert.rejects(client.request('unreadable', {}), (e) => e instanceof OctaveError && /Invalid JSON/.test(e.message))
  await assert.rejects(client.request('limited', {}), (e) => e instanceof OctaveError && e.code === 'rateLimited' && e.retryAfterMs === 25)
  client.close()
  await stop()
})

test('a subscribe or stream answered after its timeout is adopted or ended, never left streaming to nobody', { timeout: 10000 }, async () => {
  const { port, state, stop } = await fakeService()
  // 'slow' answers after 150 ms; the client gives up at 50
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/`, requestTimeoutMs: 50 })
  client.connect()
  await client.whenOnline(3000)

  // Still held when the late reply comes: it takes the id and its events arrive
  const kept = [], errors = []
  const offKept = client.subscribe('slow', { properties: ['percent'] }, (e) => { if (e.event === 'property') kept.push(e.value) }, (e) => errors.push(e.code))
  await new Promise((r) => setTimeout(r, 400))
  assert.deepEqual(errors, ['timeout'], 'the caller was told about the timeout')
  assert.deepEqual(kept, [32], 'and the subscription works once the service answers')
  assert.equal(state.sends.filter((m) => m.op === 'unsubscribe').length, 0)
  offKept()
  await new Promise((r) => setTimeout(r, 50))
  assert.equal(state.sends.filter((m) => m.op === 'unsubscribe').length, 1, 'and can be ended like any other')

  // Dropped between the timeout and the late reply: ended on the service
  const dropped = []
  const offDropped = client.subscribe('slow', { properties: ['percent'] }, (e) => dropped.push(e), () => {})
  await new Promise((r) => setTimeout(r, 90))
  offDropped()
  await new Promise((r) => setTimeout(r, 300))
  const unsub = state.sends.filter((m) => m.op === 'unsubscribe')
  assert.equal(unsub.length, 2)
  assert.equal(unsub[1].sub, state.subs)
  assert.deepEqual(dropped, [])

  // A stream the caller never got a handle for is closed
  let records = 0
  await assert.rejects(client.stream('slow', 'video', { onRecord: () => { records++ } }), (e) => e instanceof OctaveError && e.code === 'timeout')
  await new Promise((r) => setTimeout(r, 300))
  const unstream = state.sends.filter((m) => m.op === 'unstream')
  assert.equal(unstream.length, 1)
  assert.equal(unstream[0].stream, 7)
  assert.equal(records, 0)
  client.close()
  await stop()
})

test('unsubscribing before the subscribe reply ends the subscription on the service', { timeout: 10000 }, async () => {
  const { port, state, stop } = await fakeService()
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/` })
  client.connect()
  await client.whenOnline(3000)
  const values = []
  const off = client.property('slow', 'percent', (v) => values.push(v))
  off()                                   // mount / unmount before the reply
  await new Promise((r) => setTimeout(r, 400))
  const unsub = state.sends.filter((m) => m.op === 'unsubscribe')
  assert.equal(unsub.length, 1, 'one unsubscribe, sent once the reply named the id')
  assert.equal(unsub[0].sub, state.subs)
  assert.deepEqual(values, [], 'no event reaches a handler that was removed')
  client.close()
  await stop()
})

test('unpair() forgets the token and the trust', { timeout: 10000 }, async () => {
  const { port, stop } = await fakeService({ pairing: true })
  const client = new OctaveClient({ url: `ws://127.0.0.1:${port}/`, token: TOKEN })
  client.connect()
  await client.whenOnline(3000)
  assert.equal(client.trusted, true)
  await client.unpair()
  assert.equal(client.trusted, false)
  assert.equal(client.hello.scopes, undefined)
  await assert.rejects(client.get('volumeController', 'percent'), (e) => e.code === 'unauthorised')
  client.close()
  await stop()
})
