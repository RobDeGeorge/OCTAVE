// Headless Chromium over the DevTools protocol, for the page checks in this
// folder (check_playground.mjs, check_webserial.mjs). Needs Node 22+
// (WebSocket) and chromium / google-chrome on PATH (CHROME=).

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

export const CHROME = process.env.CHROME ?? ['chromium', 'chromium-browser', 'google-chrome', 'chrome'].find(onPath) ?? 'chromium'
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

function onPath(cmd) {
  return (process.env.PATH ?? '').split(path.delimiter).some((d) => {
    try { fs.accessSync(path.join(d, cmd), fs.constants.X_OK); return true } catch { return false }
  })
}

export class Browser {
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
