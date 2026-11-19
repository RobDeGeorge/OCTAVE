# Custom PIDs

Standard OBD-II carries about a hundred readings, mostly from the engine. A car knows far more (transmission temperature, oil level, each wheel's speed), but every manufacturer keeps those under its own identifiers. A custom PID tells OCTAVE one of them: what to ask for, which module to ask, and how to turn the answer into a number. From then on it is a parameter like any other: pick it for a gauge or a dashboard widget (group **Custom**), and it is recorded with every drive.

OCTAVE only reads. The request is always a read-by-identifier (mode 22 or 21) that OCTAVE builds itself from the mode and id; nothing in the file is sent to the car as written.

## The file

`custom-pids.json` in the OCTAVE config folder (Linux `~/.config/OCTAVE/`, Windows `%APPDATA%/OCTAVE/`, macOS `~/Library/Application Support/OCTAVE/`). Settings > OBD > Advanced > Custom PIDs shows where it is, what was loaded and what was skipped, and writes a starter file if there is none. The file is read when OCTAVE starts: restart after editing.

```json
{
  "schema": 1,
  "pids": [
    {
      "id": "TRANS_TEMP",
      "title": "Transmission Temp",
      "shortTitle": "TFT",
      "unit": "°C",
      "min": -40,
      "max": 150,
      "kind": "temperature",
      "decimals": 0,
      "mode": "22",
      "pid": "1940",
      "header": "7E1",
      "formula": "A-40"
    }
  ]
}
```

| Field | Needed | Meaning |
|---|---|---|
| `id` | yes | The parameter id gauges and dashboards store: 2 to 40 capitals, digits and `_`, not one OCTAVE already has. |
| `pid` | yes | The identifier, in hex: up to four digits for mode 22, two for mode 21. |
| `formula` | yes | Turns the reply's data bytes into the value (below). |
| `mode` | no | `"22"` (default) or `"21"`. Nothing else is accepted. |
| `header` | no | The module to ask, as the adapter's `ATSH` takes it: three hex digits (11-bit CAN, `"7E1"`) or six (`"DA10F1"`). Without one the request goes to the standard address, which many engine modules answer. |
| `title`, `shortTitle`, `unit` | no | What the gauges show. `title` defaults to the id. |
| `min`, `max` | no | The gauge range (0 to 100). |
| `kind` | no | `numeric` (default), `percentage`, `temperature`, `voltage`, `pressure` or `bidirectional`: which widgets offer it. Give a `temperature` in °C; OCTAVE converts to °F when that is the display unit. |
| `decimals` | no | 0 to 4 (default 1). |

Up to 64 entries. An entry with a mistake is skipped and named in Settings > OBD > Advanced > Custom PIDs and in the log; the others still load.

## Formulas

The same style PID lists for other OBD apps use, so a published formula usually works as it is. `A` is the first data byte of the reply (after the echoed identifier), `B` the second, up to `Z`, then `AA`, `AB`.

| Written | Means |
|---|---|
| `A-40` | one byte, offset |
| `(A*256+B)/10` | two bytes, scaled |
| `SIGNED(A)` | the byte as a two's complement number (-128 to 127); `SIGNED(A)*256+B` for a signed two-byte value |
| `{A:0}` | bit 0 (the lowest) of the byte: 0 or 1 |
| `A&15`, `A\|B`, `A<<8`, `A>>4` | bit operations |
| `0x28`, `0.5` | hex and decimal numbers |

`* /` bind before `+ -`, those before `<< >>`, then `&`, then `|`; parentheses as usual.

## Finding the numbers

Search for your make and model with "mode 22 PIDs" or "extended PIDs"; forum threads and the PID lists shared for Torque give the same four things: mode + id (often written together, `221940`), header, formula, unit. The starter file's entry is the automatic transmission fluid temperature commonly published for GM vehicles; it is also one the simulated car answers, so `octave --sim` shows it working.

## Limits

- A reading the car doesn't answer three times in a row is left alone and tried again every 30 seconds, so a wrong entry costs the live gauges almost nothing. The OBD connection log says when that happens.
- A `header` works on CAN (every car since 2008) and on J1850 and ISO 9141-2. On KWP2000 cars an entry with a header is not polled: OCTAVE has to put the adapter's own address back after each request and does not know it there.
- Each reading with a header costs three round trips to the adapter (set the address, ask, put the address back) against one for a standard PID.
- Not on Android yet: the phone build's Bluetooth LE poller does not send them. The browser build does, but the page's files do not survive a reload.
- Reading only. Adaptation resets, actuator tests and writes are not part of this.

## How it works

`src/managers/custompids.{h,cpp}`: `PidFormula` (the formula compiler), `CustomPids::parse()` (the file) and the `customPids` object (`file`, `fileExists`, `pids`, `errors`, `createExample()`). `AppCore` constructs it before the parameter catalog is read: the rows join `ParameterCatalog` (`custom: true`, group `Custom`) and the requests go to `OBDManager::setCustomPids()`. The poll list carries them as `{kCustomPidMode, index}` while their id is demanded; `OBDConnectionWorker::pollCustom()` sends `ATSH<header>`, the request (`ELM327Protocol::formatDataRequest`), and `ATSH<default>` again (`ELM327Protocol::defaultHeader`), and the value arrives as `obdParameterChanged(id, value)` / `parameterSampled`. Tests: `tests/cpp/tst_custompids.cpp`, and `tst_obdsim` against the simulated car's transmission module.
