# Service API changelog

What each protocol version added, deprecated and broke. Newest first. The rules are in [README.md](README.md#versioning); `tools/api/check_compat.py` (CI, `lint` job) fails a change that breaks a stable member without a protocol bump and an entry here naming it as `object.member`. `python tools/api/check_compat.py --draft` prints the entries for a change.

Additions do not bump the version; they are listed under the version that was current when they landed.

## Protocol 3

2026-10-24. The music downloader is removed (yt-dlp and YouTube Music search, every platform).

Breaking:
- Object `downloadManager` removed, with every member: its search and download methods, `searchResults` / `downloadQueue` models and signals. There is no replacement; music comes from the library folders. The rest of the envelope is unchanged: `rows` and model events stay, for list models to come.
- `mediaManager.play_downloaded_song` removed (it found a just-downloaded track by artist and title). `mediaManager.play_file_at_path` stays.

Settings: `downloadSubfolder`, `downloadFormat` and `downloadBitrate` are gone, and are dropped from an existing settings file at start-up.

Added:
- `dashboardManager.arrange` (experimental): moves or resizes a cell, the cells it lands on swapping with it or moving to the nearest free space; `{ok, cells, moved, swapped, reason}`.
- `dashboardManager.minSpan` / `sizeOptions` (experimental): a widget type's smallest readable span and its S / M / L spans on a given grid. The widget catalog gains `minSize` (`{w, h}` dp) and `aspect` on every widget, and the `editableProps` `BarGauge.maxThickness` and `WarningLight.maxSize`.
- `dashboardManager.nearestFree` (experimental): the free spot of a span nearest a grid cell, `{ok, col, row, colSpan, rowSpan}`.
- `dashboardManager.rescale` (experimental): the cells laid out again on another grid size (the editor's Coarse / Normal / Fine), `{ok, cells, reason}`.
- `dashboardManager.templates` / `fromTemplate(id, supported)` (experimental): the starting points for a new dashboard (`frontend/dashboards/templates/*.json`) and one filled in with the PIDs a car reports.
- `parameterCatalog.groups` and a `group` on every parameter row ("Engine", "Fuel & air", "Temperatures", "Electrical", "Speed & motion", "Emissions", "Diagnostics", "Sensors"), for pickers that list parameters by system.
- Widget catalog: `category`, `description` and `demo` (`{paramId, props}`) on every widget, `section` (`look` | `range` | `advanced`) on every editable prop, top-level `categories` and `suggestions` (`byId`, `byKind`: the widget to propose for a parameter). The value gauges (Circular, Arc, Bar, Segmented, Linear, DigitalReadout, Sparkline) share alert props: `warnAbove`, `warnBelow`, `critAbove`, `critBelow`, `alertFlash`.

- Dashboard spec schema 2 (`dashboardManager.supportedSchema` is now 2): an optional `layouts.portrait` `{gridColumns, gridRows, cells}`, one placement per cell, for portrait screens (`docs/api/dashboard-spec.schema.json`). A spec without `layouts` is still written as schema 1. `dashboardManager.derivePortrait(cells, gridColumns, gridRows)` and `orientedSpec(spec, portrait)` (experimental): a portrait layout derived from a landscape one, and the spec as laid out on either shape. The built-in presets are schema 2.

Changed (dashboard files, not the protocol): `warnAbove` on Bar and Segmented gauges is now the *warning* level, drawn in the theme's warning colour (amber); it was red. `critAbove` is the red level. The built-in presets moved their limits to `critAbove`; a user dashboard that wants red again sets `critAbove` instead.
- `obdManager.simulated` / `simulatedChanged` / `simulatedAdapterRequested`: the built-in simulated car (`sim://elm327`) is an adapter in every build, picked in the adapter sheet or by `obdAdapterFinder.connectAdapter("sim://elm327")`; it is never saved as the OBD port.
- `mediaManager.pick_browser_library`: in the browser demo, the browser's folder picker makes the folder picked the library for the visit (true); elsewhere it returns false.
- The audio output picker: `settingsManager.audioOutputDevice` / `audioOutputDeviceChanged` / `save_audio_output_device` (a QAudioDevice id, empty = the system default), `mediaManager.audioOutputs` / `currentAudioOutput` / `audioOutputsChanged`, `phoneMirrorManager.setAudioOutputDevice`.

## Protocol 2

2026-10-23. Pairing.

Behaviour (no protocol change):
- `pair` guesses are limited per address, not across addresses: a code is no longer void after 20 wrong tries in all, and the one-attempt-a-second interval is per address, so other clients' wrong codes neither void the owner's code nor delay their attempt. New: at most 10 attempts a second across every address but loopback ("too many pairing attempts"); loopback is never throttled.

Breaking:
- Over the network (`--service-bind`), every op but `hello` and `pair`, and `/art` / `/export/...`, answer `{"ok": false, "code": "unauthorised"}` (HTTP 401) until the client pairs. On the device itself nothing changes while `serviceLoopbackOpen` is true (the default).

Added (some landed while 1 was current, but none was released under 1):
- Ops `pair`, `unpair`, `mediaTicket`; `hello` takes a `token` and reports `auth`, `loopbackOpen` and `tokenRejected`.
- Ops `stream` / `unstream` and binary records: `phoneMirrorManager.video` (H.264), `androidAutoManager.video` (JPEG).
- `GET /art` (`?song=`, `?queue=`), `GET /export/trip`, `/export/dashboard`, `/export/logs`.
- Object `themeCatalog`.
- `audioAnalyzer.setDemand`, `clock.time`, `dashboardManager.lastError` / `lastErrorChanged`, `dashboardManager.lastExportPath` / `lastExportPathChanged`, `dashboardManager.widgetCatalog`, `mediaManager.browse_music_folders`, `nowPlaying.artRevision`, `settingsManager.applyNowPlayingPreset`, `settingsManager.apply_settings`, `settingsManager.nowPlayingPresets`.
- `settingsManager.save_media_folder` returns whether it saved (it returned nothing).
- Every member's `stability` (`experimental` when it is not covered by the version) and `deprecated` in `describe` and `docs/api/`.
- Permissions: `pair` takes `scopes` (absent: `full`, as before) and answers the granted `scopes`, as does a trusted `hello`; every method and writable property lists its `scope`; error code `forbidden`. The dev `action` form needs `full`.
- `describe` with no object also answers `ops`, the op names the server has.
- `docs/api/protocol.json`: the envelope (ops, errors, events, stream records, HTTP routes, pairing) as JSON Schema, checked against the server by `sdk_python`.
- Object `sim` (experimental, not covered by the version): the simulated car of `--sim`, in every build. Flags `--sim` and `--config DIR`.
- Limits per connection ([README.md](README.md#limits-per-connection)): past 400 ops at once or 200 a second an op is answered `{"ok": false, "code": "rateLimited", "retryAfterMs": N}` (dev actions are not counted); a connection that leaves 4 MB unread for 15 s is closed; new connections past 20 a second from one network address (100 from this device) are closed at once. `rateLimited` and `retryAfterMs` are new in error replies.
- Object `frontendManager`: installed frontends (`octave-frontend.json`), install from a folder or zip, remove, the active screen.
- Object `frontendManager`: installed frontends (`octave-frontend.json`), install from a folder or zip, remove, the active screen. An active exec frontend runs with a session token of its manifest's scopes, and loopback trust is off for that run; flag `--qml`.
- Flag `--pair-code`: a pairing code from the running instance over a per-user local channel, for pairing while no Settings page is on screen.
- While an installed web frontend is served, a browser on the device holds the frontend's manifest scopes rather than loopback's full trust.

## Protocol 1

2026-10-22. The first published API.

- Ops `hello`, `describe`, `get`, `set`, `call`, `subscribe` / `unsubscribe`, `rows`; list-model events.
- Transports: JSON lines over TCP and WebSocket text frames on the same port.
- Every manager of `AppCore` as a service object, `docs/api/` generated by `octave --dump-api`.
