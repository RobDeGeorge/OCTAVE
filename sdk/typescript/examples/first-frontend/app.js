// A first OCTAVE frontend with the TypeScript SDK, in 60 lines: connect, pair
// when this browser is not on the head unit, two live gauges, a now-playing
// line with transport, the cover. Build the SDK, then serve this folder from
// the app (see README.md).
import { OctaveClient, obj } from './lib/index.js'

const $ = (id) => document.getElementById(id)
const client = new OctaveClient({ token: localStorage.getItem('token') ?? undefined })   // ws://<this page's host>/
const obd = obj(client, 'obdManager')
const now = obj(client, 'nowPlaying')

client.on('status', (s) => { $('status').textContent = s === 'online' ? `OCTAVE ${client.hello?.app.version}` : s })
client.on('hello', (h) => {
  // Protocol 2: over the network we must pair once; on the head unit nothing to do.
  if (h.tokenRejected) localStorage.removeItem('token')
  $('pair').style.display = h.auth === 'untrusted' ? 'block' : 'none'
  if (h.auth !== 'untrusted') start()
})
$('pair').onsubmit = async (e) => {
  e.preventDefault()
  try {
    localStorage.setItem('token', await client.pair($('code').value, 'First frontend'))
    client.close(); client.connect()                         // a fresh, trusted connection re-issues everything
  } catch (err) { $('pairError').textContent = err.message }
}

let started = false
async function start() {
  if (started) return
  started = true
  // Live readings: the app polls the car only for what someone asks for.
  await obd.call('setParameterDemand', 'first-frontend', ['SPEED', 'RPM'])
  client.params(['SPEED', 'RPM'], (v) => { for (const id in v) $(id).textContent = v[id] === null ? '–' : Math.round(v[id]) }, 100)
  // Now playing: the properties arrive at once, then on every change.
  now.subscribe({ properties: ['hasTrack', 'title', 'artist', 'playing', 'artRevision'], throttleMs: 250 }, (e) => {
    if (e.event !== 'property') return
    if (e.name === 'title' || e.name === 'hasTrack') $('title').textContent = e.name === 'hasTrack' && !e.value ? 'Nothing playing' : e.value || $('title').textContent
    if (e.name === 'artist') $('artist').textContent = e.value
    if (e.name === 'playing') $('play').textContent = e.value ? '⏸' : '▶'
    if (e.name === 'artRevision') cover(e.value)
  })
}
async function cover(rev) {
  // Over the network /art needs a short-lived ticket; on the head unit it is harmless.
  const t = client.hello?.loopbackOpen && location.hostname === '127.0.0.1' ? '' : `&ticket=${(await client.mediaTicket()).ticket}`
  $('cover').src = rev > 0 ? `/art?rev=${rev}${t}` : ''
}
$('play').onclick = () => now.call('togglePlay')
$('next').onclick = () => now.call('next')
$('prev').onclick = () => now.call('previous')
client.connect()
