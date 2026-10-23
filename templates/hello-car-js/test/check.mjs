// Drives the template's client (octave.js) through what app.js does, against
// a real `octave --headless --sim` on a throwaway port: hello, pairing with
// the media scope (the code from the simulator), the readings via
// setParameterDemand + paramStore, play / pause, the cover with a media
// ticket. Node 22+; OCTAVE_BIN names the binary. ctest runs this as
// template_js; CI's sdk job does not (no app there).
import { spawn } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import net from 'node:net'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { Octave } from '../octave.js'

const bin = process.env.OCTAVE_BIN ?? path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../build/octave')
const port = await new Promise((r) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => r(p)) }) })
const home = mkdtempSync(path.join(os.tmpdir(), 'hello-car-'))
const app = spawn(bin, ['--headless', '--sim', '--service', String(port)], {
  env: { ...process.env, HOME: home, XDG_CONFIG_HOME: home, XDG_DATA_HOME: home, XDG_CACHE_HOME: home, QT_QPA_PLATFORM: 'offscreen' },
  stdio: 'ignore',
})
const fail = (why) => { console.error('FAIL:', why); app.kill('SIGTERM'); process.exit(1) }
const check = (cond, why) => { if (!cond) fail(why); else console.log('ok:', why) }
process.on('exit', () => { app.kill('SIGTERM'); rmSync(home, { recursive: true, force: true }) })

for (let i = 0; i < 300; i++) {
  if (app.exitCode !== null) fail(`octave exited with ${app.exitCode}`)
  try { await new Promise((ok, no) => { const s = net.connect(port, '127.0.0.1'); s.on('connect', () => { s.end(); ok() }); s.on('error', no) }); break } catch { await new Promise((r) => setTimeout(r, 200)) }
}

const octave = new Octave(`ws://127.0.0.1:${port}/`)
const hello = await octave.connect()
check(hello.auth === 'trusted' && hello.app.headless, 'hello on loopback is trusted and headless')

// Pairing with the media scope, the way app.js does when untrusted (the code from the simulator)
const { code } = await octave.call('sim', 'pairingCode')
const token = await octave.pair(code, 'hello-car', ['media'])
check(/^[0-9a-f]{64}$/.test(token), 'pair with ["media"] returns a token')
check(octave.scopes.includes('media') && !octave.scopes.includes('full'), `the device was granted ${octave.scopes}`)
// On the head unit itself (loopback) every connection has full access anyway; the
// token matters over the network. Carry on with a connection that presents it.
const paired = new Octave(`ws://127.0.0.1:${port}/`)
const h2 = await paired.connect(token)
check(h2.auth === 'trusted', 'hello with the token is trusted')

// Readings
const values = {}
paired.onEvent((e) => { if (e.event === 'params') Object.assign(values, e.values) })
await paired.call('obdManager', 'setParameterDemand', 'hello-car', ['RPM', 'SPEED'])
await paired.subscribe('paramStore', { properties: ['RPM', 'SPEED'], throttleMs: 100 })
await paired.call('sim', 'setEngineOn', true)
await paired.call('sim', 'setThrottle', 40)
for (let i = 0; i < 100 && !(values.RPM > 0 && typeof values.SPEED === 'number'); i++) await new Promise((r) => setTimeout(r, 100))
check(values.RPM > 0 && typeof values.SPEED === 'number', `RPM and speed arrive (${values.RPM} rpm, ${values.SPEED})`)

// Play / pause
const np = {}
paired.onEvent((e) => { if (e.event === 'property' && e.object === 'nowPlaying') np[e.name] = e.value })
await paired.subscribe('nowPlaying', { properties: ['hasTrack', 'title', 'playing', 'artRevision'] })
await new Promise((r) => setTimeout(r, 300))
await paired.call('nowPlaying', 'togglePlay')     // nothing loaded yet: starts the first test track
for (let i = 0; i < 50 && !(np.hasTrack && np.playing); i++) await new Promise((r) => setTimeout(r, 100))
check(np.hasTrack === true && np.playing === true && np.title, `togglePlay starts a track (${np.title})`)
await paired.call('nowPlaying', 'togglePlay')
for (let i = 0; i < 50 && np.playing; i++) await new Promise((r) => setTimeout(r, 100))
check(np.playing === false, 'togglePlay pauses it')

// The cover: a ticket in the URL, never the token
const ticket = await paired.ticket()
const withTicket = await fetch(`http://127.0.0.1:${port}/art?rev=${np.artRevision}&ticket=${ticket}`)
check(withTicket.status === 200 || withTicket.status === 404, `/art with a ticket answers ${withTicket.status} (200 a cover, 404 none)`)
const without = await fetch(`http://127.0.0.1:${port}/art`, { headers: { 'Sec-Fetch-Site': 'cross-site' } })
check(without.status === 403 || without.status === 401, `/art from another site is refused (${without.status})`)

octave.close(); paired.close()
console.log('hello-car-js: all checks passed')
process.exit(0)
