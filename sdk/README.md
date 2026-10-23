# OCTAVE SDK

Clients for OCTAVE's service API: read and drive the app (media, volume, live OBD and IMU values, settings, dashboards) from another program, with no Qt. The objects and their types are in [`docs/api/`](../docs/api/README.md); the protocol envelope (ops, replies, errors, events, stream records, HTTP routes, pairing) is [`docs/api/protocol.json`](../docs/api/protocol.json), JSON Schema checked against the server, and in prose in [`wiki/utilities.html#service-protocol`](../wiki/utilities.html#service-protocol). Writing a client in another language: start from `protocol.json` and the `<object>.json` files; `sdk/python/tests/test_protocol_spec.py` shows how to check a client's idea of the protocol against a running app.

Turn the service on first. It is off by default, and listens on 127.0.0.1:9876:

```bash
./build/octave --service          # with the normal UI
./build/octave --headless         # no UI: the managers and the service only
```

(Or turn on Settings > Device > Service. To reach it from another machine add `--service-bind 0.0.0.0`: clients there must pair first, with the code that card shows; programs on the head unit itself need nothing. Traffic is not encrypted, so only on a network you control.)

## Browser: `web/`

`web/index.html` is a reference client in plain JavaScript (no build, no libraries): live gauges, now playing with transport controls, volume, and an API explorer. The gauges show native units (`parameters.json`), not the user's display units. The app serves it at **http://127.0.0.1:9876/** and the page talks back over a WebSocket on the same port. One JSON request, reply or event per text frame, the same messages as the line protocol.

A browser page from anywhere else can't connect. The app refuses a WebSocket whose `Origin` isn't its own page, and refuses a host given by name rather than by address (`src/service/websocket.h`). Scripts send no `Origin` and are not affected. So open the page by address (`http://192.168.x.y:9876/`), not by a name such as `octave.local`: names are refused on purpose, since a website could point one at the car. With `--service-bind` beyond loopback the page shows a pairing form until you enter the code from Settings > Device > Service; it keeps the token in the browser and loads covers with short-lived media tickets.

## Python: `python/`

`python/octave_client` is a stdlib-only client (Python 3.8+):

```python
import sys; sys.path.insert(0, "sdk/python")
from octave_client import Client, launch_headless

with launch_headless() as app, Client(port=app.port) as octave:   # or Client() for a running app
    print(octave.hello())
    octave.call("volumeController", "applyVolume", 30)
    octave.subscribe("paramStore", properties=["RPM", "SPEED"], throttle_ms=100)
    for event in octave.events(timeout=5):
        print(event)
```

`Client` has `hello`, `describe`, `get`, `set`, `call`, `rows`, `subscribe` / `unsubscribe` / `events`, and `request(op, ...)` for anything else. The objects behave as they do for the touch screen: `volumeController.toggleMute()`, for one, ignores a second toggle within 150 ms (the knob's debounce), so a script that wants a definite state sets the writable property instead (`set("volumeController", "muted", False)`). Replies are matched by id, so events can interleave and requests can come from several threads. `launch_headless()` starts `octave --headless` on a free port with a throwaway config (the binary from `$OCTAVE_BIN`, else this checkout's `build/`).

Examples: `python/examples/live_gauges.py` (readings with titles and units) and `python/examples/now_playing.py` (follow the track; `next`, `play`, `volume 40`, `mute`).

Over the network: `token = Client(host).pair("123456", "My script")` once, then `Client(host, token=token)`. Video: `octave.stream("phoneMirrorManager", "video")`, after which `events()` also yields `{"event": "record", "stream", "flags", "pts", "key", "config", "data": bytes}` (H.264 Annex-B access units, every keyframe with its SPS / PPS; `docs/api/README.md`, "Streams"); `unstream(id)` stops it.

Tests: `python -m unittest discover -s sdk/python/tests` runs the client against a headless app (the stream test uses the dev-only `sim_video` and skips without the dev tools) and `test_protocol_spec.py`, which checks `docs/api/protocol.json` against that app: the same ops both ways, and replies, errors and events validating against the spec's schemas. ctest runs it as `sdk_python`.

## TypeScript: `typescript/`

`typescript/` is an npm package (`octave-client`: ESM, no dependencies, browsers and Node 22+) with the same client as a library plus types generated from `docs/api/`, so `obj(client, "volumeController").call("applyVolume", 40)` type-checks and `call("applyVolume", "loud")` does not:

```ts
import { OctaveClient, obj } from 'octave-client'

const client = new OctaveClient({ url: 'ws://127.0.0.1:9876/' })   // in a browser the page's own host is the default
client.connect()
await client.whenOnline()
const vol = obj(client, 'volumeController')
await vol.call('applyVolume', 40)
vol.property('percent', (v) => console.log(v))                       // now and on every change
client.params(['RPM', 'SPEED'], (values) => console.log(values))    // live readings
await client.stream('phoneMirrorManager', 'video', { onRecord: (r) => decoder.feed(r) })   // media records
```

It reconnects with backoff and re-issues its subscriptions; `request(op, args)` covers any op. `cd sdk/typescript && npm install && npm test` regenerates nothing but checks that the committed `src/types.ts` matches `docs/api` (so it goes stale loudly, like `tst_apidocs`), type-checks the facade, and runs the client against a fake service; `npm run generate` rewrites the types after `octave --dump-api docs/api`. Its README has the details and `examples/now_playing.mjs`.

## Versioning

The Python and web clients know protocol versions 1 and 2, as does the TypeScript one (under 2, loopback stays open). The Python `Client` asks `hello` on connect and raises `OctaveError` on a version it doesn't know (`check_version=False` skips that); the web page shows the mismatch in its header and carries on. See [`docs/api/README.md`](../docs/api/README.md) for what a version bump means.
