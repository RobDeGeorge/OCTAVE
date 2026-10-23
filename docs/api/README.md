# OCTAVE service API

What a frontend, a script or another program can read and drive in OCTAVE, without Qt or QML. The QML UI that ships with OCTAVE sees the same objects. Protocol and transport: [wiki/utilities.html#service-protocol](../../wiki/utilities.html#service-protocol). A browsable page built from these files: [wiki/api-reference.html](../../wiki/api-reference.html).

## Files

| File | What | Source |
|---|---|---|
| `index.json` | `protocolVersion` and every object's name → C++ class | generated |
| `<object>.json` | one object: `properties` (type, readable / writable / constant, `notify` signal, `enum` keys), `methods` (`params`, `returns`, `kind`: slot or invokable), `signals` (`params`), `enums`. Exactly what the `describe` op returns | generated |
| `parameters.json` | every OBD PID and IMU reading: id, title, native unit, range, kind, decimals (`parameterCatalog.parameters`) | generated |
| `dashboard-spec.schema.json` | JSON Schema of a dashboard file (`schema: 1`) | hand-written from `DashboardManager::normalizeSpec` |
| `widget-catalog.json` | the widget types a dashboard cell can use: display name, glyph, default span, `supportedKinds`, label / range / colour flags, `editableProps` (`{key, label, kind, def, min?, max?, options?}`), `colorSwatches` (`dashboardManager.widgetCatalog`, from `frontend/dashboards/widget-catalog.json`) | generated |
| `README.md` | this file | hand-written |

The generated files come from the meta-objects of the desktop build (Linux, downloads on). Mobile builds stub some managers and `OCTAVE_ENABLE_DOWNLOADS=OFF` stubs `downloadManager`; a client finds out at run time with `describe`.

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

**Developing without a phone.** In a developer build, `octave --headless --dev --service` and the dev-only action `{"action":"sim_video","args":{"on":true}}` loop `tests/data/stream-test.h264` (640x360 for 2 s, then 1280x720, 30 fps; made by `tools/stream/make_test_h264.sh`) into the phone stream, so the config change is exercised. Never send dev actions from a shipped frontend.

**Security.** A read under the same rules as every op (loopback by default, the browser Origin rule). The phone screen is sensitive: when pairing lands, `stream` will need an authorised client like every other op.

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
| 1 | 2026-10-22 | `hello`, `describe`, `get`, `set`, `call`, `subscribe` / `unsubscribe`, `rows`; list-model events. Transports: JSON lines over TCP, and WebSocket text frames on the same port (a transport, not a protocol change). Later additions without a bump: `stream` / `unstream` and binary records, `GET /art`, `/export/...` |
| 2 | 2026-10-23 | Pairing ("Pairing" below): `pair`, `unpair`, `mediaTicket`; `hello` takes a `token` and reports `auth` and `loopbackOpen`. **What breaks for a v1 client:** nothing on this device while `serviceLoopbackOpen` is true (the default), except that a strict client sees `protocolVersion` 2; over the network (`--service-bind`) every op but `hello` and `pair`, and `/art` / `/export`, now answer `{"ok": false, "code": "unauthorised"}` (HTTP 401) until it pairs |

Not covered by the version: the dev-only `action` vocabulary (`navigate`, `click`, `screenshot`, `sim_*`, ...), which exists only with `--profile` in a developer build and may change at any time.

## Pairing

Who may use the service (`src/service/servicepairing.h`). A connection is **trusted** when its peer is on this device and the setting `serviceLoopbackOpen` is true (the default), or when it has presented a paired device's token. Every op and action except `hello` and `pair` needs trust, checked as each op runs; an untrusted connection gets `{"ok": false, "error": "not paired: ...", "code": "unauthorised"}`. The page and `--web-root` files are public (they carry the pairing form); `/art` and `/export/...` need trust: a media ticket (`?ticket=`) from a browser, or `Authorization: Bearer <token>`.

1. The user opens OCTAVE's Settings > Service > Pair a device: a 6-digit code shows for 2 minutes.
2. The client sends `{"op":"pair","code":"123456","name":"Kitchen tablet"}` and gets `{"token": "<64 hex>"}`; that connection is trusted at once. Store the token.
3. Later connections send `{"op":"hello","token":"..."}` first; `hello` answers `"auth": "trusted"`. An unknown or revoked token is not an error: `hello` answers `"auth": "untrusted", "tokenRejected": true` (and drops any token the connection had), so show your pairing screen.
4. For images and downloads, a trusted connection asks `{"op":"mediaTicket"}` → `{ticket, expiresIn: 600}` and uses `/art?ticket=...`; never put the token in a URL.
5. `{"op":"unpair"}` forgets the caller's own device.

Developing a networked frontend without the head unit's screen: run a developer build with `--dev --service-bind 0.0.0.0` (or `--headless --dev --service-bind 0.0.0.0`), connect your frontend through the machine's LAN address (untrusted), and get a code over loopback with the dev-only action `{"action":"sim_pairing_code"}` → `{code, secondsLeft}`.

Limits: the code is single use and void after 5 wrong tries; one `pair` attempt a second across all clients; an address with 10 wrong codes is refused until the app restarts; refusals are logged with the address (never the code). Only a SHA-256 of each token is stored, in the settings file key `serviceTokens`, which no setter, `apply_settings` or reset can write. Revoking a device in Settings closes its connections at once (their subscriptions and streams end) and voids its tickets. Making codes, listing or revoking devices and switching the service on are on the head unit only (`servicePairing`, not a service object).

`serviceLoopbackOpen` trusts every process on the device, including a local reverse proxy: behind a proxy, set it to false in `settingsConfigure.json`.

## Values

`parameters.json` lists each value in its native unit (°C, MPH for speed, km for distances, kPa, ...). `paramStore` values and `subscribe` `params` events are native too. `parameterCatalog.displayParameters` and `parameterCatalog.toDisplay(id, value)` convert to the units the user picked (`settingsManager.gaugeTempUnit`, `gaugeSpeedUnit`).
