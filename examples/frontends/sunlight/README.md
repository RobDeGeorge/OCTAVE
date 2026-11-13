# Sunlight

A third OCTAVE frontend, written to prove the open backend: **Go + Gio** instead of C++ / QML, and a brutalist screen built to be read in direct sunlight on the Pi instead of the bundled UI's look. Black on white (an inverted night mode is one tap away), huge numerals, thick rules, hard edges, no gradients or blur, and one accent colour (signal orange) for anything worth a glance. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol versions 1 to 3, with pairing) with its own client in plain `net` + `encoding/json`. The same code also builds for the browser (`GOOS=js GOARCH=wasm`), where it is a web frontend for a phone: see [In a browser](#in-a-browser). It is one of the example frontends in `examples/frontends/` (MIT licensed); the QML UI in `frontend/` is the one that ships. `examples/frontends/check_all.sh` runs `go vet`, `go test` and, given an X display, `verify_controls.py`.

It renders the user's dashboards straight from their `schema: 1` / `2` JSON (on a screen taller than wide, as `dashboardManager.orientedSpec` turns them: the spec's own portrait layout or the one OCTAVE derives), with its own Go drawing of every gauge type: the dashboard format is usable from another language, with no QML involved.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all, sunlight is the only screen
./build/octave --headless --sim         # or: a simulated car (any release build), for trying it at a desk

cd examples/frontends/sunlight
go run -tags novulkan .                  # --host H --port P --fullscreen --night --page drive|dash|media|dtc|trips --dash ID --token T --token-file F
```

Over the network (OCTAVE started with `--service-bind`), protocol 2 wants the screen paired: sunlight shows a keypad, you type the 6-digit code from OCTAVE's Settings > Service > Pair a device and press PAIR (or type it and Enter). The token is kept in `$XDG_CONFIG_HOME/octave-sunlight/tokens.json` (mode 0600, one per host:port; `--token-file` elsewhere, `--token` or `OCTAVE_TOKEN` to pass one), and a revoked one is forgotten so it asks again. The cover is fetched with the token as `Authorization: Bearer`, never in a URL. On the head unit itself nothing is asked while `serviceLoopbackOpen` is on (the default).

Start it before or after OCTAVE: until the service answers, the top bar shows NO SIGNAL in orange and it retries every second. Needs Go 1.26 or newer. Gio builds against the Wayland, X11, EGL and xkbcommon dev packages (Debian / Raspberry Pi OS: `libwayland-dev libx11-dev libx11-xcb-dev libxkbcommon-x11-dev libgles2-mesa-dev libegl1-mesa-dev libxcursor-dev libxfixes-dev`). The `novulkan` tag drops Gio's optional Vulkan renderer, so the Vulkan headers are not needed; it draws with OpenGL ES either way.

| Key | Does |
|---|---|
| 1 to 5 | DRIVE / DASH / MEDIA / DTC / TRIPS |
| Space | play / pause |
| Left / Right | previous / next track |
| Up / Down (or + / -) | volume by 5 |
| M | mute (sets `volumeController.muted`, a definite state) |
| [ / ] | previous / next dashboard |
| N | night mode |
| F11 | fullscreen |
| Ctrl+Q | quit |

Every key has a touch target on screen; the buttons are at least 64 dp tall with a 5 dp rule, and invert while pressed or active.

## Install it as the screen

`octave-frontend.json` makes this folder an installable frontend: the program `bin/sunlight --fullscreen`, asking for the **media** and **vehicle** permissions (play / pause / skip / seek / volume / mute; read and clear trouble codes).

```bash
cd examples/frontends/sunlight
go build -tags novulkan -o bin/sunlight .               # the manifest expects bin/sunlight
```

Then on the head unit: Settings > Device > Frontends > **Install from folder** (this folder), and **Try** (a timed trial that goes back by itself) or **Use** (from the next start). Over the service: `frontendManager.installFromFolder(path)`, then `tryFrontend(id, seconds)` or `setActive(id)`. Started that way it reads `OCTAVE_HOST`, `OCTAVE_PORT` and `OCTAVE_TOKEN` from its environment (the flags still win), sends the token with `hello`, and logs one `connected ...` line that OCTAVE relays into its own log as `[org.octave.example.sunlight] ...`. The token holds exactly the manifest's scopes, and while it is the screen nothing else on the head unit gets in without one (pair other tools with `octave --pair-code`). Quitting with Ctrl+Q (exit code 0) quits OCTAVE too.

Pressing mute five times within four seconds always ends it and brings back OCTAVE's built-in screen: the ESP32 knob, the gesture sensor, or anything else that calls `volumeController.toggleMute()` (this frontend's own M key and mute button set `volumeController.muted` directly, a definite state, so they don't count toward the five); `octave --qml` gives the built-in screen for one start. The rules are in `wiki/writing-a-frontend.html#install`.

