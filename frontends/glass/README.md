# Glass (experimental)

A third OCTAVE frontend, written to prove the open backend from the browser: **TypeScript + Svelte 5 + Vite**, no UI kit. It is a glass tablet UI: frosted panels over a slowly drifting backdrop in the colours of the album that is playing, large type and touch targets, light and dark. It is laid out for the 1280x800 head unit in landscape and works in a phone browser in portrait. It talks to OCTAVE over the service API (`docs/api/`, protocol version 1) and reaches everything the bundled QML UI can do. The QML UI stays the default; this one is a side project and is not built by CI or shipped.

## Run

Production: build once, then let OCTAVE serve the page itself. A browser may only open a WebSocket to the service from a page the service served (the Origin rule), so this is the normal way to use it.

```bash
cd frontends/glass
npm install
npm run build                                   # -> dist/

./build/octave --service --web-root frontends/glass/dist      # the app, plus the page at http://127.0.0.1:9876/
./build/octave --headless --web-root frontends/glass/dist     # or: no QML, the browser is the screen
```

Open `http://127.0.0.1:9876/` on the head unit, or add `--service-bind 0.0.0.0` and open `http://<head-unit-ip>:9876/` from a phone on the car's network (read Versioning and Security in `wiki/writing-a-frontend.html` first: the service has no authentication).

Development: the Vite dev server serves the page and proxies the WebSocket to a running OCTAVE, dropping the `Origin` header on the upgrade so the service treats the dev page like a script. `GET /art` is proxied too.

```bash
./build/octave --dev --service                  # a simulated car, service on 9876
OCTAVE_PORT=9876 npm run dev                    # http://127.0.0.1:5173/, OCTAVE_HOST for another machine
npm run check                                   # svelte-check + tsc, must be clean
```

End to end, against a live OCTAVE that serves the build (CI has no backend, so this is a local check):

```bash
./build/octave --dev --service --web-root frontends/glass/dist     # the simulated car, page at :9876
OCTAVE_PORT=9876 npm run e2e                                       # OCTAVE_HOST, CHROME to override
```

`e2e/run.mjs` (plain Node 22+, a headless Chromium over the DevTools protocol, no extra packages) loads every page and fails on any console error, then drives the controls and reads the result back through the service's own TCP port: play / pause, next, the volume slider, mute, a settings toggle, a theme pick, a trouble-code read and a confirmed clear on the simulated car, and a dashboard made in the editor (place, drag with real pointer events, resize, save, the saved spec compared with the canvas, delete). It restores what it changed, and exits 0 with a note when no service answers or the page served is not this build.

The page reconnects on its own when OCTAVE goes away (`src/lib/octave.ts`, backoff up to 8 s) and re-issues every subscription on the new connection. Add `?theme=dark` or `?theme=light` to the URL to force a theme; the rail button cycles auto / dark / light and remembers the choice in the browser.

## Pages

- **Home**: the clock (OCTAVE's own text, in the user's format), now playing with transport and seek, the home gauges from `settingsManager.homeOBDParameters`, the vehicle and black-box state.
- **Media**: now playing in full (transport, seek, shuffle, volume, mute, the waveform from `audioAnalyzer.levels` while the page is open), the library sorted by title / artist / album, playlists (select, create, delete), move a song to a playlist, delete a song.
- **Vehicle**: every PID the car reports, live, with a filter (Live); connect / reconnect / disconnect / reset, rescan, read VIN, the connection log, serial ports, saved adapters, a typed address, Bluetooth scan / pair / connect (Connection); trouble codes with read, lamp status, clear behind a confirmation, and the freeze frame (Trouble codes).
- **Dashboards**: the chooser (new, edit, copy, share, delete, import from pasted JSON, restore built-ins), a full-screen view of any `schema: 1` spec drawn with this frontend's own gauges, and the editor: tap a widget then the grid to place it, drag the handle to move, the corner grip to resize, a PID picker filtered by the widget's kinds, the catalog's editable props, ranges in the user's units, colour swatches.
- **Trips**: the black box. Every drive with its summary, any recorded reading plotted with its band and a crosshair, the trip's events, export, delete, end the live trip, clear the history, the recorder switch.
- **Settings**: every setting, grouped like the bundled UI (Display, Media, Vehicle, Accessories, Device), with a theme picker that previews each palette from `themeCatalog` (and an option to tint this page's accent from the active theme), editors for the shift light bands, the home gauges, the gesture mapping, the knob's port, the IMU zero and cards, network / updates / power, and About. Whatever `describe settingsManager` lists with a `save_*` slot that has no row of its own appears under Everything else.
- **More**: yt-dlp search and the download queue, phone mirror and Android Auto session controls and state (no video: media streams are not on the service yet), diagnostics with the recent log, export and copy.

## What it uses from the API

| Object | For |
|---|---|
| `paramStore` | every live reading (one `subscribe` per page with the ids it shows) |
| `parameterCatalog.displayParameters` | titles, units, ranges, `displayScale` / `displayOffset` |
| `obdManager` | `setParameterDemand("glass-<page>", ids)`, connection, VIN, log, adapters, DTCs, freeze frame |
| `obdAdapterFinder` | Bluetooth scan, pair, connect |
| `nowPlaying`, `volumeController` | playback, position, seek, shuffle, volume, mute; `GET /art` with `artRevision` for the cover |
| `themeCatalog` | the palettes behind the theme picker, `activeTheme` for the accent tint |
| `audioAnalyzer` | `setDemand("glass", true)` while the Media page shows the waveform, `levels` |
| `mediaManager` | the library, sorting, playlists, delete |
| `settingsManager` | every property and `save_*` slot, `save_setting` for the shift light keys, the home gauges, gestures |
| `shiftLightManager` | the bands (`setFlags`), the live lamp |
| `dashboardManager` | `dashboards`, `loadDashboard`, `saveDashboard`, `uniqueIdForLabel`, `fitAt`, duplicate / delete / export / import |
| `tripRecorder` | `trips`, `requestDetail` / `requestSeries` (results arrive as `detailReady` / `seriesReady` signals), export, delete |
| `downloadManager` | search, the two list models (`rows` + `model` events), the queue actions |
| `phoneMirrorManager`, `androidAutoManager` | session state and controls |
| `esp32VolumeManager`, `berryIMU`, `gestureSensor`, `networkManager`, `diagnosticsManager`, `clock`, `fontCatalog` | the matching settings and device sections |

`src/lib/octave.ts` is the whole client (about 300 lines, one WebSocket): request / reply by id, `describe` cache, and subscriptions as Svelte stores (`prop`, `props`, `params`, `model`, `signal`) that replay on reconnect. `src/lib/stores.ts` holds the stores pages share. `src/gauges/` holds the dashboard primitives and the renderer, `src/lib/widgets.ts` the widget catalog: read from `dashboardManager.widgetCatalog` when the app serves it, with a built-in copy of the same data for an older app.

## Known gaps

- No phone-mirror video: media streams are not on the service yet (`TODO/open-backend.md`, step 6).
- Download search was exercised only as far as the backend's own error path (the test machine had no yt-dlp).
- The volume slider sends `applyVolume` while dragging; on a slow link the knob can lag the finger.
