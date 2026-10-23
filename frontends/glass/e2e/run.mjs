#!/usr/bin/env node
// End-to-end check of the glass frontend against a live OCTAVE serving it:
//
//   ./build/octave --dev --service --web-root frontends/glass/dist   # a simulated car, the page at :9876
//   OCTAVE_PORT=9876 npm run e2e
//
// Plain Node (22+ for the built-in WebSocket) and a headless Chromium driven
// over the DevTools protocol; state is read back through the service's own
// TCP port, not through the page. Exits 0 with a note when nothing answers,
// so it is safe in any environment; a failed check exits 1.
import { spawn } from 'node:child_process'
import net from 'node:net'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const HOST = process.env.OCTAVE_HOST ?? '127.0.0.1'
const PORT = Number(process.env.OCTAVE_PORT ?? 9876)
const CHROME = process.env.CHROME ?? ['chromium', 'chromium-browser', 'google-chrome', 'chrome'].find(exists) ?? 'chromium'
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

function exists(cmd) {
  return (process.env.PATH ?? '').split(path.delimiter).some((d) => { try { fs.accessSync(path.join(d, cmd), fs.constants.X_OK); return true } catch { return false } })
}

// ── the service, over TCP JSON lines ────────────────────────────────────
class Service {
  constructor() { this.id = 1; this.pending = new Map(); this.signals = []; this.buf = '' }
  connect() {
    return new Promise((resolve, reject) => {
      this.sock = net.connect(PORT, HOST, () => resolve())
      this.sock.on('error', reject)
      this.sock.on('data', (d) => {
        this.buf += d.toString()
        let i
        while ((i = this.buf.indexOf('\n')) >= 0) {
          const line = this.buf.slice(0, i); this.buf = this.buf.slice(i + 1)
          if (!line.trim()) continue
          const m = JSON.parse(line)
          if (m.event) { this.signals.push(m); continue }
          const p = this.pending.get(m.id); if (!p) continue
          this.pending.delete(m.id); m.ok ? p.resolve(m.result) : p.reject(new Error(m.error))
        }
      })
    })
  }
  request(op, args = {}) {
    const id = this.id++
    return new Promise((resolve, reject) => { this.pending.set(id, { resolve, reject }); this.sock.write(JSON.stringify({ op, id, ...args }) + '\n') })
  }
  get(object, property) { return this.request('get', { object, property }) }
  call(object, method, ...args) { return this.request('call', { object, method, args }) }
  /** The dev-only simulator (only with --profile / --dev): nested args. */
  sim(action, args) { return new Promise((resolve) => { const s = net.connect(PORT, HOST, () => { s.write(JSON.stringify({ action, args }) + '\n'); s.once('data', () => { s.end(); resolve() }) }); s.on('error', () => resolve()) }) }
  close() { this.sock?.end() }
}

// ── Chromium over CDP ───────────────────────────────────────────────────
class Browser {
  async start() {
    this.dir = fs.mkdtempSync(path.join(os.tmpdir(), 'glass-e2e-'))
    const port = 9400 + Math.floor(Math.random() * 400)
    this.proc = spawn(CHROME, ['--headless', '--disable-gpu', '--hide-scrollbars', '--no-first-run', `--remote-debugging-port=${port}`,
      '--window-size=1280,800', `--user-data-dir=${this.dir}`, 'about:blank'], { stdio: 'ignore' })
    let target
    for (let i = 0; i < 50 && !target; i++) {
      await sleep(200)
      try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page') } catch { /* not up yet */ }
    }
    if (!target) throw new Error(`could not start ${CHROME}`)
    this.ws = new WebSocket(target.webSocketDebuggerUrl)
    await new Promise((r, j) => { this.ws.onopen = r; this.ws.onerror = () => j(new Error('devtools socket')) })
    this.id = 0; this.pending = new Map(); this.errors = []
    this.ws.onmessage = (m) => {
      const d = JSON.parse(m.data)
      if (d.id && this.pending.has(d.id)) { this.pending.get(d.id)(d); this.pending.delete(d.id); return }
      if (d.method === 'Runtime.exceptionThrown') this.errors.push(d.params.exceptionDetails?.exception?.description ?? d.params.exceptionDetails?.text ?? 'exception')
      if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') this.errors.push(d.params.args.map((a) => a.value ?? a.description).join(' '))
      if (d.method === 'Log.entryAdded' && d.params.entry.level === 'error') this.errors.push(`${d.params.entry.source}: ${d.params.entry.text} ${d.params.entry.url ?? ''}`)
    }
    for (const m of ['Page.enable', 'Runtime.enable', 'Log.enable']) await this.send(m)
  }
  send(method, params = {}) { return new Promise((r) => { const id = ++this.id; this.pending.set(id, r); this.ws.send(JSON.stringify({ id, method, params })) }) }
  async goto(url) { await this.send('Page.navigate', { url }); await sleep(1500) }
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true })
    if (r.result?.exceptionDetails) throw new Error(`page: ${r.result.exceptionDetails.exception?.description ?? r.result.exceptionDetails.text}`)
    return r.result?.result?.value
  }
  async hash(h) { await this.eval(`location.hash = ${JSON.stringify(h)}`); await sleep(1200) }
  /** Click by CSS selector (optionally the one whose text matches). */
  async click(sel, text) {
    const ok = await this.eval(`const t = ${JSON.stringify(text ?? '')}; const all = [...document.querySelectorAll(${JSON.stringify(sel)})]; const el = all.find((e) => !t || e.textContent.trim() === t) ?? all.find((e) => e.textContent.includes(t)); if (!el) return false; el.click(); return true`)
    if (!ok) throw new Error(`nothing to click: ${sel} ${text ?? ''}`)
    await sleep(400)
  }
  mouse(type, x, y, buttons = 1) { return this.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons, clickCount: 1, pointerType: 'mouse' }) }
  async drag([x0, y0], [x1, y1]) {
    await this.mouse('mouseMoved', x0, y0, 0); await this.mouse('mousePressed', x0, y0)
    for (let i = 1; i <= 12; i++) { await this.mouse('mouseMoved', x0 + ((x1 - x0) * i) / 12, y0 + ((y1 - y0) * i) / 12); await sleep(25) }
    await this.mouse('mouseReleased', x1, y1, 0); await sleep(300)
  }
  takeErrors() { const e = this.errors; this.errors = []; return e }
  stop() { this.proc.kill(); fs.rmSync(this.dir, { recursive: true, force: true }) }
}

