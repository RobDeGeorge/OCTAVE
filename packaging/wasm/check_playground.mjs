// The frontend playground, end to end in headless Chromium: the real core
// (wasm) starts, the simulated car drives, and a bundled web frontend in the
// iframe shows live values it got over Module.octaveService.
//
//   python3 packaging/wasm/serve.py /tmp/octave-site 8080 &
//   node packaging/wasm/check_playground.mjs http://127.0.0.1:8080/ [frontend ...]
//
// Default frontends: hello-car-js (and glass when the site has it). Exits 0
// when each showed a changing RPM; SHOT_DIR=dir keeps a screenshot of each.
// Then index.html: Settings > Device > Frontends lists the site's frontends
// (frontendManager.frontends), Use (setActive) shows one over the built-in
// screen with live values, and its OCTAVE button brings the screen back.
// Needs Node 22+ (WebSocket) and chromium / google-chrome on PATH (CHROME=).

import fs from 'node:fs'
import path from 'node:path'

import { Browser, sleep } from './headless.mjs'

const base = process.argv[2] ?? 'http://127.0.0.1:8080/'
const wanted = process.argv.slice(3)

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

// The server may still be starting (CI starts it just before this)
let site = false
for (let i = 0; ; i++) {
  try { site = (await fetch(new URL('frontends/glass/index.html', base))).ok; break } catch (e) {
    if (i >= 60) throw new Error(`no server at ${base}: ${e.cause?.code ?? e.message}`)
    await sleep(500)
  }
}
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

  // The demo page: a frontend chosen as the screen
  const demo = 'hello-car-js'
  console.log(`… index.html: ${demo} from Settings > Device > Frontends`)
  try {
    await b.send('Page.navigate', { url: new URL('index.html', base).href })
    await b.waitFor(`!!globalThis.octaveService`, 180000, 'the core to offer Module.octaveService')
    const listed = await b.eval(`(async () => {
      const own = OctaveFrontendHost.connect(globalThis.octaveService)
      globalThis.__check = own
      const list = await own.request('get', { object: 'frontendManager', property: 'frontends' })
      return list.map((f) => f.id + ':' + f.kind + ':' + f.compatible)
    })()`)
    const catalog = await (await fetch(new URL('frontends/catalog.json', base))).json()
    for (const f of catalog) {
      if (!listed.includes(`${f.id}:web:true`)) throw new Error(`${f.id} not listed as a usable web frontend (listed: ${listed.join(', ')})`)
    }
    const id = catalog.find((f) => f.slug === demo).id
    await b.eval(`globalThis.__check.call('frontendManager', 'setActive', ${JSON.stringify(id)})`)
    // Shown, and on top: what is at the middle of the page is the frontend, not Qt's screen
    await b.waitFor(`document.getElementById('back').style.display === 'block'
      && document.elementFromPoint(innerWidth / 2, innerHeight / 2) === document.getElementById('frontend')`, 30000, 'the frontend over the screen')
    const seen = new Set()
    const until = Date.now() + 60000
    let text = ''
    while (Date.now() < until && seen.size < 2) {
      text = await frameText(b)
      const rpm = rpmIn(text)
      if (Number.isFinite(rpm) && rpm > 0) seen.add(rpm)
      await sleep(700)
    }
    if (process.env.SHOT_DIR) await b.shot(path.join(process.env.SHOT_DIR, `index-${demo}.png`))
    if (seen.size < 2) throw new Error(`no live RPM (saw ${[...seen].join(', ') || 'none'}); its text: ${text.slice(0, 300)}`)
    if (!(await b.eval(`location.search.includes('frontend=${demo}')`))) throw new Error('?frontend= not set')
    await b.eval(`document.getElementById('back').click()`)
    await b.waitFor(`document.getElementById('frontend').style.display === 'none'
      && document.getElementById('screen').contains(document.elementFromPoint(innerWidth / 2, innerHeight / 2))`, 15000, 'the built-in screen back')
    const after = await b.eval(`globalThis.__check.request('get', { object: 'frontendManager', property: 'activeId' })`)
    if (after !== '') throw new Error(`activeId is still ${after}`)
    console.log(`PASS index.html: listed ${catalog.length}, showed ${demo} (RPM ${[...seen].join(' -> ')}), OCTAVE button back`)
  } catch (e) {
    failures.push('index.html')
    console.log(`FAIL index.html: ${e.message}`)
    for (const line of b.console.slice(-15)) console.log('    ' + line)
  }
} finally {
  b.stop()
}
process.exit(failures.length ? 1 : 0)
