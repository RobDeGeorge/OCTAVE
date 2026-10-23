import { test } from 'node:test'
import assert from 'node:assert/strict'
import { buildRecord, parseRecord, F_KEY, F_JPEG, F_DISCONTINUITY, RECORD_HEADER } from '../dist/index.js'

test('a media record round-trips through build and parse', () => {
  const payload = Uint8Array.from([0, 0, 0, 1, 0x67, 1, 2, 3])
  const buf = buildRecord(2, F_KEY | F_DISCONTINUITY, 1234567, payload)
  assert.equal(buf.byteLength, RECORD_HEADER + payload.length)
  const r = parseRecord(buf)
  assert.ok(r)
  assert.equal(r.stream, 2)
  assert.equal(r.pts, 1234567)
  assert.equal(r.keyframe, true)
  assert.equal(r.discontinuity, true)
  assert.equal(r.jpeg, false)
  assert.deepEqual([...r.payload], [...payload])
})

test('the pts is a big-endian u64', () => {
  const v = new DataView(buildRecord(1, F_JPEG, 0x0102030405, new Uint8Array(0)))
  assert.equal(v.getUint8(11), 0x05)
  assert.equal(v.getUint8(10), 0x04)
  assert.equal(parseRecord(v.buffer).jpeg, true)
})

test('a record of another version or too short is refused', () => {
  const buf = buildRecord(1, 0, 0, new Uint8Array(0))
  new DataView(buf).setUint8(0, 2)
  assert.equal(parseRecord(buf), null)
  assert.equal(parseRecord(new ArrayBuffer(4)), null)
})