## In a browser

The same program builds as a **web frontend**: Gio draws into a WebGL canvas, and the client speaks the protocol over the browser's own `WebSocket` (one JSON message per text frame) instead of TCP.

```bash
cd examples/frontends/sunlight
./build_web.sh                                              # dist-web/: index.html, boot.js, wasm_exec.js, sunlight.wasm, octave-frontend.json
./build/octave --headless --sim --service --web-root examples/frontends/sunlight/dist-web   # from the repo root; then open http://127.0.0.1:9876/
```

`dist-web/` (not committed) is a web frontend folder with its own manifest (`web/octave-frontend.json`, id `org.octave.example.sunlight.web`, `"web": {"root": "."}`; `build_web.sh` fails if its name, version, description, protocol or scopes drift from the native manifest). `sunlight.wasm` is about 12.5 MB (3.3 MB gzipped) and `wasm_exec.js` is copied from the Go toolchain that built it; the page shows a loading line with the megabytes so far.

- **Where the service is**: the host that served the page (`ws://` + `location.host`, `wss://` on an https page). There are no flags; the query does their job: `?page=media&night=1&dash=ID`.
- **Pairing**: on the head unit's own browser nothing is asked. From a phone (OCTAVE started with `--service-bind`) the same keypad comes up; the token is kept in the browser's `localStorage` (`sunlight.token`), asking only for the manifest's `media` and `vehicle` scopes. The cover is fetched from `/art` with the token as `Authorization`.
- **In OCTAVE's browser demo** the folder runs in a sandboxed frame against the core in the page: every file is loaded by relative URL with CORS and no credentials, the WebSocket there is the page's stand-in (text frames only, which is all Sunlight sends), storage is refused (so no token is kept, and none is needed), and there is no `/art`, so MEDIA has no cover.
- **Touch and shape**: everything is a touch target, and a window taller than wide (a phone held upright) gets its own arrangement: the tabs in a row of their own, speed over RPM, the transport and volume in two rows, the trip list over its summary, the pairing code over the keypad, and dashboards in their portrait layout. The native program lays out the same way when its window is upright.

