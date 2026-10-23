# OCTAVE service API

What a frontend, a script or another program can read and drive in OCTAVE, without Qt or QML. The QML UI that ships with OCTAVE sees the same objects. Protocol and transport: [wiki/utilities.html#service-protocol](../../wiki/utilities.html#service-protocol). A browsable page built from these files: [wiki/api-reference.html](../../wiki/api-reference.html).

## Files

| File | What | Source |
|---|---|---|
| `index.json` | `protocolVersion` and every object's name → C++ class | generated |
| `<object>.json` | one object: `properties` (type, readable / writable / constant, `notify` signal, `enum` keys), `methods` (`params`, `returns`, `kind`: slot or invokable), `signals` (`params`), `enums`. Exactly what the `describe` op returns | generated |
| `parameters.json` | every OBD PID and IMU reading: id, title, native unit, range, kind, decimals (`parameterCatalog.parameters`) | generated |
| `protocol.json` | the protocol envelope, JSON Schema style: the transports, every op with its arguments and reply, the error form and codes, the event shapes, the stream record, the HTTP routes, the pairing flow ("Protocol spec" below) | hand-written, checked against the server by `sdk/python/tests/test_protocol_spec.py` |
| `dashboard-spec.schema.json` | JSON Schema of a dashboard file (`schema: 1`) | hand-written from `DashboardManager::normalizeSpec` |
| `widget-catalog.json` | the widget types a dashboard cell can use: display name, glyph, default span, `supportedKinds`, label / range / colour flags, `editableProps` (`{key, label, kind, def, min?, max?, options?}`), `colorSwatches` (`dashboardManager.widgetCatalog`, from `frontend/dashboards/widget-catalog.json`) | generated |
| `README.md` | this file | hand-written |

The generated files come from the meta-objects of the desktop build (Linux, downloads on). Mobile builds stub some managers and `OCTAVE_ENABLE_DOWNLOADS=OFF` stubs `downloadManager`; a client finds out at run time with `describe`.

## Protocol spec

[`protocol.json`](protocol.json) is the half of the API that no meta-object produces: the ops (`hello`, `describe`, `get`, `set`, `call`, `rows`, `subscribe` / `unsubscribe`, `stream` / `unstream`, `pair` / `unpair`, `mediaTicket`) with the schema of each one's arguments and reply, the reply envelope and the error codes, every event (`property`, `signal`, `params`, `model`, `dropped`, `streamConfig`, `streamDropped`), the binary record and its framing on both transports, the HTTP routes with their query keys and status codes, the pairing flow and its limits, and the shape of an object description (what `describe` and the `<object>.json` files hold, annotations included). It carries `protocolVersion` like `index.json`, and each op and route says `since` which version. Code generators read it: `sdk/typescript` and `sdk/go` build their envelope from it, the object files give them the objects.

**For SDK authors.** `tools/api/fake_server.py` (standard library) speaks this spec without the app: `python tools/api/fake_server.py --port 0` serves `hello`, `describe` from these files, canned data on `volumeController`, `nowPlaying`, `paramStore` (ticking), `obdManager`, `parameterCatalog` and `downloadManager.downloadQueue`, a synthetic `phoneMirrorManager.video` stream, `mediaTicket`, and pairing with a fixed code (`--code`, `--untrusted` to start as a network client would) with permissions enforced from each member's `scope`. A `fake` object the app does not have injects faults: `rateLimitNext(n)`, `setForbidden(on)`, `revokeTokens()`, `setValue(object, property, value)`, `emitSignal(object, name, args)`. Every message it sends is validated against this spec, so a client that passes against the fake has met the spec's shapes; `tools/api/tests/test_fake_server.py` (Python) and `sdk/go/fake_test.go` (Go) show the exercise.

It is hand-written, so a test keeps it honest: `sdk/python/tests/test_protocol_spec.py` (ctest `sdk_python`) starts `octave --headless`, checks that the ops it lists are exactly the ops the server reports (`describe` with no object answers `ops`), and validates real replies, errors and events against the schemas strictly, so a field the server sends that the spec does not describe fails, as does an op in either place but not the other. `tools/api/jsonschema_lite.py` is the validator (the 2020-12 subset the spec uses). A change to `src/service/servicebridge.cpp`, `commandserver.cpp`, `streamhub.cpp` or the routes in `main.cpp` lands with its line in `protocol.json` and, when it is a break, a protocol bump and a changelog entry like any other.

## Streams

Video goes to clients as binary records on the same connection (`src/service/streamhub.h`). Additive: a new op, no version bump.

| Stream | Ask with | Payload |
|---|---|---|
| The phone mirror picture | `{"op":"stream","object":"phoneMirrorManager","name":"video"}` | H.264 as the phone sends it (no re-encode), one Annex-B access unit per record |
| Android Auto | `{"op":"stream","object":"androidAutoManager","name":"frames"}` | one JPEG per record, at most 15 a second, encoded only while someone watches |

The reply is `{"stream": S}` plus `format` (`annexb` / `jpeg`) and, for H.264 once known, `codec` (`avc1.PPCCLL`, from the SPS), `width`, `height`. A `{"event":"streamConfig","stream":S,"codec","width","height","format"}` event comes on every change of the parameter sets (the phone rotating, a new session), before the records that use them. `{"op":"unstream","stream":S}` stops one; a closed connection ends all of its streams.

**Record**: one binary WebSocket frame, or on the line protocol `0xFF`, a big-endian u32 length, then the record (a JSON line never starts with `0xFF`):

| Bytes | Field |
|---|---|
| 0 | version, 1 |
| 1 | stream id `S` |
| 2 | flags: 1 key, 2 config (SPS / PPS inline before the IDR), 4 JPEG, 8 discontinuity |
| 3 | reserved, 0 |
| 4-11 | presentation time, microseconds, big-endian u64 (the phone's clock; JPEG: wall clock) |
| 12- | payload |

Every keyframe record carries the current SPS and PPS first, so a decoder can start at any keyframe. In a browser: `new VideoDecoder({...}).configure({codec})` with no `description` (Annex-B mode), then `decode(new EncodedVideoChunk({type: flags & 1 ? "key" : "delta", timestamp, data: payload}))`; reconfigure on `streamConfig`.

**Joining and falling behind.** A new subscriber gets nothing until the next keyframe (its first record has the discontinuity flag); the app asks the phone for one at once, at most once a second across all clients. A client that stops reading has its queue capped (2 MB or 90 records): when it overflows every queued record is dropped, a `{"event":"streamDropped","stream":S,"count":N}` says so, and the stream resumes at the next keyframe.

**Touch.** `phoneMirrorManager.injectTouch(pointerId, action, relX, relY)`: `action` 0 down, 1 up, 2 move; `relX` / `relY` from 0 to 1 across the picture as decoded (the current `streamConfig` width and height), origin top left. Use one `pointerId` per finger.

**Developing without a phone.** `octave --headless --sim` (any build) and `sim.setVideo(true)` loop a test clip (`tests/data/stream-test.h264`, compiled in: 640x360 for 2 s, then 1280x720, 30 fps; made by `tools/stream/make_test_h264.sh`) into the phone stream, so the config change is exercised. See [Simulator](#simulator).

**Security.** `stream` needs a trusted connection like every op ("Pairing" below), and both streams show a phone's screen, so both need the `phone` permission ("Permissions"); a device paired for less gets `forbidden`.

## Regenerating

```bash
./build/octave --dump-api docs/api      # managers built in a throwaway config dir, no UI, no service
venv/bin/python wiki/build_api_reference.py
```

`tst_apidocs` (ctest, and so CI) dumps into a temp dir and fails if any generated file here differs, so a change to a manager's public surface lands with its regenerated docs in the same commit.

## Versioning

`protocolVersion` (in `index.json`, in `hello`, and `kProtocolVersion` in `src/service/protocol.h`) is **2**. It goes up on any breaking change: a removed or renamed op, object, property, method or signal, or a changed type or meaning. Additions (a new object, property, method, signal, op or event field) do not bump it, so a client should ignore what it does not know. Accept the versions you were written against and refuse, or degrade, on an unknown later one (`sdk/python` accepts 1 and 2).

Compatibility so far:

| Version | Since | Notes |
|---|---|---|
| 1 | 2026-10-22 | `hello`, `describe`, `get`, `set`, `call`, `subscribe` / `unsubscribe`, `rows`; list-model events. Transports: JSON lines over TCP, and WebSocket text frames on the same port (a transport, not a protocol change). |
| 2 | 2026-10-23 | Pairing ("Pairing" below): `pair`, `unpair`, `mediaTicket`; `hello` takes a `token` and reports `auth` and `loopbackOpen`. **What breaks for a v1 client:** nothing on this device while `serviceLoopbackOpen` is true (the default), except that a strict client sees `protocolVersion` 2; over the network (`--service-bind`) every op but `hello` and `pair`, and `/art` / `/export`, now answer `{"ok": false, "code": "unauthorised"}` (HTTP 401) until it pairs |

### Stability and deprecation

Every member is **stable** unless marked: covered by the version, so it changes only with a bump. A member (or a whole object) marked `"stability": "experimental"` in its file here and in `describe` may change or go in any release; build on it knowing that. A member marked `"deprecated": "<what to use instead, and when it goes>"` still works; it is kept for at least one protocol version after the one that deprecates it, and its removal is a breaking change like any other. Both are declared in the C++ class next to the members (`Q_CLASSINFO("octave.experimental", "...")`, `Q_CLASSINFO("octave.deprecated.<member>", "...")`, see `src/service/metadescribe.h`); `--dump-api` refuses an annotation that names a member the class doesn't have.

### Enforced

`tools/api/check_compat.py` compares this directory with the base of a change (CI's `lint` job: the PR's base branch, or the commit before a push) and fails when a stable member, a parameter id or a parameter's unit is removed or changed without `kProtocolVersion` going up, or when the new version's section in [CHANGELOG.md](CHANGELOG.md) does not name each break as `object.member`. `python tools/api/check_compat.py --draft` lists every change, breaking and additive, for the changelog. What it does not see: the meaning of a value, and the envelope (the ops and their arguments, the events, the stream record, the HTTP routes), which `protocol.json` describes and `test_protocol_spec.py` checks against the server; a change there still needs a reviewer and a changelog line. `check_compat` does check that `protocol.json` and `index.json` agree on the version.

Not covered by the version: the `sim` object (experimental, see [Simulator](#simulator)), and the dev-only `action` vocabulary (`navigate`, `click`, `screenshot`, `sim_*`, ...), which exists only with `--dev` / `--profile` in a developer build and may change at any time.

## Pairing

Who may use the service (`src/service/servicepairing.h`). A connection is **trusted** when its peer is on this device and the setting `serviceLoopbackOpen` is true (the default), or when it has presented a paired device's token. Every op and action except `hello` and `pair` needs trust, checked as each op runs; an untrusted connection gets `{"ok": false, "error": "not paired: ...", "code": "unauthorised"}`. The page and `--web-root` files are public (they carry the pairing form); `/art` and `/export/...` need trust: a media ticket (`?ticket=`) from a browser, or `Authorization: Bearer <token>`.

1. The user opens OCTAVE's Settings > Service > Pair a device: a 6-digit code shows for 2 minutes.
2. The client sends `{"op":"pair","code":"123456","name":"Kitchen tablet","scopes":["media"]}` and gets `{"token": "<64 hex>", "scopes": ["read", "media"]}`; that connection is trusted at once. Store the token. Without `scopes` the device gets `full` ("Permissions" below).
3. Later connections send `{"op":"hello","token":"..."}` first; `hello` answers `"auth": "trusted"`. An unknown or revoked token is not an error: `hello` answers `"auth": "untrusted", "tokenRejected": true` (and drops any token the connection had), so show your pairing screen.
4. For images and downloads, a trusted connection asks `{"op":"mediaTicket"}` → `{ticket, expiresIn: 600}` and uses `/art?ticket=...`; never put the token in a URL.
5. `{"op":"unpair"}` forgets the caller's own device.

Developing a networked frontend without the head unit's screen: run `octave --headless --sim --service-bind 0.0.0.0` (any build), connect your frontend through the machine's LAN address (untrusted), and get a code over loopback with `{"op":"call","object":"sim","method":"pairingCode","args":[]}` → `{code, secondsLeft}`.

Limits: the code is single use and void after 20 wrong tries in all; one `pair` attempt a second across all clients; an address (an IPv6 /64) with 10 wrong codes is refused until the app restarts; an untrusted `hello` gets only `protocolVersion`, `auth` and `loopbackOpen` (no app version, build or object list); at most 256 media tickets exist at once (the oldest goes); refusals are logged with the address (never the code). Only a SHA-256 of each token is stored, in the settings file key `serviceTokens`, which no setter, `apply_settings` or reset can write. Revoking a device in Settings closes its connections at once (their subscriptions and streams end) and voids its tickets. Making codes, listing or revoking devices and switching the service on are on the head unit only (`servicePairing`, not a service object).

### Permissions

A paired device holds **scopes** (`src/service/servicescopes.h`), asked for in `pair`: `{"op":"pair","code":"123456","name":"Dash","scopes":["media"]}`. Without `scopes` it gets `full`, as every device did before scopes existed. Every paired device can **read**: `get`, `subscribe`, `rows`, `describe` and `/art`; the two video streams show a phone's screen and need `phone`. Changing anything needs the member's scope, which `describe` and each file here list as `"scope"` on every method and writable property:

| Scope | What it lets a device change |
|---|---|
| `read` | nothing (getters and pure helpers are `read`, so any device may call them, as are the demand calls a reading client needs: `obdManager.setParameterDemand`, `berryIMU.setDemand`, `audioAnalyzer.setDemand`) |
| `media` | playback, volume, the queue, the visualiser |
| `vehicle` | OBD connection and trouble codes, adapters, the IMU, gestures, the shift light, the volume knob |
| `phone` | phone mirroring and Android Auto, including their video streams and touch / key input |
| `settings` | `settingsManager` |
| `files` | the library and playlists, downloads, dashboards, trips, logs, and `/export/...` |
| `system` | restart, shut down, update, opening folders on the head unit |
| `full` | everything, including the dev `action` form and app internals no frontend needs |

`pair` answers `{token, scopes}` with what was granted (`read` is always included); a trusted `hello` reports `scopes` too (the head unit's own screen, on loopback, has `full`). An unknown scope fails `pair` without using up the code. A member the device may not change answers `{"ok": false, "code": "forbidden", "error": "mediaManager.delete_song needs the files permission; this device has read, media"}`; a route answers 401. Settings > Device > Paired devices shows each device's access and can make it read only (or give it full access) at once, on its open connections too. Narrowing a member's scope later is a breaking change (`tools/api/check_compat.py` catches it); widening it, or making it `read`, is not.

`serviceLoopbackOpen` trusts every process on the device, including a local reverse proxy: behind a proxy, set it to false in `settingsConfigure.json`. While an installed exec frontend is the screen (`frontendManager`, [wiki/writing-a-frontend.html#install](../../wiki/writing-a-frontend.html#install)), loopback trust is off for that run: the frontend gets a session token (`OCTAVE_TOKEN` in its environment, kept in memory, revoked when it ends) holding only the scopes its manifest asks for, and every other local program needs a token too. While an installed web frontend is served at `GET /`, a browser on the device (a loopback request carrying `Origin` or `Sec-Fetch-Site`) holds that frontend's manifest scopes instead of loopback's full trust, unless it presents a token; programs keep full trust. `octave --pair-code` (same user, same `--config`) prints a code from the running instance for pairing them, over a local socket that only that user can open and that has no other op (`src/service/adminchannel.h`). That holds the frontend to its scopes on the socket; it is not a sandbox: the program runs as OCTAVE's own user, so it can still read the config folder (the frontends folder included) and its token is in its process environment, which its child processes inherit. Install only frontends you trust.

## Simulator

A car to build against without a car, in every build (release and app store included): `octave --headless --sim` serves every object with the real `obdManager` connected to a simulated ELM327 (`sim://elm327`: every PID supported, VIN, trouble codes, freeze frame), the `berryIMU` readings moving with it, and three short test tracks in the music folder. `--sim` without `--headless` gives the same car in the QML UI. `--dev` (developer builds) implies `--sim` on your usual config.

**Config.** `--sim` runs on a throwaway config folder (logged at start, deleted at exit) seeded with the test tracks and the ESP32 knob and gesture sensor off, so trying it never reads or writes a real install. `--config DIR` keeps settings, trips, dashboards and logs in `DIR` instead (any run); with `--sim` nothing is seeded there.

**The `sim` object** (`src/sim/simulator.h`, [sim.json](sim.json)) drives the car. It is listed in every build; `active` is false unless the run is simulated, and then every control returns `false` and does nothing. Controls return `true`:

| Method | What |
|---|---|
| `setThrottle(percent)`, `setBrake(percent)` | pedals, 0-100 (the physics moves the speed, RPM and gear) |
| `rev(intensity)` | a blip of the throttle, 0-100 |
| `setEngineOn(on)`, `toggleEngine()` | |
| `setGear(n)`, `upshift()`, `downshift()` | 1-6 |
| `setSpeed(mph)`, `setRpm(rpm)` | jump to a value |
| `scenario(name)` | `cold`, `hot`, `low_fuel`, anything else for normal |
| `setRealistic(on)` | Bluetooth-like latency and 2 % NO DATA (on at start) |
| `setDropouts(on)`, `dropoutNow()`, `setFailureRate(rate)`, `setLinkUp(up)` | link faults; `setLinkUp(false)` unplugs the adapter |
| `setImu(on)` | the IMU motion (on at start with `--sim` and `--headless`) |
| `setVideo(on)` | the test clip into `phoneMirrorManager.video` |
| `pairingCode()` | shows a pairing code as Settings would: `{code, secondsLeft}` |
| `status()` | the car's values and link settings, `imu`, `video` |

Experimental: not covered by the protocol version, so names and arguments may change in any release. Never call it from a frontend meant for a real car; check `sim.active` if you show simulator controls. The dev tooling's `sim_*` actions are aliases for these methods.

## Limits per connection

What one client may cost (`CommandServer::Limits` in `src/service/commandserver.h`), so a stuck or greedy client never slows the car, the screen or the other clients. Measured with `tools/stream/service_load.py`: [wiki/utilities.html#many-clients](../../wiki/utilities.html#many-clients).

| Limit | Default | What happens |
|---|---|---|
| Ops a connection sends | 400 at once, 200 a second sustained | Each op past it is answered at once, without running: `{"ok": false, "id": ..., "error": "too many requests: ...", "code": "rateLimited", "retryAfterMs": N}`. Send it again after `N` ms. Dev `action`s are not counted. A client refused more than 4000 times in one second is not waiting as told: its connection is closed. |
| Events a client has not read | 4 MB unsent | Events wait in a queue of 1000 instead (the oldest go; `{"event": "dropped", "count": N}` says how many), stream records in 2 MB or 90 (`streamDropped`, then the next keyframe). |
| Staying at that 4 MB | 15 s | The connection is closed, and its subscriptions, demand and streams go with it. Reconnect and subscribe again. |
| Connections | 64 in all, 8 from one network address | Further connections are closed at once. |
| New connections | 20 a second from one network address (100 at once: each of a page's files is one); 100 a second from this device (400 at once) | Closed as soon as they are accepted, before any request is read. All of this device counts as one address, so while a runaway local loop is refused, other new local connections are too; open ones are not affected. Reconnect with back-off (1 s, doubling) and this never trips. |
| Silence | 10 s before the first byte; 60 s for an untrusted connection | Closed. |
| One request line | 1 MB | `{"ok": false, "error": "request too long"}`, then closed. |

Error codes so far: `unauthorised` (pair first, "Pairing" above), `forbidden` ("Permissions" above), `rateLimited`. A client should act on `code` and show `error`.

## Values

`parameters.json` lists each value in its native unit (°C, MPH for speed, km for distances, kPa, ...). `paramStore` values and `subscribe` `params` events are native too. `parameterCatalog.displayParameters` and `parameterCatalog.toDisplay(id, value)` convert to the units the user picked (`settingsManager.gaugeTempUnit`, `gaugeSpeedUnit`).
