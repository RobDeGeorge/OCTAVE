# Console (experimental)

An operator console for OCTAVE in the terminal, written to prove the open backend: **Python + Textual** instead of C++ / QML, dense and keyboard-first instead of touch-first. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol versions 1 and 2) through the repo's own Python SDK (`sdk/python/octave_client`, imported from the tree, not copied). The QML UI in `frontend/` stays the default; this one is a side experiment and is not shipped; CI runs its offline tests.

It reaches every control and readout of the QML app that is not video (`frontends/PARITY.md` has the feature-by-feature check): every live PID, the OBD connection and trouble codes, the black box with charts, the music library and playlists, downloads, dashboards (managed and edited as JSON), the shift light and its flags, the IMU, gesture sensor and ESP32 knob, phone mirroring and Android Auto sessions, network, updates and diagnostics, every setting, and an explorer for the API itself, with a now-playing bar that is always on screen.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all
./build/octave --dev --service           # or: the simulated car, for trying it at a desk

python3 -m venv ~/venvs/octave-console
~/venvs/octave-console/bin/pip install -r frontends/console/requirements.txt
cd frontends/console
~/venvs/octave-console/bin/python -m octave_console    # --host H --port P --tab live|obd|trips|library|downloads|dash|car|phone|system|settings|api
```

Over the network (OCTAVE started with `--service-bind`), protocol 2 wants the console paired: it shows a pair screen, you type the 6-digit code from OCTAVE's Settings > Service > Pair a device, and the token is kept in `$XDG_CONFIG_HOME/octave-console/tokens.json` (mode 0600, one per host:port; `--token-file` elsewhere, `--token` or `OCTAVE_TOKEN` to pass one). A revoked token is forgotten and it asks again. On the head unit itself nothing is asked while `serviceLoopbackOpen` is on (the default). Covers and downloads send the token as `Authorization: Bearer`, never in a URL.

Start it before or after OCTAVE: until the service answers, the status line shows NO LINK and it retries every second. It reconnects the same way if OCTAVE restarts. Needs Python 3.10 or newer and a terminal of about 120x32 or more.

| Key | Does |
|---|---|
| 1 to 9, 0, ` | Live, OBD, Trips, Library, Downloads, Dashboards, Car, Phone, System, Settings, API |
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
| Live | S cycle sort column (or click a header), R reverse, P pin / unpin a sparkline (up to four), H put the PID on the home screen's OBD cards or take it off, Shift+K / Shift+J move it earlier / later there |
| OBD | C connect, Shift+R reconnect, T read trouble codes, F freeze frame, Shift+X clear codes (asks first), Enter on an adapter connects to it, X forgets a saved adapter; buttons for force connect, reset, a direct MAC connect, pending codes, status, diagnostic mode, Bluetooth power on and cancel |
| Trips | Enter details, C chart one value of the trip, E export (on the head unit), W download the last export to this computer, D delete (asks first), Shift+C clear the whole history (asks first), Shift+E end the current trip, G refresh |
| Library | Enter plays a song / selects a playlist / skips to a phone queue item, S cycle sort, N new playlist, Shift+D delete playlist, V move the song to a playlist, X delete the song, Shift+R rescan, Shift+S strip file names (deleting and stripping ask first) |
| Downloads | / search, Enter on a result queues it, Enter on a finished download plays it, N more results, Shift+P pause, U resume, X cancel, T target playlist |
| Dashboards | E edit as JSON, D duplicate, X delete (asks first), I import pasted JSON, O export (shows the JSON to copy), W download the last export file, Shift+R restore the built-ins |
| Car | on the flags table: Enter edits a flag, A adds one, X removes one, Shift+E all flags as JSON; T tares the IMU; Enter on a gesture picks its action; buttons for the knob |
| Phone | buttons only for the session (it talks to a real phone): start, stop, resume, wake, check, keys, pairing; W sets the wireless address |
| System | L refresh the log, X export logs, "Download export…" saves the last log bundle here; update, restart and shut down are buttons that ask first |
| Settings | / filter, Enter edits (a bool flips at once; the music folder opens a folder browser; anything else opens an editor showing the options or range), Shift+T theme picker with swatches, P Now Playing preset, F music folder, O export every setting as JSON, I import (apply) pasted settings |
| API | / command line; selecting a property in the tree reads it, a method fills in a call |

## Panes

