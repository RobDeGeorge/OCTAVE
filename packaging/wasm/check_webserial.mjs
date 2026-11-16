// The Web Serial OBD transport (src/platform/wasm/webserial.cpp), end to end
// in headless Chromium against a fake ELM327: the page's navigator.serial is
// replaced by one whose port answers like an adapter on a K-line car, then
// obdAdapterFinder.pickSerialPort() has to list it, open it at 38400 baud and
// get the real OBDManager connected and polling; after the cable is "pulled"
// and "plugged back in" (a new port object with the same USB ids) OBDManager's
// retries have to reopen it with no new pick; and connecting the simulated
// car has to close the port.
//
//   python3 packaging/wasm/serve.py /tmp/octave-site 8080 &
//   node packaging/wasm/check_webserial.mjs http://127.0.0.1:8080/
//
// Proves the C++ and JS halves and the hand-off to OBDManager, not a real
// cable (see TODO/obd-adapter-discovery.md).

import { Browser, sleep } from './headless.mjs'

const base = process.argv[2] ?? 'http://127.0.0.1:8080/'

// Runs in the page before its own scripts
const fakeSerial = `(() => {
  const enc = new TextEncoder(), dec = new TextDecoder()
  const state = globalThis.__serial = { commands: [], opens: [], closes: 0, ports: [], rpm: 0 }
  const answer = (cmd) => {
    const c = cmd.replace(/\\s+/g, '').toUpperCase()
    if (c === 'ATZ' || c === 'ATI') return 'ELM327 v1.5'
    if (c === 'ATRV') return '12.6V'
    if (c === 'ATDPN') return '3'                       // ISO 9141-2: one PID per request
    if (c.startsWith('AT')) return 'OK'
    if (c === '0100') return '410008180000'             // supports 05, 0C, 0D
    if (/^010C1?$/.test(c)) {
      state.rpm = 800 + (state.commands.length * 37) % 3000
      return '410C' + (state.rpm * 4).toString(16).toUpperCase().padStart(4, '0')
    }
    if (/^010D1?$/.test(c)) return '410D32'
    if (/^01051?$/.test(c)) return '41057B'
    return 'NO DATA'
  }
  const makePort = () => {
    const port = {
      gone: false, open_: false, readable: null, writable: null,
      getInfo: () => ({ usbVendorId: 0x0403, usbProductId: 0x6001 }),
      addEventListener() {},
      async open(options) {
        if (port.gone) throw new DOMException('Failed to open serial port.', 'NetworkError')
        if (port.open_) throw new DOMException('The port is already open.', 'InvalidStateError')
        port.open_ = true
        state.opens.push(options.baudRate)
        let pending = ''
        port.readable = new ReadableStream({ start(c) { port.ctl = c } })
        port.writable = new WritableStream({
          write(chunk) {
            pending += dec.decode(chunk)
            for (let i; (i = pending.indexOf('\\r')) >= 0;) {
              const cmd = pending.slice(0, i)
              pending = pending.slice(i + 1)
              state.commands.push(cmd)
              setTimeout(() => { try { port.ctl.enqueue(enc.encode(answer(cmd) + '\\r\\r>')) } catch (e) {} }, 4)
            }
          },
        })
      },
      async close() {
        if (port.readable?.locked || port.writable?.locked) throw new TypeError('Cannot close a locked stream')
        port.open_ = false; port.readable = null; port.writable = null
        ++state.closes
      },
    }
    state.ports.push(port)
    return port
  }
  let current = makePort()
  // The cable pulled, then plugged back in: the old object is dead, the
  // browser has a new one for the same adapter
  state.unplug = () => {
    const old = current
    old.gone = true
    try { old.ctl.error(new DOMException('The device has been lost.', 'NetworkError')) } catch (e) {}
    old.readable = null
    current = makePort()
  }
  Object.defineProperty(navigator, 'serial', { configurable: true, value: {
    requestPort: async () => current,
    getPorts: async () => [current],
  } })
})()`

