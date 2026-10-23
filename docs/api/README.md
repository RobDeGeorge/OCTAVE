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

`protocolVersion` (in `index.json`, in `hello`, and `kProtocolVersion` in `src/service/protocol.h`) is **1**. It goes up on any breaking change: a removed or renamed op, object, property, method or signal, or a changed type or meaning. Additions (a new object, property, method, signal, op or event field) do not bump it, so a client should ignore what it does not know. Pin to the version you were written against and refuse, or degrade, on a different one.

Compatibility so far:

| Version | Since | Notes |
|---|---|---|
| 1 | 2026-10-22 | `hello`, `describe`, `get`, `set`, `call`, `subscribe` / `unsubscribe`, `rows`; list-model events. Transports: JSON lines over TCP, and WebSocket text frames on the same port (a transport, not a protocol change) |

Not covered by the version: the dev-only `action` vocabulary (`navigate`, `click`, `screenshot`, `sim_*`, ...), which exists only with `--profile` in a developer build and may change at any time.

## Values

`parameters.json` lists each value in its native unit (°C, MPH for speed, km for distances, kPa, ...). `paramStore` values and `subscribe` `params` events are native too. `parameterCatalog.displayParameters` and `parameterCatalog.toDisplay(id, value)` convert to the units the user picked (`settingsManager.gaugeTempUnit`, `gaugeSpeedUnit`).
