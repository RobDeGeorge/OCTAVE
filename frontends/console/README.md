# Console (experimental)

An operator console for OCTAVE in the terminal, written to prove the open backend: **Python + Textual** instead of C++ / QML, dense and keyboard-first instead of touch-first. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol version 1) through the repo's own Python SDK (`sdk/python/octave_client`, imported from the tree, not copied). The QML UI in `frontend/` stays the default; this one is a side experiment and is not shipped; CI runs its offline tests.

It covers the operator side of the app: every live PID, the OBD connection and trouble codes, the black box, every setting, downloads, and an explorer for the API itself, with a now-playing bar that is always on screen.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all
./build/octave --dev --service           # or: the simulated car, for trying it at a desk

python3 -m venv ~/venvs/octave-console
~/venvs/octave-console/bin/pip install -r frontends/console/requirements.txt
cd frontends/console
~/venvs/octave-console/bin/python -m octave_console    # --host H --port P --tab live|obd|trips|settings|downloads|api
```

Start it before or after OCTAVE: until the service answers, the status line shows NO LINK and it retries every second. It reconnects the same way if OCTAVE restarts. Needs Python 3.10 or newer and a terminal of about 120x32 or more.

| Key | Does |
|---|---|
| 1 to 6 | Live, OBD, Trips, Settings, Downloads, API |
| Ctrl+P | command palette (every pane, transport, OBD and black-box action by name) |
| Space | play / pause |
| < / > | previous / next track |
| , / . | seek back / forward 10 s |
| + / - | volume by 5 |
| M | mute (sets `volumeController.muted`, so it is a definite state) |
| Z | shuffle |
| Q | quit |

Pane keys are shown in the footer while the pane has focus:

| Pane | Keys |
|---|---|
| Live | S cycle sort column (or click a header), R reverse, P pin / unpin a sparkline (up to four) |
| OBD | C connect, Shift+R reconnect, T read trouble codes, F freeze frame, Shift+X clear codes (asks first), Enter on an adapter connects to it |
| Trips | Enter details, E export, D delete (asks first), Shift+E end the current trip, G refresh |
| Settings | / filter, Enter edits (a bool flips at once; anything else opens an editor showing the options or range) |
| Downloads | / search, Enter on a result queues it, N more results, Shift+P pause, U resume, X cancel |
| API | / command line; selecting a property in the tree reads it, a method fills in a call |

## Panes

- **Live**: every PID the vehicle reports (plus any other reading that arrives, such as the IMU), in the user's display units, sortable by any column. Only the rows on screen and the pinned ones are registered as OBD demand, so scrolling changes what the car is polled for; leaving the pane sends an empty demand.
- **OBD**: connection state, detail, progress, vehicle, VIN, supported PID count and MIL; connect / reconnect / disconnect / scan the vehicle; the serial and simulated ports from `obdManager` and the Bluetooth devices from `obdAdapterFinder` in one table; trouble codes with read and a confirmed clear; the freeze frame; the connection log, live.
- **Trips**: the black box's trips, newest first (the open trip shows its live duration and sample count), details on demand, export, delete, end the current trip; recording state and storage in the side panel.
- **Settings**: every `settingsManager` property, read from `describe` at run time, so new settings appear without a console change. Labels, options and ranges come from `get_settings_registry`. A write goes through the property's save slot (the registry's `saveSlot`, else `save_<snake_name>`, else `save_setting(key, value)`), and the table shows the value read back from the property event.
- **Downloads**: search, results and the queue mirrored from the two list models (`rows` once, then `model` events: reset, insert, update, remove), with pause, resume, cancel and clearing. Says so when the build has downloads compiled out.
- **API**: a tree of every object with its properties, methods (with parameter types) and signals from `describe`, and a command line for `hello`, `describe`, `get`, `set`, `call` and `rows` (`call volumeController applyVolume 40`, `set volumeController muted true`; arguments are JSON where they parse, strings otherwise).

The status line shows the app version, build and protocol, the address, the OBD state and car, the REC lamp with the trip timer, MIL, the last action and the clock. The media bar shows play state, title and artist, a position bar, shuffle, the source and the volume.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | every reading (one `subscribe` with no ids, 250 ms batches) |
| `parameterCatalog.displayParameters` | titles, units, ranges, decimals, and `displayScale` / `displayOffset` for the user's units |
| `obdManager` | connection properties and log, `connect_obd` / `reconnect` / `close` / `scan_vehicle` / `refresh_ports` / `connect_to_adapter`, `read_dtc` / `clear_dtc` / `read_freeze_frame` and their signals, `setParameterDemand("console", ids)` |
| `obdAdapterFinder` | Bluetooth state, `devices`, `startScan` / `stopScan` / `connectAdapter` |
| `tripRecorder` | `trips`, recording state, `requestDetail` (answered by `detailReady`), `exportTrip` (`exportFinished`), `deleteTrip`, `endTripNow`, `refresh` |
| `settingsManager` | every property (subscribed by name from `describe`), `get_settings_registry`, the `save_*` slots |
| `downloadManager` | `searchResults` / `downloadQueue` models, `search` / `search_more` / `download` / `pause_download` / `resume_download` / `cancel_download` / `clear_*`, status signals |
| `nowPlaying` | track, position, source, `togglePlay` / `next` / `previous` / `seek` / `toggleShuffle` |
| `volumeController` | `percent`, `muted` (set), `effectivelyMuted`, `applyVolume` |
| `clock` | `timeChanged` for the status line |

`octave_console/link.py` is the connection: one SDK `Client` on a background thread that subscribes, posts every event to the UI as a Textual message and reconnects. Requests run on Textual thread workers, so a slow call never freezes the screen. `octave_console/panes.py` holds the six panes, `app.py` the status line, media bar, palette and key bindings.

## Tests

```bash
OCTAVE_PORT=9876 ~/venvs/octave-console/bin/python -m pytest frontends/console    # OCTAVE_SHOTS=DIR keeps SVG screenshots
```

Textual pilot tests against a running service: they press the keys and read the results back through the SDK (volume, mute, transport, a setting toggled and edited, trouble codes read and cleared through the confirmation, trips, the API explorer, the palette). They skip when nothing answers on the port. Point them at a throwaway instance with the simulated car (`octave --dev --profile`): they clear its trouble codes and change settings, and restore the settings they change. The live-data test nudges the simulator's throttle with a dev-only `sim_throttle` action; the console itself uses no dev actions.

## Known gaps

- No album art or phone-mirror video (a terminal can't show them, and media streams are not on the service yet: `TODO/open-backend.md`, step 6).
- The freeze frame shows what `get_freeze_frame` returns, which today is the code that stored it, not the frame's PID values.
- If the process is killed (not closed), its OBD demand stays registered until OCTAVE restarts.
- Downloads were only tried with no search results (no network in the test setup).