const b = new Browser()
let failed = false
try {
  await b.start()
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: fakeSerial })
  await b.send('Page.navigate', { url: new URL('index.html', base).href })
  await b.waitFor(`!!globalThis.octaveService`, 180000, 'the core to offer Module.octaveService')
  await b.eval(`globalThis.__own = OctaveFrontendHost.connect(globalThis.octaveService), true`)
  const get = (object, property) => b.eval(`globalThis.__own.request('get', { object: ${JSON.stringify(object)}, property: ${JSON.stringify(property)} })`)
  const waitConnected = (ms, what) => b.waitFor(`globalThis.__own.request('get', { object: 'obdManager', property: 'connected' })`, ms, what)
  const polled = () => b.eval(`globalThis.__serial.commands.filter((c) => /^010C/.test(c)).length`)

  if (!(await get('obdAdapterFinder', 'canPickSerial'))) throw new Error('canPickSerial is false with navigator.serial present')
  await b.eval(`globalThis.__own.call('obdAdapterFinder', 'pickSerialPort')`)
  await waitConnected(60000, 'OBDManager to connect over the fake serial port')
  const devices = await get('obdAdapterFinder', 'devices')
  const row = devices.find((d) => d.kind === 'serial')
  if (!row || row.name !== 'USB adapter (FTDI)') throw new Error(`no serial row named for its chip: ${JSON.stringify(devices)}`)
  const opens = await b.eval(`globalThis.__serial.opens`)
  if (opens.length !== 1 || opens[0] !== 38400) throw new Error(`expected one open at 38400 baud, saw ${JSON.stringify(opens)}`)
  const log = (await get('obdManager', 'connectionLog')).join(' | ')
  if (!/Opened webserial:\/\/port1 \(38400 baud\)/.test(log)) throw new Error(`the connection log doesn't name the port: ${log}`)
  const before = await polled()
  await sleep(4000)
  const after = await polled()
  if (after <= before) throw new Error(`no polling after the connect (${before} -> ${after} RPM requests)`)
  console.log(`PASS connect: ${row.name}, 38400 baud, ${after} RPM requests, status "${await get('obdAdapterFinder', 'status')}"`)

  await b.eval(`globalThis.__serial.unplug()`)
  await b.waitFor(`globalThis.__own.request('get', { object: 'obdManager', property: 'connected' }).then((c) => !c)`, 30000, 'OBDManager to notice the pulled cable')
  await waitConnected(90000, 'OBDManager to reconnect after the cable came back')
  const reopened = await b.eval(`globalThis.__serial.opens.length`)
  if (reopened < 2) throw new Error('reconnected without opening the new port')
  console.log(`PASS replug: reconnected on the new port object with no new pick (${reopened} opens)`)

  await b.eval(`globalThis.__own.call('obdAdapterFinder', 'connectAdapter', 'sim://elm327')`)
  await b.waitFor(`globalThis.__own.request('get', { object: 'obdManager', property: 'simulated' })`, 30000, 'the simulated car to take over')
  await b.waitFor(`globalThis.__serial.ports.every((p) => !p.open_)`, 10000, 'the serial port to close')
  const quiet = await b.eval(`globalThis.__serial.commands.length`)
  await sleep(3000)
  if ((await b.eval(`globalThis.__serial.commands.length`)) !== quiet) throw new Error('commands still reach the serial port under the simulated car')
  console.log('PASS simulated car: the serial port closed and went quiet')
} catch (e) {
  failed = true
  console.log(`FAIL: ${e.message}`)
  try { console.log('    commands: ' + (await b.eval(`globalThis.__serial.commands.slice(-12).join(' ')`))) } catch { /* no page */ }
  for (const line of b.console.slice(-20)) console.log('    ' + line)
} finally {
  b.stop()
}
process.exit(failed ? 1 : 0)