What differs from the native program: no F11 / `--fullscreen` (use the browser's own, or Add to Home Screen) and no Ctrl+Q; the rest of the keyboard works with a keyboard attached.

`web/index.html` and `web/boot.js` are the page (the loader is a file because OCTAVE serves a frontend with a Content-Security-Policy that allows no inline script); `link/transport_js.go`, `link/tokens_js.go` and `main_js.go` are the browser's side of what `transport_tcp.go`, `tokens.go` and `main_native.go` do natively.

## Pages

- **DRIVE**: speed and RPM in giant monospaced numerals, a 24-segment shift bar (segments past the shift light's first flag are orange; the whole bar flashes while `shiftLightManager.lit`), and coolant, fuel and voltage tiles with a bar against the PID's range. Orange marks hot coolant, low fuel or odd voltage.
- **DASH**: the dashboards from `dashboardManager.dashboards`, one at a time (PREV / NEXT), each loaded with `loadDashboard` and laid out on its grid. It has its own drawing of every widget in the catalog: `CircularGauge` and `ArcGauge` (arc, ticks, needle, redline; a wide arc cell puts the readout beside the arc), `BarGauge` (horizontal or vertical, `warnAbove` mark), `LinearGauge` (zero-centred fill for trims), `DigitalReadout` (`padDigits`), `SparklineGauge` (`autoScale`, `fillBelow`), `WarningLight` (`triggerAbove` / `triggerBelow`, `label`, `pulse`), `GForceGauge`, `CompassGauge`, `NowPlayingWidget` and `MediaControlsWidget` (live transport buttons). A type it does not know gets a crossed placeholder with its name, as in OCTAVE. Colour props are ignored on purpose: sunlight has two inks and one accent.
- **MEDIA**: the cover from `GET /art`, title, artist, album and source; a position bar you tap to seek; PREV, PLAY / PAUSE, NEXT, SHUFFLE; VOL - / VOL + / MUTE and a volume bar you tap to set.

- **DTC**: the check-engine lamp (the one box the accent fills), the number of stored codes, the codes as giant tiles with their descriptions, READ, and CLEAR behind a second tap: the first tap turns it orange (TAP AGAIN: CLEAR) for four seconds, and only a tap inside that window clears. The ECU's answer to a clear stays on screen. Opening the page reads the car.
- **TRIPS**: the black box's trips, newest first, six to a page (the open one marked REC with its live time), and the selected trip's summary from `requestDetail`: distance, time, top speed, top RPM, the hottest coolant and oil, the voltage range, fuel used and the sample count, in the user's units (distance follows the speed unit).

The top bar holds the page tabs, the OBD state and car, the REC lamp with the trip timer, the app version, the clock and the NIGHT / DAY switch.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | every reading (one `subscribe` with no ids, 50 ms batches) |
| `parameterCatalog.displayParameters` | titles, units, ranges, decimals, and `displayScale` / `displayOffset`, so every page follows the user's °F / km/h choice |
| `obdManager` | `setParameterDemand("sunlight", ids)` for what the page shows (DRIVE's five, the open dashboard's PIDs, none on MEDIA); `connectionState`, make, model year |
| `dashboardManager` | `dashboards`, `loadDashboard(id)` |
| `nowPlaying` | track, position, source, `artRevision`, `togglePlay` / `next` / `previous` / `seek` / `toggleShuffle` |
| `GET /art` | the current cover, fetched again when `artRevision` changes (404: no cover) |
| `volumeController` | `percent`, `muted` (set), `effectivelyMuted`, `applyVolume` |
| `shiftLightManager` | `maxRpm`, `flags`, `lit` for the shift bar |
| `obdManager` (codes) | `read_dtc`, `clear_dtc`, `get_mil_status`, and the `dtcCodesChanged` / `milStatusChanged` / `dtcClearResult` signals |
| `tripRecorder` | `trips`, the current trip, `requestDetail` (answered by `detailReady`) |
| `clock` | `clock.time` |

`link/` is the client (about 600 lines): `link.go` the connection (request / reply matching by id, events on a channel) over a transport of whole messages (`transport_tcp.go`: lines over TCP; `transport_js.go`: WebSocket text frames in a browser), `state.go` what the UI reads (values, display conversion, history for sparklines, properties), `session.go` the reconnecting session, subscriptions and demand. `dash/` parses and normalises a spec the way the schema documents it (defaults, clamping, cells moved back onto the grid, overlaps shrunk or dropped) and lays the cells out. `ui.go` holds the drawing primitives, `gauges.go` the widgets, `main.go` the pages.

## Tests

```bash
cd examples/frontends/sunlight
go vet -tags novulkan ./... && go test -tags novulkan ./...
GOOS=js GOARCH=wasm go vet ./...                                         # the browser build's files
python3 verify_controls.py --display :83 --port 9876 --bin ./sunlight    # on an X display such as Xvfb :83
```

`go test` covers protocol parsing, state folding (display units, history, link loss), request / reply matching around interleaved events against a fake service, spec normalisation and layout, and parses every built-in preset without losing or overlapping a cell. `verify_controls.py` taps the touch targets through XTEST and reads each result back through the Python SDK: on MEDIA volume up / down, the volume bar, mute, play, next, seek and shuffle (put back afterwards); on DTC that opening the page reads the codes, that one tap on CLEAR only arms it and that a second clears them (so run it against the simulated car); on TRIPS that tapping a trip asks for its summary.

## Known gaps

- Dashboards are shown, not edited (the editor stays in the QML app; `saveDashboard` is there for whoever writes one).
- Colour props in a spec are ignored by design, and font props have no meaning here.
- No phone-mirror video (media streams are not on the service yet: `TODO/open-backend.md`, step 6).
- Not tried on the Pi or the Jeep box yet: built and screenshotted on Xvfb with Mesa's software GL.
- The browser build was driven in headless Chromium with touch emulation (served by OCTAVE, paired, and inside a sandboxed frame), not yet on a real phone.
