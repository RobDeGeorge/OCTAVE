# octave-client (TypeScript)

A client for OCTAVE's service API for browsers and Node 22+: TypeScript, ESM, no dependencies. The objects and their types come from [`docs/api/`](../../docs/api/README.md) and are generated into `src/types.ts`, so a call that does not exist or takes the wrong arguments fails at compile time. The protocol is in [`wiki/utilities.html#service-protocol`](../../wiki/utilities.html#service-protocol); this client speaks version 1.

```bash
cd sdk/typescript
npm install && npm run build        # -> dist/ (ESM + .d.ts)
npm test                            # types match docs/api, the facade type-checks, the client against a fake service
```

Use it from a sibling project as a path dependency (`"octave-client": "file:../octave/sdk/typescript"`) or copy `dist/`.

## Use

```ts
import { OctaveClient, obj } from 'octave-client'

const client = new OctaveClient()          // ws://<this page's host>/ in a browser, ws://127.0.0.1:9876/ in Node; or { url }
client.connect()                           // reconnects with backoff until close()
await client.whenOnline()

const vol = obj(client, 'volumeController')   // typed from docs/api/volumeController.json
await vol.call('applyVolume', 40)             // vol.call('applyVolume', 'loud') does not compile
const muted = await vol.get('muted')
vol.property('percent', (v) => console.log('volume', v))   // now, and on every change

client.params(['RPM', 'SPEED'], (values) => console.log(values), 100)   // live readings, native units
const stop = client.signal('obdManager', 'dtcCodesChanged', (codes) => console.log(codes))
client.model('downloadManager', 'searchResults', (rows) => render(rows))  // a list model, kept current

await client.stream('phoneMirrorManager', 'video', { onRecord: (r) => decoder.feed(r) })   // binary media records
```

`OctaveClient` has `request(op, args)` for any op, `get` / `set` / `call` / `rows` / `describe` (cached), `subscribe(object, {properties, signals, throttleMs}, handler)` with `property`, `signal`, `params` and `model` on top of it, `stream`, `on('status' | 'hello')`, `whenOnline`. Subscriptions survive a reconnect (they are issued again on the new connection, so the current values arrive again); a media stream ends with its connection and is opened again by the caller. Replies are matched by id; a request made while connecting waits for the link.

Browsers may only connect from a page the service served (the Origin rule), so a web app is served by the app itself with `--web-root`. Node sends no Origin and connects from anywhere the port is reachable.

## Types

`npm run generate` reads `docs/api/*.json` and writes `src/types.ts`: one interface per object with `properties`, `methods` (argument tuples, overloads as a union, return types) and `signals` (argument tuples); Q_ENUMs become string unions. Qt types map to `string`, `number`, `boolean`, `string[]`, `unknown[]`, `Record<string, unknown>` and `unknown`; a list-model property reads as `ModelSummary`. `npm test` fails when the committed `src/types.ts` does not match `docs/api`, so regenerate it in the same commit as the API (`./build/octave --dump-api docs/api` first).

## Example

`examples/now_playing.mjs` follows the track and takes `next`, `previous`, `play`, `volume N`, `mute` from the keyboard: `npm run example` against a running app (`OCTAVE_URL` for another address).
