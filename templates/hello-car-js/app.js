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

let retryMs = 1000                              // the pause before the next connection, growing to 8 s

export async function main() {
  const octave = new Octave(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/`)
  let lost = () => {}
  // When the connection goes (OCTAVE restarted, or it closed one that had not paired after a
  // minute) connect again and set everything up again: no reload needed
  octave.onclose = () => {
    $('status').textContent = 'disconnected: reconnecting…'
    lost()
    setTimeout(() => main().catch(show), retryMs)
    retryMs = Math.min(retryMs * 2, 8000)
  }

  let hello = await octave.connect(store.get() ?? undefined)
  if (hello.tokenRejected) store.set(null)      // revoked or unknown: pair again
  if (hello.auth === 'untrusted') hello = await pair(octave, (fn) => { lost = fn })
  $('pairing').hidden = true
  retryMs = 1000
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
async function pair(octave, whenLost) {
  $('pairing').hidden = false
  return new Promise((resolve, reject) => {
    // Fetching the code can take longer than OCTAVE keeps an unpaired connection: main()
    // connects again and shows this again, with what was typed still in the field
    whenLost(() => reject(new Error('connection closed')))
    let paired = false
    $('pair').onclick = async () => {
      $('pair-error').textContent = ''
      try {
        // A code works once: after it has paired, another press only asks hello again
        if (!paired) { store.set(await octave.pair($('code').value.replace(/\D/g, ''), NAME, ['media'])); paired = true }
        // The panel stays until hello has answered, so a failure here is seen
        const hello = await octave.request('hello', { token: store.get() })
        $('pairing').hidden = true
        resolve(hello)
      } catch (err) { $('pair-error').textContent = paired ? `Paired, but OCTAVE did not answer after that (${err.message}). Press Pair to try again.` : err.message }
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

function show(err) { if (err.message !== 'connection closed') $('status').textContent = err.message }

main().catch(show)