- **Live**: every PID the vehicle reports (plus any other reading that arrives, such as the IMU), in the user's display units, sortable by any column. Only the rows on screen and the pinned ones are registered as OBD demand, so scrolling changes what the car is polled for; leaving the pane sends an empty demand.
- **OBD**: connection state, detail, progress, vehicle, VIN, supported PID count and MIL; connect / reconnect / disconnect / scan the vehicle; the serial and simulated ports from `obdManager` and the Bluetooth devices from `obdAdapterFinder` in one table; trouble codes with read and a confirmed clear; the freeze frame; the connection log, live.
- **Trips**: the black box's trips, newest first (the open trip shows its live duration and sample count), details on demand, export, delete, end the current trip; recording state and storage in the side panel.
- **Settings**: every `settingsManager` property, read from `describe` at run time, so new settings appear without a console change. Labels, options and ranges come from `get_settings_registry`. A write goes through the property's save slot (the registry's `saveSlot`, else `save_<snake_name>`, else `save_setting(key, value)`), and the table shows the value read back from the property event.
- **Downloads**: search, results and the queue mirrored from the two list models (`rows` once, then `model` events: reset, insert, update, remove), with pause, resume, cancel and clearing. Says so when the build has downloads compiled out.
- **Library**: the songs of the current playlist with artist, album and length (sorted by the app's `sort_media_files`), the playlists, the phone's queue while mirroring, the highlighted song's cover (or the phone queue item's) drawn in half blocks from `GET /art?song=` / `?queue=` (32 x 16 pixels, Pillow decodes it), and a one-line spectrum from `audioAnalyzer.levels` (switched on with `setDemand` while the pane is shown). Metadata takes four calls per song, so a big library takes a moment to list.
- **Dashboards**: every dashboard with a preview of its grid and cells. Editing opens the spec as JSON; on save it goes through `normalizedSpec` (the console says what it moved or dropped) and `saveDashboard`. A built-in is saved as your own copy under `uniqueIdForLabel`. Duplicate, delete, restore built-ins, import pasted JSON, and export.
- **Car**: the shift light (live state, the look settings, and the flags as a table you edit one by one or as JSON, checked before `setFlags`), the IMU (every IMU reading while the pane is shown, tare and reset tare), the gesture sensor (status, last gesture, the mapping, reset), and the ESP32 knob (status, ports, connect, disconnect, retry). Enables live in Settings.
- **Phone**: mirroring state, devices, notices and errors, start / stop / resume / wake / check, the phone's home / back / apps keys, wireless address, forget, QR pairing (the QR code drawn in text), install help, the phone's media with its own transport, and the Android Auto desktop head unit (installed, running, launch, close, help).
- Exports (a trip's CSV, a dashboard file, the log bundle) are written on the head unit; W / "Download export…" fetches the last one over `GET /export/trip|dashboard|logs` and asks where to save it on this computer.
- **Streams** (Phone pane): watch the phone-mirror video and the Android Auto frames over the service's binary stream records (`stream` / `unstream`), with a live line per stream (frames a second, bit rate, size, codec, records, keyframes, drops); Android Auto's JPEG frames also show as a 48 x 28 half-block preview, four times a second. H.264 is counted, not decoded. Streams run only while the pane is shown.
- **System**: network, update status with check / apply, restart and shut down (all confirmed), version, backend, device info, the log directory and the last 300 log lines, export logs, the commit, and the LICENSE and NOTICE texts.
- Settings also has the Now Playing presets (`nowPlayingPresets`, `applyNowPlayingPreset`), a browser for the music folder over the head unit's own folders (`mediaManager.browse_music_folders`), and export / import of the settings as one JSON object: import goes through `apply_settings`, which applies each key through its own save slot with strict types and returns what it refused, and the console shows that list.
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
| `clock` | `time` for the status line |
| `mediaManager`, `audioAnalyzer` | the library, playlists, play / move / delete, rescan; `levels` and `setDemand` for the spectrum |
| `dashboardManager` | `dashboards`, `loadDashboard`, `normalizedSpec`, `saveDashboard`, `uniqueIdForLabel`, `duplicateDashboard`, `deleteDashboard`, `restoreBuiltInDashboards`, `importDashboardFromText`, `exportDashboard` |
| `shiftLightManager`, `berryIMU`, `gestureSensor`, `esp32VolumeManager` | the Car pane: live state, `setFlags`, `setDemand`, `calibrateTare` / `resetTare`, `setGestureAction` / `resetMappingToDefaults`, the knob's ports and connection |
| `phoneMirrorManager`, `androidAutoManager` | the Phone pane: session state and controls, pairing, phone keys and media, the queue; DHU launch / close |
| `networkManager`, `diagnosticsManager` | the System pane |
| `themeCatalog` | the theme picker |

`octave_console/link.py` is the connection: one SDK `Client` on a background thread that subscribes, posts every event to the UI as a Textual message and reconnects. Requests run on Textual thread workers, so a slow call never freezes the screen. `octave_console/panes.py` holds Live, OBD, Trips, Settings, Downloads and API; `panes_media.py` the Library, `panes_dash.py` the Dashboards, `panes_car.py` the Car pane, `panes_system.py` Phone and System; `app.py` the status line, media bar, palette and key bindings.

## Tests

```bash
OCTAVE_PORT=9876 ~/venvs/octave-console/bin/python -m pytest frontends/console    # OCTAVE_SHOTS=DIR keeps SVG screenshots
```

Textual pilot tests against a running service: they press the keys and read the results back through the SDK (volume, mute, transport, a setting toggled and edited, trouble codes read and cleared through the confirmation, trips and a trip chart, the API explorer, the palette, a playlist created / filled / deleted and a song played, a dashboard duplicated / edited as JSON / imported / deleted, shift flags added / edited / refused / removed, a gesture remapped, diagnostic mode, home OBD cards, the download playlist, the theme). Nothing in them connects an adapter, starts mirroring, opens a serial port, checks for updates or toggles a hardware enable. They skip when nothing answers on the port. Point them at a throwaway instance with the simulated car (`octave --dev --profile`): they clear its trouble codes and change settings, and restore the settings they change. The live-data test nudges the simulator's throttle with a dev-only `sim_throttle` action; the console itself uses no dev actions.

## Known gaps

- No phone-mirror picture: the H.264 stream is measured, not decoded (a terminal has no use for a decoder); Android Auto gets a coarse half-block preview.
- The freeze frame shows what `get_freeze_frame` returns, which today is the code that stored it, not the frame's PID values.
- Covers are 32 x 16 half-block pixels: recognisable, not pretty. The media bar has no room for one.
- Downloads were only tried with no search results (no network in the test setup).
