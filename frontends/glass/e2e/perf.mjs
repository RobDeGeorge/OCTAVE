// perf.mjs URL seconds -- Chromium (2 cores via taskset) on URL; after a 6 s
// warm-up, samples CPU of every chromium process for N seconds from /proc.
// Prints renderer / gpu / browser / total CPU as % of one core.
import { spawn, execSync } from 'node:child_process'
import fs from 'node:fs'
const [url, secs = '30'] = process.argv.slice(2)
const port = 9300 + Math.floor(Math.random() * 500)
const dir = '/tmp/claude-1000/perf-' + port
const chrome = spawn('taskset', ['-c', '0,1', 'chromium', '--headless', '--disable-gpu', '--hide-scrollbars', `--remote-debugging-port=${port}`, '--window-size=1280,800', `--user-data-dir=${dir}`, 'about:blank'], { stdio: 'ignore' })
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
let target
for (let i = 0; i < 50 && !target; i++) { await sleep(200); try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page') } catch {} }
const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise((r) => (ws.onopen = r))
let id = 0; const pending = new Map()
ws.onmessage = (m) => { const d = JSON.parse(m.data); if (pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id) } }
const send = (method, params = {}) => new Promise((r) => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })) })
await send('Page.navigate', { url })
await sleep(6000)
function tree(root) { const out = [root]; for (const p of out) { try { for (const c of execSync(`pgrep -P ${p}`).toString().trim().split('\n').filter(Boolean)) out.push(Number(c)) } catch {} } return out }
function kind(pid) { try { const c = fs.readFileSync(`/proc/${pid}/cmdline`).toString(); const m = /--type=(\w+)/.exec(c); return m ? m[1] : 'browser' } catch { return 'gone' } }
function cpu(pid) { try { const f = fs.readFileSync(`/proc/${pid}/stat`).toString().split(') ')[1].split(' '); return Number(f[11]) + Number(f[12]) } catch { return 0 } }
const pids = tree(chrome.pid)
const before = Object.fromEntries(pids.map((p) => [p, cpu(p)]))
await sleep(Number(secs) * 1000)
const hz = 100
const by = {}
for (const p of pids) { const k = kind(p); by[k] = (by[k] ?? 0) + (cpu(p) - before[p]) / hz / Number(secs) * 100 }
const total = Object.values(by).reduce((a, b) => a + b, 0)
console.log(`${url.replace(/^.*#/, '#').padEnd(32)} renderer ${(by.renderer ?? 0).toFixed(1).padStart(6)}%  gpu ${(by.gpu ?? 0).toFixed(1).padStart(6)}%  browser ${(by.browser ?? 0).toFixed(1).padStart(6)}%  total ${total.toFixed(1).padStart(6)}%   (of one core, ${secs} s)`)
chrome.kill(); fs.rmSync(dir, { recursive: true, force: true }); process.exit(0)
