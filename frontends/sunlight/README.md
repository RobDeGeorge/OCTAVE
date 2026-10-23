# Sunlight (experimental)

A third OCTAVE frontend, written to prove the open backend: **Go + Gio** instead of C++ / QML, and a brutalist screen built to be read in direct sunlight on the Pi instead of the bundled UI's look. Black on white (an inverted night mode is one tap away), huge numerals, thick rules, hard edges, no gradients or blur, and one accent colour (signal orange) for anything worth a glance. It is a separate program that talks to OCTAVE over the service API (`docs/api/`, protocol versions 1 and 2; pairing for remote use is still to come) with its own client in plain `net` + `encoding/json`. The QML UI in `frontend/` stays the default; this one is a side experiment and is not shipped; CI runs `go vet`, `go test` and a build.

It renders the user's dashboards straight from their `schema: 1` JSON, with its own Go drawing of every gauge type: the dashboard format is usable from another language, with no QML involved.

## Run

```bash
./build/octave --service                 # the normal app, plus the service on 127.0.0.1:9876
./build/octave --headless                # or: no QML UI at all, sunlight is the only screen
./build/octave --dev --service           # or: the simulated car, for trying it at a desk

cd frontends/sunlight
go run -tags novulkan .                  # --host H --port P --fullscreen --night --page drive|dash|media|dtc|trips --dash ID
```

Start it before or after OCTAVE: until the service answers, the top bar shows NO SIGNAL in orange and it retries every second. Needs Go 1.24 or newer. Gio builds against the Wayland, X11, EGL and xkbcommon dev packages (Debian / Raspberry Pi OS: `libwayland-dev libx11-dev libx11-xcb-dev libxkbcommon-x11-dev libgles2-mesa-dev libegl1-mesa-dev libxcursor-dev libxfixes-dev`). The `novulkan` tag drops Gio's optional Vulkan renderer, so the Vulkan headers are not needed; it draws with OpenGL ES either way.

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

`link/` is the client (about 600 lines): `link.go` the connection (request / reply matching by id, events on a channel), `state.go` what the UI reads (values, display conversion, history for sparklines, properties), `session.go` the reconnecting session, subscriptions and demand. `dash/` parses and normalises a spec the way the schema documents it (defaults, clamping, cells moved back onto the grid, overlaps shrunk or dropped) and lays the cells out. `ui.go` holds the drawing primitives, `gauges.go` the widgets, `main.go` the pages.

## Tests

```bash
cd frontends/sunlight
go vet -tags novulkan ./... && go test -tags novulkan ./...
python3 verify_controls.py --display :83 --port 9876 --bin ./sunlight    # on an X display such as Xvfb :83
```

`go test` covers protocol parsing, state folding (display units, history, link loss), request / reply matching around interleaved events against a fake service, spec normalisation and layout, and parses every built-in preset without losing or overlapping a cell. `verify_controls.py` taps the touch targets through XTEST and reads each result back through the Python SDK: on MEDIA volume up / down, the volume bar, mute, play, next, seek and shuffle (put back afterwards); on DTC that opening the page reads the codes, that one tap on CLEAR only arms it and that a second clears them (so run it against the simulated car); on TRIPS that tapping a trip asks for its summary.

## Known gaps

- Dashboards are shown, not edited (the editor stays in the QML app; `saveDashboard` is there for whoever writes one).
- Colour props in a spec are ignored by design, and font props have no meaning here.
- No phone-mirror video (media streams are not on the service yet: `TODO/open-backend.md`, step 6).
- Not tried on the Pi or the Jeep box yet: built and screenshotted on Xvfb with Mesa's software GL.