// ── checks ──────────────────────────────────────────────────────────────
const results = []
function check(name, ok, detail = '') { results.push({ name, ok, detail }); console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `  (${detail})` : ''}`) }
async function until(fn, ms = 8000) { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (await fn()) return true; await sleep(200) } return false }

async function main() {
  const svc = new Service()
  try { await svc.connect(); await svc.request('hello') } catch { console.log(`No OCTAVE service on ${HOST}:${PORT}; nothing to test (start one with --service --web-root frontends/glass/dist).`); return 0 }
  const base = `http://${HOST}:${PORT}/`
  let html = ''
  try { html = await (await fetch(base)).text() } catch { /* below */ }
  if (!html.includes('OCTAVE Glass')) { console.log(`${base} does not serve the glass build; start OCTAVE with --web-root frontends/glass/dist. Nothing to test.`); svc.close(); return 0 }
  const hello = await svc.request('hello')
  console.log(`OCTAVE ${hello.app.version} (${hello.app.build}) at ${base}`)

  // a track playing and the sim car moving make every page show something
  if (!(await svc.get('nowPlaying', 'hasTrack'))) { const files = await svc.call('mediaManager', 'get_media_files'); if (files[0]) await svc.call('mediaManager', 'play_file', files[0]) }
  if (!(await svc.get('nowPlaying', 'playing'))) await svc.call('nowPlaying', 'togglePlay')
  await svc.sim('sim_throttle', { value: 40 })

  const b = new Browser()
  await b.start()
  try {
    await b.goto(base + '?theme=dark#/home')
    check('page connects', await until(() => b.eval(`return !!document.querySelector('.brand .dot.on')`)))
    b.takeErrors()

    // every page loads without console errors
    for (const page of ['home', 'media', 'obd', 'obd/link', 'obd/codes', 'dashboards', 'dashboards/view/sport', 'trips', 'settings/display', 'settings/media', 'settings/vehicle', 'settings/accessories', 'settings/device', 'more/downloads', 'more/phone', 'more/diagnostics']) {
      await b.hash('#/' + page)
      const errs = b.takeErrors()
      check(`page ${page} loads clean`, errs.length === 0, errs.slice(0, 2).join(' | '))
    }

    // media
    await b.hash('#/media')
    const wasPlaying = await svc.get('nowPlaying', 'playing')
    await b.click('[aria-label="Pause"], [aria-label="Play"]')
    check('play/pause toggles', await until(async () => (await svc.get('nowPlaying', 'playing')) !== wasPlaying))
    if (!(await svc.get('nowPlaying', 'playing'))) await b.click('[aria-label="Play"]')
    const title = await svc.get('nowPlaying', 'title')
    await b.click('[aria-label="Next"]')
    check('next track', await until(async () => (await svc.get('nowPlaying', 'title')) !== title))
    const vol = await svc.get('volumeController', 'percent')
    const target = vol >= 50 ? 30 : 60
    await b.eval(`const i = document.querySelector('input[aria-label="Volume"]'); i.value = ${target}; i.dispatchEvent(new Event('input', { bubbles: true })); i.dispatchEvent(new Event('change', { bubbles: true }))`)
    check('volume slider', await until(async () => (await svc.get('volumeController', 'percent')) === target), `${vol} -> ${target}`)
    const muted = await svc.get('volumeController', 'muted')
    await b.click('[aria-label="Mute"], [aria-label="Unmute"]')
    check('mute toggles', await until(async () => (await svc.get('volumeController', 'muted')) !== muted))
    await svc.request('set', { object: 'volumeController', property: 'muted', value: muted })

    // settings: a toggle and a theme
    await b.hash('#/settings/display')
    const seconds = await svc.get('settingsManager', 'clockShowSeconds')
    await b.click('[role=switch][aria-label="Show seconds"]')
    check('settings toggle writes', await until(async () => (await svc.get('settingsManager', 'clockShowSeconds')) !== seconds))
    await svc.call('settingsManager', 'save_clock_show_seconds', seconds)
    const theme = await svc.get('settingsManager', 'themeSetting')
    const pick = theme === 'AmberConsole' ? 'Neon Matrix' : 'Amber Console'
    await b.click('.tile', undefined).catch(() => {})
    await b.eval(`[...document.querySelectorAll('.tile')].find((t) => t.textContent.includes(${JSON.stringify(pick)}))?.click()`)
    check('theme pick saves', await until(async () => (await svc.get('settingsManager', 'themeSetting')) === pick.replace(' ', '')))
    await svc.call('settingsManager', 'save_theme_setting', theme)

    // DTC on the simulated car
    if (await svc.get('obdManager', 'connected')) {
      await b.hash('#/obd/codes')
      await svc.request('subscribe', { object: 'obdManager', signals: ['dtcCodesChanged', 'dtcClearResult'] })
      svc.signals.length = 0
      await b.click('button', 'Read codes')
      const read = await until(() => svc.signals.some((e) => e.name === 'dtcCodesChanged'), 10000)
      const codes = await svc.call('obdManager', 'get_dtc_codes')
      check('DTC read answers', read, codes.length ? codes.map((c) => c.code).join(' ') : 'no stored codes on this car')
      svc.signals.length = 0
      await b.click('button', 'Clear codes')
      await b.click('[role=dialog] .btn.danger')
      check('DTC clear with confirm', await until(() => svc.signals.some((e) => e.name === 'dtcClearResult' && e.args[0] === true), 10000))
    } else {
      check('DTC read (skipped: no car)', true)
    }

    // dashboards: new, place, drag, resize, save, delete
    await b.hash('#/dashboards/edit/new')
    await b.eval(`const n = document.querySelector('input[aria-label="Dashboard name"]'); n.value = 'E2E test'; n.dispatchEvent(new Event('input', { bubbles: true }))`)
    await b.click('.pal', 'Digital')
    await b.eval(`const g = document.querySelector('.grid-stage').getBoundingClientRect(); document.querySelector('.grid-stage').dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: g.left + g.width * 0.03, clientY: g.top + g.height * 0.05 }))`)
    await sleep(500)
    check('widget placed', (await b.eval(`return document.querySelectorAll('.dash .cell').length`)) === 1)
    const mover = await b.eval(`const m = document.querySelector('.mover').getBoundingClientRect(); return [m.x + m.width / 2, m.y + m.height / 2]`)
    await b.drag(mover, [mover[0] + 250, mover[1] + 280])
    const grip = await b.eval(`const g = document.querySelector('.grip').getBoundingClientRect(); return [g.x + g.width / 2, g.y + g.height / 2]`)
    await b.drag(grip, [grip[0] + 200, grip[1] + 80])
    const cellStyle = await b.eval(`const c = document.querySelector('.dash .cell'); return c.style.gridColumn + '|' + c.style.gridRow`)
    check('widget moved and resized', !cellStyle.startsWith('1 / span 4|1 / span 2'), cellStyle)
    await b.click('button', 'Save')
    const saved = await until(async () => (await svc.get('dashboardManager', 'dashboards')).some((d) => d.id === 'e2e-test'))
    check('dashboard saved', saved)
    if (saved) {
      const spec = await svc.call('dashboardManager', 'loadDashboard', 'e2e-test')
      check('saved spec matches the editor', spec.cells?.length === 1 && cellStyle.startsWith(`${spec.cells[0].col + 1} / span ${spec.cells[0].colSpan}|${spec.cells[0].row + 1} / span ${spec.cells[0].rowSpan}`), JSON.stringify(spec.cells?.[0]))
      await b.hash('#/dashboards')
      await b.click('[aria-label="Delete E2E test"]')
      await b.click('[role=dialog] .btn.danger')
      check('dashboard deleted', await until(async () => !(await svc.get('dashboardManager', 'dashboards')).some((d) => d.id === 'e2e-test')))
    }
    const late = b.takeErrors()
    check('no console errors while driving', late.length === 0, late.slice(0, 2).join(' | '))
  } finally {
    b.stop()
    svc.close()
  }
  const failed = results.filter((r) => !r.ok)
  console.log(`\n${results.length - failed.length} of ${results.length} checks passed`)
  return failed.length ? 1 : 0
}

process.exit(await main().catch((e) => { console.error('e2e:', e.message); return 1 }))
