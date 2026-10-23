#!/usr/bin/env node
// Follow what is playing and drive it from the keyboard: next, play,
// volume 40, mute. Node 22+; OCTAVE_URL to point elsewhere than 127.0.0.1:9876.
import readline from 'node:readline'
import { OctaveClient, obj } from '../dist/index.js'

const client = new OctaveClient({ url: process.env.OCTAVE_URL })
client.on('status', (s) => console.log(`[${s}]`))
client.connect()
await client.whenOnline(10000)
console.log(`OCTAVE ${client.hello.app.version}; type next / previous / play / volume N / mute / quit`)

const np = obj(client, 'nowPlaying')
const vol = obj(client, 'volumeController')
let line = ''
np.subscribe({ properties: ['title', 'artist', 'playing', 'positionMs', 'durationMs'], throttleMs: 500 }, () => {})
const state = {}
for (const p of ['title', 'artist', 'playing', 'positionMs', 'durationMs']) np.property(p, (v) => { state[p] = v; show() })
vol.property('percent', (v) => { state.volume = v; show() })
function show() {
  const mmss = (ms) => `${Math.floor(ms / 60000)}:${String(Math.floor((ms % 60000) / 1000)).padStart(2, '0')}`
  const next = `${state.playing ? '>' : '||'} ${state.title ?? ''} - ${state.artist ?? ''}  ${mmss(state.positionMs ?? 0)}/${mmss(state.durationMs ?? 0)}  vol ${state.volume ?? '?'}`
  if (next !== line) { line = next; process.stdout.write(`\r${line.padEnd(100)}`) }
}

const rl = readline.createInterface({ input: process.stdin })
for await (const cmd of rl) {
  const [verb, arg] = cmd.trim().split(/\s+/)
  if (verb === 'next') await np.call('next')
  else if (verb === 'previous') await np.call('previous')
  else if (verb === 'play') await np.call('togglePlay')
  else if (verb === 'volume') await vol.call('applyVolume', Number(arg))
  else if (verb === 'mute') await vol.set('muted', !(await vol.get('muted')))
  else if (verb === 'quit') break
}
client.close()
process.exit(0)
