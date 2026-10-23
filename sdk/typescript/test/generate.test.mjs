// The generators: the protocol envelope from docs/api/protocol.json and
// the object types from docs/api/*.json, with `deprecated` / `stability`
// carried into JSDoc (@deprecated, @experimental) from a fixture that has
// them (the real docs/api may not, at any given time).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const run = (script, env) => execFileSync(process.execPath, [path.join(here, '../scripts', script), '--stdout'], { env: { ...process.env, ...env }, encoding: 'utf8' })

test('object types carry @deprecated and @experimental from docs/api', () => {
  const out = run('generate-types.mjs', { OCTAVE_API_DIR: path.join(here, 'fixture-api') })
  assert.match(out, /\* @deprecated use title; goes in protocol 3\n\s+\*\/\n\s+name: string/)
  assert.match(out, /@experimental[^\n]*\n\s+\*\/\n\s+level: number/)
  assert.match(out, /@deprecated use run\(speed, mode\); goes in protocol 3 \*\/\n\s+go: \{ args: \[speed: number\]; returns: void \}/)
  assert.match(out, /@experimental[^\n]*\*\/\n\s+levelChanged: \[level: number\]/)
  assert.match(out, /old \(Old\)\n \* @experimental[^\n]*\n \*\/\nexport interface Old/)
  assert.doesNotMatch(out, /@deprecated[^\n]*\n\s+run:/)   // only the annotated members
})

test('the protocol envelope comes from protocol.json', () => {
  const out = run('generate-protocol.mjs', {})
  assert.match(out, /export const PROTOCOL_VERSION = \d+/)
  assert.match(out, /export const OPS = \["hello",/)
  assert.match(out, /export interface HelloReply \{[\s\S]*protocolVersion: number[\s\S]*auth: "trusted" \| "untrusted"/)
  assert.match(out, /@since protocol 2[\s\S]{0,400}export interface PairArgs/)
  assert.match(out, /export interface SubscribeReply \{\n\s+[^\n]*\n?\s*sub: number/)
  assert.match(out, /export type ServiceEvent = PropertyEvent \| SignalEvent \| ParamsEvent \| ModelEvent \| DroppedEvent \| StreamConfigEvent \| StreamDroppedEvent/)
  assert.match(out, /export type ErrorCode = "unauthorised"/)
  assert.match(out, /export const F_DISCONTINUITY = 8/)
  assert.match(out, /export const ROUTES = \["\/art","\/export\/trip","\/export\/dashboard","\/export\/logs"\]/)
})
