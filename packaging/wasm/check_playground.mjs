// The frontend playground, end to end in headless Chromium: the real core
// (wasm) starts, the simulated car drives, and a bundled web frontend in the
// iframe shows live values it got over Module.octaveService.
//
//   python3 packaging/wasm/serve.py /tmp/octave-site 8080 &
//   node packaging/wasm/check_playground.mjs http://127.0.0.1:8080/ [frontend ...]
//
// Default frontends: hello-car-js (and glass when the site has it). Exits 0
// when each showed a changing RPM; SHOT_DIR=dir keeps a screenshot of each.
// Needs Node 22+ (WebSocket) and chromium / google-chrome on PATH (CHROME=).

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const base = process.argv[2] ?? 'http://127.0.0.1:8080/'
const wanted = process.argv.slice(3)
const CHROME = process.env.CHROME ?? ['chromium', 'chromium-browser', 'google-chrome', 'chrome'].find(onPath) ?? 'chromium'
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

function onPath(cmd) {
  return (process.env.PATH ?? '').split(path.delimiter).some((d) => {
    try { fs.accessSync(path.join(d, cmd), fs.constants.X_OK); return true } catch { return false }
  })
}

class Browser {
  async start() {
    this.dir = fs.mkdtempSync(path.join(os.tmpdir(), 'playground-check-'))
    const port = 9400 + Math.floor(Math.random() * 400)
    this.proc = spawn(CHROME, ['--headless', '--hide-scrollbars', '--no-first-run', '--enable-unsafe-swiftshader',
      `--remote-debugging-port=${port}`, '--window-size=1400,860', `--user-data-dir=${this.dir}`, 'about:blank'], { stdio: 'ignore' })
    let target
    for (let i = 0; i < 75 && !target; i++) {
      await sleep(200)
      try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page') } catch { /* not up */ }
    }
    if (!target) throw new Error(`could not start ${CHROME}`)
    this.ws = new WebSocket(target.webSocketDebuggerUrl)
    await new Promise((resolve, reject) => { this.ws.onopen = resolve; this.ws.onerror = () => reject(new Error('devtools socket')) })
    this.id = 0; this.pending = new Map(); this.console = []
    this.ws.onmessage = (m) => {
      const d = JSON.parse(m.data)
      // The sandboxed iframe runs out of process: its own target, attached below
      if (d.method === 'Target.attachedToTarget' && d.params.targetInfo.type === 'iframe') this.frameSession = d.params.sessionId
      if (d.method === 'Target.detachedFromTarget' && d.params.sessionId === this.frameSession) this.frameSession = null
      if (d.id && this.pending.has(d.id)) { this.pending.get(d.id)(d); this.pending.delete(d.id); return }
      if (d.method === 'Runtime.exceptionThrown') this.console.push('exception: ' + (d.params.exceptionDetails?.exception?.description ?? d.params.exceptionDetails?.text))
      if (d.method === 'Runtime.consoleAPICalled' && (d.params.type === 'error' || d.params.type === 'warning')) {
        const text = d.params.args.map((a) => a.value ?? a.description).join(' ')
        // The core's own log goes to stderr as console errors: keep its warnings only
        if (!/^\d{4}-\d\d-\d\d .*\| (INFO|DEBUG) /.test(text)) this.console.push(d.params.type + ': ' + text)
      }
    }
    await this.send('Runtime.enable')
    await this.send('Page.enable')
    await this.send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true })
  }
  send(method, params = {}, sessionId) {
    const id = ++this.id
    return new Promise((resolve, reject) => {
      this.pending.set(id, (d) => d.error ? reject(new Error(`${method}: ${d.error.message}`)) : resolve(d.result))
      this.ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }))
    })
  }
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text)
    return r.result.value
  }
  async waitFor(expr, ms, what) {
    const until = Date.now() + ms
    let last
    while (Date.now() < until) {
      try { last = await this.eval(expr); if (last) return last } catch { /* page navigating */ }
      await sleep(500)
    }
    throw new Error(`timed out (${ms / 1000} s) waiting for ${what}`)
  }
  async shot(file) {
    const r = await this.send('Page.captureScreenshot', { format: 'png' })
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'))
  }
  stop() {
    try { this.ws?.close() } catch { /* gone */ }
    this.proc?.kill()
    try { fs.rmSync(this.dir, { recursive: true, force: true }) } catch { /* in use */ }
  }
}

// The text of the frontend's document. The iframe is sandboxed (an opaque
// origin), so the playground page can't read it: DevTools evaluates in the
// child frame itself
async function frameText(b) {
  if (b.frameSession) {
    try {
      const r = await b.send('Runtime.evaluate', { expression: 'document.body ? document.body.innerText : ""', returnByValue: true }, b.frameSession)
      return r.result?.value ?? ''
    } catch { /* the frame is reloading */ }
  }
  // An in-process frame (a browser that keeps it in the page's process)
  const { frameTree } = await b.send('Page.getFrameTree')
  const child = (frameTree.childFrames ?? [])[0]
  if (!child) return ''
  const { executionContextId } = await b.send('Page.createIsolatedWorld', { frameId: child.frame.id, worldName: 'check' })
  const r = await b.send('Runtime.evaluate', { expression: 'document.body ? document.body.innerText : ""', contextId: executionContextId, returnByValue: true })
  return r.result?.value ?? ''
}

// RPM as each bundled frontend shows it: the number on the tile labelled
// RPM ("819 / Engine RPM", "2988 / RPM")
function rpmIn(text) {
  const m = /(\d[\d,]*)[^\d]{0,24}RPM/.exec(text)
  return m ? Number(m[1].replace(/,/g, '')) : NaN
}

const site = await (await fetch(new URL('frontends/glass/index.html', base))).ok
const frontends = wanted.length ? wanted : ['hello-car-js', ...(site ? ['glass'] : [])]
const failures = []
const b = new Browser()
try {
  await b.start()
  for (const fe of frontends) {
    const url = new URL(`playground.html?frontend=${encodeURIComponent(fe)}`, base).href
    console.log(`… ${fe}: ${url}`)
    await b.send('Page.navigate', { url })
    try {
      // The core: up to 3 minutes the first time (80 MB, compile)
      await b.waitFor(`!!globalThis.octaveService`, 180000, 'the core to offer Module.octaveService')
      await b.waitFor(`document.getElementById('frontend').style.display === 'block'`, 30000, 'the frontend to load')
      // Two different RPM readings: values arrive live, not once
      const seen = new Set()
      const until = Date.now() + 60000
      let text = ''
      while (Date.now() < until && seen.size < 2) {
        text = await frameText(b)
        const rpm = rpmIn(text)
        if (Number.isFinite(rpm) && rpm > 0) seen.add(rpm)
        await sleep(700)
      }
      if (process.env.SHOT_DIR) {
        fs.mkdirSync(process.env.SHOT_DIR, { recursive: true })
        await b.shot(path.join(process.env.SHOT_DIR, `playground-${fe}.png`))
      }
      if (seen.size < 2) throw new Error(`no live RPM in the frontend (saw ${[...seen].join(', ') || 'none'}); its text: ${text.slice(0, 300)}`)
      console.log(`PASS ${fe}: RPM ${[...seen].join(' -> ')}`)
    } catch (e) {
      failures.push(fe)
      console.log(`FAIL ${fe}: ${e.message}`)
      for (const line of b.console.slice(-15)) console.log('    ' + line)
    }
    b.console = []
  }
} finally {
  b.stop()
}
process.exit(failures.length ? 1 : 0)
