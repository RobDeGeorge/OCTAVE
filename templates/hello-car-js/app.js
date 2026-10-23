// Hello, car: connect, pair when this browser is not on the head unit, two
// live readings, the track with play / pause, the cover. The page is served
// by OCTAVE itself (--web-root), so the socket is this page's own host.
import { Octave } from './octave.js'

const NAME = 'hello-car'                        // this frontend's name: its demand consumer and its device name
const PIDS = ['RPM', 'SPEED']
const $ = (id) => document.getElementById(id)
const store = {                                 // the pairing token, per page origin
  get() { try { return localStorage.getItem('octave.token') } catch { return null } },
  set(v) { try { v ? localStorage.setItem('octave.token', v) : localStorage.removeItem('octave.token') } catch { /* private window */ } },
}

export async function main() {
  const octave = new Octave(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/`)
  octave.onclose = () => { $('status').textContent = 'disconnected: reload to reconnect' }

  let hello = await octave.connect(store.get() ?? undefined)
  if (hello.tokenRejected) store.set(null)      // revoked or unknown: pair again
  if (hello.auth === 'untrusted') hello = await pair(octave)
  $('status').textContent = `OCTAVE ${hello.app.version}, protocol ${hello.protocolVersion}`

  // Readings: titles and units from the catalog, values from paramStore (native units)
  const info = {}
  for (const id of PIDS) info[id] = await octave.call('parameterCatalog', 'info', id)
  for (const id of PIDS) { $(id.toLowerCase() + '-label').textContent = info[id].title; $(id.toLowerCase() + '-unit').textContent = info[id].unit === info[id].title ? '' : info[id].unit }
  const np = {}
  octave.onEvent((e) => {
    if (e.event === 'params') for (const [id, v] of Object.entries(e.values)) $(id.toLowerCase()).textContent = v === null ? '–' : v.toFixed(info[id].decimals)
    if (e.event === 'property' && e.object === 'nowPlaying') { np[e.name] = e.value; render(octave, np) }
  })
  // OCTAVE polls the car only for what someone shows: ask for these two, by our own consumer name
  await octave.call('obdManager', 'setParameterDemand', NAME, PIDS)
  await octave.subscribe('paramStore', { properties: PIDS, throttleMs: 100 })
  await octave.subscribe('nowPlaying', { properties: ['hasTrack', 'title', 'artist', 'playing', 'artRevision'], throttleMs: 250 })
  $('play').onclick = () => octave.call('nowPlaying', 'togglePlay')
}

// Pairing: the user reads a 6-digit code off the head unit (Settings > Service > Pair a device);
// we ask only for the media permission (play / pause), readings are always allowed
async function pair(octave) {
  $('pairing').hidden = false
  return new Promise((resolve) => {
    $('pair').onclick = async () => {
      $('pair-error').textContent = ''
      try {
        store.set(await octave.pair($('code').value.replace(/\D/g, ''), NAME, ['media']))
        $('pairing').hidden = true
        resolve(await octave.request('hello', { token: store.get() }))
      } catch (err) { $('pair-error').textContent = err.message }
    }
  })
}

async function render(octave, np) {
  $('title').textContent = np.hasTrack ? np.title || 'Unknown title' : 'Nothing playing'
  $('artist').textContent = np.hasTrack ? np.artist || '' : ''
  $('play').textContent = np.playing ? 'Pause' : 'Play'
  const img = $('art')
  if (!np.hasTrack) { img.hidden = true; return }
  // The cover over HTTP, with a short-lived ticket in the URL; artRevision defeats the cache
  img.onerror = () => { img.hidden = true }
  img.onload = () => { img.hidden = false }
  img.src = `/art?rev=${np.artRevision}&ticket=${await octave.ticket()}`
}

main().catch((err) => { $('status').textContent = err.message })
