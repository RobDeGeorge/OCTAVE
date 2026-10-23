# OCTAVE service API

What a frontend, a script or another program can read and drive in OCTAVE, without Qt or QML. The QML UI that ships with OCTAVE sees the same objects. Protocol and transport: [wiki/utilities.html#service-protocol](../../wiki/utilities.html#service-protocol). A browsable page built from these files: [wiki/api-reference.html](../../wiki/api-reference.html).

## Files

| File | What | Source |
|---|---|---|
| `index.json` | `protocolVersion` and every object's name → C++ class | generated |
| `<object>.json` | one object: `properties` (type, readable / writable / constant, `notify` signal, `enum` keys), `methods` (`params`, `returns`, `kind`: slot or invokable), `signals` (`params`), `enums`. Exactly what the `describe` op returns | generated |
| `parameters.json` | every OBD PID and IMU reading: id, title, native unit, range, kind, decimals (`parameterCatalog.parameters`) | generated |
| `dashboard-spec.schema.json` | JSON Schema of a dashboard file (`schema: 1`) | hand-written from `DashboardManager::normalizeSpec` |
| `README.md` | this file | hand-written |

The generated files come from the meta-objects of the desktop build (Linux, downloads on). Mobile builds stub some managers and `OCTAVE_ENABLE_DOWNLOADS=OFF` stubs `downloadManager`; a client finds out at run time with `describe`.

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
