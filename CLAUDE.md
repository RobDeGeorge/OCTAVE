# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OCTAVE (Open-source Cross-platform Telematics for Augmented Vehicle Experience) is a **C++ / Qt 6 / QML** infotainment system designed for vehicles. It runs on Windows, macOS, Linux, Raspberry Pi, iOS, and Android, and as a browser demo (WebAssembly) at robdegeorge.com/OCTAVE/.

### C++ only

OCTAVE is a **C++ / Qt 6** app (`src/`) with a QML frontend (`frontend/`). Every shipped binary on every platform (Windows `.exe`, macOS `.dmg`, Linux AppImage, Android `.apk`, iOS `.ipa` once enabled, the WebAssembly browser demo) is built from the C++ tree.

Until 2026-10-18 there was also a Python / PySide6 backend (`backend/`, `main.py`). It was frozen at the `python-final` branch (`b5463c9`, with its own wiki) and deleted from `main`. Don't reintroduce a Python backend, Python-on-Android (buildozer / p4a) or a PyInstaller pipeline.

The music downloader (yt-dlp, YouTube Music search, youtubedl-android on the phone, the `OCTAVE_ENABLE_DOWNLOADS` option and its "app store" builds) was removed on 2026-10-24 (service protocol 3). Every build is the same on that axis; music comes from the library folders only. Don't bring it back.

Python that remains is tooling around the C++ app, listed in `requirements.txt` and installed into `./venv`: `scripts/fetch_platform_tools.py`, `wiki/build_search_index.py`, `wiki/build_api_reference.py` (builds `wiki/api-reference.html` from `docs/api/`), `scripts/gen_gauge_param_docs.py` (regenerates the parameter table in `docs/GAUGE_AUTHORING.md` §6 from the C++ catalog), the Blender scripts in `tools/vehicle/`, `tools/trips/make_mock_trip.py` (a mock drive for the black box / Trip History page), and the gitignored `dev/` clients that drive the app over the command server.

## Commands

### C++ build (primary)

```bash
# Configure + build (debug)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Run
./build/octave

# Developer tooling (OCTAVE_DEV_TOOLS: default ON only for -DCMAKE_BUILD_TYPE=Debug)
./build/octave --dev             # simulated car over a fake ELM327 + dev overlay
./build/octave --profile         # perf monitor + JSON command server on 127.0.0.1:9876
./build/octave --dev --profile   # both: what the screenshot / MCP / perf tooling launches

# C++ tests (tests/cpp, QtTest; OCTAVE_BUILD_TESTS defaults ON for Debug). CI runs these.
ctest --test-dir build --output-on-failure
```

See `BUILD.md` for the full build matrix (iOS / Android / Flatpak / WebAssembly and the desktop targets).

### Python helper tools

```bash
python3 -m venv venv && venv/bin/pip install -r requirements.txt   # dev-tool clients + docs builders
venv/bin/ruff check .                                              # lint scripts/ and packaging/ (CI runs it)
python wiki/build_search_index.py                                  # after wiki edits
```

## Architecture

**A C++ / Qt 6 backend (`src/`) + a QML frontend** (`frontend/`) communicating via Qt Signals/Slots.

### Backend — C++ (`src/`, primary for builds & distribution)

Every feature is a C++ **manager class** inheriting from `QObject`. Managers expose state to QML via `Q_PROPERTY`, `Q_INVOKABLE`, and Qt signals. All managers are members of `AppCore` (`src/appcore.{h,cpp}`: built and wired there with no QML engine, each entered in the service registry by `expose()`); `src/main.cpp` builds the core and registers every entry as a QML context property (`core.exposeTo(ctx)`), so the QML UI and the service protocol (`src/service/`) see the same objects. Build system is CMake; sources are listed explicitly in `CMakeLists.txt`'s `qt_add_executable(octave ...)` call.

Key managers (`src/managers/*.{h,cpp}`):
- `settingsmanager` — Central settings store, persists JSON to OS-specific config dirs
- `mediamanager` — Local audio playback via QMediaPlayer
- `obdmanager` + `elm327protocol` — OBD-II diagnostics
- `nowplaying` — what is playing and its controls whichever source has them (phone while mirroring, else `mediamanager`); owns the phone-position clock and the phone/local hand-off (context property `nowPlaying`)
- `parameterstore` — the latest value of every PID / IMU reading as a `QQmlPropertyMap` (context property `paramStore`, one notifying key per id, 33 ms coalescing), fed from `OBDManager::parameterSampled` and the IMU signals in `src/appcore.cpp`
- `parametercatalog` — title / unit / range / kind / decimals of every OBD PID and IMU reading (context property `parameterCatalog`); new PIDs need a row here as well as a decoder in `elm327protocol` (`tst_parametercatalog` enforces it)
- `themecatalog` — the colour themes as data (`frontend/themes.json`: the built-in palettes, their order, light / dark grouping) and `activeTheme` / `activeThemeName`, the palette in force resolved as `Style.qml` resolves it (album art > custom > built-in > fallback), live from the theme settings (context property `themeCatalog`); `Style.qml` keeps behaviour and animation
- `shiftlightmanager` — RPM shift light rules (legacy flag migration, band match, flash phase) fed by `OBDManager::rpmChanged`; the QML overlays and home-card lamp only bind to `active` / `activeFlag` / `lit`
- `obdadapterfinder` (+ `src/platform/bluezpairer`) — Bluetooth OBD adapter scan / PIN pairing / trust / connect (desktop Linux so far; see `TODO/obd-adapter-discovery.md`)
- `esp32volumemanager` — Wireless rotary encoder (serial)
- `berryimumanager` — I²C sensor hub
- `gesturemanager` — PAJ7620U2 gesture sensor
- `audioanalyzer` — FFT waveform visualization
- `androidautomanager`, `phonemirrormanager` — Android Auto / phone mirroring (built-in scrcpy-protocol client in `src/phone_mirror/`, no external scrcpy)
- `triprecorder` + `tripstore` — the black box: every OBD reading of every drive into `<config>/history/blackbox.sqlite` (SQLite compiled in from `third_party/sqlite/`, own thread; never go through Qt Sql, its driver is a separate runtime plugin that the Jeep's Ubuntu box did not have) plus one CSV + JSON per trip under `history/trips/`; taps `OBDManager::parameterSampled` and registers a background poll sweep (`setBackgroundDemand`) for every PID the vehicle supports. Viewer: `frontend/TripHistory.qml`
- `frontendmanager` (+ `frontendsupervisor`) — installable frontends: the active screen, running and restarting an exec frontend with fall-back to QML (service object `frontendManager`)
- `clock`, `networkmanager`, `volumecontroller` — self-explanatory

Developer tooling lives in `src/dev/` (committed, compiled only with the `OCTAVE_DEV_TOOLS` CMake option, inert without `--dev` / `--profile`): `devruntime` wires it up from `main.cpp`, `coreactions` + `uidriver` the UI / perf actions it adds to the JSON-lines TCP command server (which lives in `src/service/` with the service protocol, built in every configuration and off unless `--service` / `serviceEnabled` / `--profile` asks for it), `perfmonitor` the profiler, `devkeyboard` the dev keyboard shortcuts, `collagebridge` the collage tool's in-app half, `qml/` the dev overlay. The simulator is separate and built in every configuration, inert unless `--sim` (or `--dev`, which implies it) starts it: `src/sim/` holds `carsim` + `simelm327` (a simulated car behind the *real* `OBDManager`, registered with `OBDConnectionWorker::setCustomTransport()` for port `sim://elm327`), `imufeed` simulated IMU motion, `simconfig` (the throwaway config `--sim` runs on unless `--config DIR`) and `Simulator`, the experimental `sim` service object. `octave --headless --sim` from any release build is what a frontend author develops against. Time hot paths with `OCTAVE_PERF_SCOPE("label")` from `src/util/perfscope.h` (empty in Release). The clients in the gitignored `dev/` tree (MCP servers, screenshot / story / collage / perf scripts, dashboard drivers) launch the C++ binary through `dev/app_launch.py` (`OCTAVE_BIN` to pick one).

When adding a new manager: add `.h`/`.cpp` to `src/managers/`, list both in `CMakeLists.txt`, add a member to `AppCore` (`src/appcore.h`, in construction order), wire it and `expose("name", &member)` in `src/appcore.cpp` (that is its QML context property and its service object). Declare what a paired device needs to call its methods / set its properties with `Q_CLASSINFO("octave.scope", "...")` plus `octave.scope.read` / `.full` / ... lists for the exceptions (`src/service/metadescribe.h`); `--dump-api` refuses a manager without it.

### Change workflow

1. Implement in C++ (`src/`); QML in `frontend/`.
2. Build and test: `cmake --build build -j && ctest --test-dir build`. Add or extend a QtTest in `tests/cpp/` for logic changes (`octave_add_test()` in its CMakeLists; give anything that touches settings a temp `XDG_CONFIG_HOME`). Drive the result with `./build/octave --dev --profile` and the dev tooling (screenshots, stories, dashboard drivers, MCP tools) when it touches the UI.
3. Update the wiki (below) and `docs/GAUGE_AUTHORING.md` when the gauge API changes. When a manager's public surface changes (a `Q_PROPERTY`, `Q_INVOKABLE`, slot or signal, or a PID), regenerate the published API in the same commit: `./build/octave --dump-api docs/api && venv/bin/python wiki/build_api_reference.py && npm --prefix sdk/typescript run generate && (cd sdk/go && go generate ./...)` (`tst_apidocs` fails otherwise, and so do `sdk/typescript`'s `npm test` and `sdk/go`'s `TestGeneratedFilesAreCurrent` in CI). A breaking change to a stable member also bumps `kProtocolVersion` (`src/service/protocol.h`, `docs/api/README.md`) and names each break as `object.member` under the new version in `docs/api/CHANGELOG.md`; `python tools/api/check_compat.py` (CI, `lint`) fails otherwise, and `--draft` lists the changes. Prefer deprecating (`Q_CLASSINFO("octave.deprecated.<member>", "use ...")`) over breaking; mark new members you are unsure of `Q_CLASSINFO("octave.experimental", "...")` (`src/service/metadescribe.h`).

### Service API (`src/service/`, `sdk/`, `docs/api/`)

The QML UI is the reference frontend; every manager is also served to other programs over the service protocol: JSON lines over TCP, or WebSocket frames, on `127.0.0.1:9876`. The ops are `hello` / `describe` / `get` / `set` / `call` / `subscribe` / `rows` / `stream` and `pair` / `unpair` / `mediaTicket` (`servicebridge`, `serviceregistry`, `metadescribe`, `websocket`, `commandserver`, `streamhub`). The service is off unless `--service [PORT]`, the `serviceEnabled` setting, `--headless` (managers + service, no QML engine) or `--profile` (dev builds) turns it on, and it binds loopback unless `--service-bind`. Off the device a client pairs with a 6-digit code (protocol 2, `servicepairing`; `octave --pair-code` prints one from the running instance) and holds permission scopes (`servicescopes.h`: read, media, vehicle, phone, settings, files, system, full); browsers are held to the app's own page by an Origin rule; every connection has limits (`CommandServer::Limits`: `rateLimited`, connects per address, a reader that stays behind; ctest `service_load_check`). `docs/api/` is the published API: generated by `--dump-api`, checked by `tst_apidocs`, versioned by `kProtocolVersion`, kept compatible by `tools/api/check_compat.py` (CI) with `docs/api/CHANGELOG.md`; `docs/api/protocol.json` is the envelope as JSON Schema. `tst_conformance` fails if QML reaches a context property set around `AppCore::expose` or a member `docs/api/` doesn't list. Clients: `sdk/python` (stdlib only, `sdk_python` ctest), `sdk/web` (embedded, served at `GET /`), `sdk/typescript` and `sdk/go` (generated from `docs/api/`; `sdk_go` ctest); starter projects in `templates/`; `tools/api/fake_server.py` for SDK authors. Installable frontends: `frontendmanager` (+ `frontendmanifest`, `frontendsupervisor`), Settings > Device > Frontends. Walkthrough: `wiki/writing-a-frontend.html`. Still to do: the process split (`TODO/open-backend.md`) and the frontend platform's device checks (`TODO/frontend-platform.md`).

### Frontend (`frontend/`)

QML files using Qt Quick 2.15. Entry point is `Main.qml`.

- Top-level QML files are full views/pages (e.g., `MediaPlayer.qml`, `CarMenu.qml`, `OBDMenu.qml`)
- `BottomBar.qml` — Persistent navigation and volume control
- `EnvironmentTheme.qml` — fixed style tokens; album-art colours are computed in C++ (`MediaManager`) and applied by `Style.qml`, which takes the theme palettes from `themeCatalog`
- `Style.qml` / `Spacing.qml` — Design tokens
- `settings/` — Settings pages and reusable settings UI components (cards, toggles, sliders, etc.)

### Communication Pattern

Backend managers are registered as QML context properties at app startup.

C++ (`src/main.cpp`):
```cpp
engine.rootContext()->setContextProperty("mediaManager", &mediaManager);
```

QML accesses them by name:
```qml
mediaManager.next_track()
```

Custom QML types (for embedded video frames) are registered with `qmlRegisterType` under `OCTAVE.AndroidAuto` and `OCTAVE.PhoneMirror` modules.

### Volume System

Volume uses a **quadratic curve** (`(volume/100)^2.0`) for the UI percent → linear audio mapping. `src/managers/volumecontroller.{h,cpp}` owns the curve math and the `VolumeController` QObject, which is the single entry point for routing a 0–100 percent to every output (local media, phone mirror, ESP32 LED). All handlers (gesture sensor, ESP32 knob, command server) and QML slider widgets call `volumeController.applyVolume(percent)` and `volumeController.toggleMute()` — never touch the individual manager `setVolume` / `setMuted` methods or hardcode the curve. `VolumeController` also owns mute (in memory) and its rules: raising the volume while muted unmutes from any input, and the mute toggle at 0 % restores the startup volume. QML binds to `volumeController.percent` / `muted` / `effectivelyMuted` and never does curve math; the inverse is `toPercent()`. Changing the curve only requires editing `toLinearStatic()` and `toPercentStatic()` in `src/managers/volumecontroller.{h,cpp}`.

### Threading

Heavy I/O runs on worker threads to avoid blocking the UI:
- OBD connection uses a dedicated `QThread` worker with progress signals
- I²C sensors (BerryIMU, gesture) are read in a loop on their own thread (`QThread` worker; the IMU loop is paced at 200 Hz) and deliver values to the GUI thread by signal; `QTimer`s only drive lifecycle (delayed start, retry back-off). The ESP32 knob uses `QSerialPort::readyRead`

### Image Providers

A custom `QQuickImageProvider` streams video frames directly to QML for Android Auto (`dhuframe`), avoiding file I/O. Phone mirror decodes H.264 itself (libavcodec) and pushes `QVideoFrame`s to the `QVideoSink` of the QML `VideoOutput`.

### Settings Persistence

User settings stored in `settingsConfigure.json` at OS-specific paths:
- Windows: `%APPDATA%/OCTAVE/`
- macOS: `~/Library/Application Support/OCTAVE/`
- Linux: `~/.config/OCTAVE/`

### Logging

Rotating log files in `logs/` subdirectory of the config path:
`octave-cpp.log` (5 MB, 3 backups), `octave-cpp-error.log` (WARNING+ plus crash backtraces; 2 MB, 5 backups), `octave-cpp-debug.log` (debug only; 10 MB, 2 backups), written by `src/util/logger.cpp`.

### WebAssembly (browser demo)

`packaging/wasm/build.sh` (Qt 6.12 `wasm_multithread` kit + emsdk 5.0.5, `QT_WASM` / `QT_HOST`) builds the app for the browser; `.github/workflows/wiki-pages.yml` publishes it at robdegeorge.com/OCTAVE/ with the wiki at `/OCTAVE/wiki/`. `EMSCRIPTEN` in CMake defines `OCTAVE_WASM` and `Q_OS_MOBILE` and drops Qt SerialPort / Bluetooth, TagLib and FFmpeg. `src/platform/wasm/` holds what replaces them: stand-in `QSerialPort` / `QSerialPortInfo` headers whose ports never open, inert `obdadapterfinder_wasm.cpp` / `networkmanager_wasm.cpp` (swapped in for the real `.cpp`), and `webdemo` (the `src/sim` simulator, the same one `--sim` runs, driven by an autopilot). Guard any new `QProcess` use with `#if QT_CONFIG(process)` and new hardware APIs with `OCTAVE_WASM`, or the browser build breaks. `frontend/` is staged into the page's filesystem by `packaging/wasm/stage_frontend.cmake` (keep big unused assets out of it: every MB is downloaded by every visitor). Threads need cross-origin isolation, which `packaging/wasm/coi-serviceworker.js` provides on GitHub Pages; serve local builds with COOP/COEP headers.

### Build & CI

CMake + Qt 6 produces native executables per platform (see `BUILD.md`). Mobile (Android `.apk` / iOS `.ipa`) goes through the Qt for Mobile pipeline.

GitHub Actions (`.github/workflows/build.yml`) runs `lint` (ruff over the repo's Python tools) and `cpp-compile-check` (Linux CMake configure + build of the C++ tree, `ctest` over `tests/cpp`, and a `qmllint` error pass over `frontend/`) on every push and PR to `main`, plus `phone-server` (jar/source invariants) and `sdk` (`sdk/typescript`: generated types current against `docs/api/`, type check, tests). On version tags (`v*`) or manual dispatch, the C++ matrix runs (`cpp-build-windows`, `cpp-build-macos`, `cpp-build-linux`, `cpp-build-linux-arm64`, `cpp-build-android`, each gated on `lint` + `cpp-compile-check`) and the `release` job attaches every produced artifact (`.exe`, `.dmg`, x86_64 and aarch64 AppImages, `.apk`) to a single GitHub Release.

## Key Conventions

- Qt Quick Controls style is forced to `"Basic"` for full slider customization
- QML import path includes `frontend/` — QML files there are importable by name
- The `dev/` directory is gitignored and fully isolated from production code
- C++ logs through `QLoggingCategory`

## Wiki Maintenance

The project wiki lives in `wiki/` (static HTML pages — `architecture.html`, `development.html`, `media-manager.html`, `obd-manager.html`, etc., plus `index.html` as the entry point and `search-index.js` for full-text search).

**Whenever a code change affects documented behavior, update the corresponding wiki page in the same commit.** This includes:

- Adding, removing, or renaming a backend manager or its public Slots/Signals/Properties → update the relevant manager page and `signals-slots-reference.html`
- Changing the settings schema or adding a new setting → update `settings-reference.html` and `settings-manager.html`
- New build/test/lint commands or CI changes → update `development.html` and `building.html`
- New dev tools, keyboard shortcuts, or MCP capabilities → update `development.html`
- New QML components or view-level changes → update `components.html` / `pages.html` / `frontend-overview.html`
- New hardware support (sensors, controllers, protocols) → update `hardware-managers.html` and `hardware-setup.html`
- Theme, style token, or animation system changes → update `theme-system.html`

If you are unsure which page to update, `wiki/index.html` lists all pages. Rebuild the search index if wiki content changes: `python wiki/build_search_index.py`. The wiki is the user-facing reference — stale docs are worse than missing docs, so treat wiki updates as part of "done" for any feature or refactor.

## Phone server (`phone_server/`)

The Java program that runs on the phone during mirroring is OCTAVE's fork of the scrcpy server (Apache-2.0, forked at v3.3.4, package `org.octave.phoneserver`). The app pushes the prebuilt jar `tools/phone-server/octave-phone-server` over adb (it is compiled into the binary as a Qt resource) and speaks the scrcpy 3.x protocol to it (`src/phone_mirror/scrcpyclient.{h,cpp}`). After editing anything under `phone_server/src`, rebuild the jar with `phone_server/build_without_gradle.sh` (see `BUILD.md`) and commit it together with the source — the `phone-server` CI job rebuilds the jar, fails if either jar lacks the `org/octave/phoneserver/Server` class or the version string, and warns when the two hashes differ. The version string in `build_without_gradle.sh` (`SCRCPY_VERSION_NAME`) must equal `kServerVersion` in the client; the server refuses a mismatch. Protocol byte layouts live in `docs/PHONE_MIRROR_NATIVE_PLAN.md`; when pulling upstream scrcpy changes, re-verify them against `phone_server/src/main/java/org/octave/phoneserver/` (`DesktopConnection`, `control/ControlMessageReader`, `device/Streamer`).

## Gauges & Dashboards

Dashboards are built in layers, each feeding the next:

1. **Data** — the C++ `paramStore` holds every live value (fed from `obdManager.parameterSampled` and the IMU); `frontend/OBDParameterModel.qml` (singleton) serves it as `liveValue(paramId)` (per-key, or the editor's demo sweep) and serves the per-PID metadata (title / unit / min / max / `kind` / decimals) from the C++ `ParameterCatalog` (`src/managers/parametercatalog.cpp`, context property `parameterCatalog`), plus the vehicle support gate. Widgets never touch `obdManager` directly. Polling is demand-driven: pages register the PIDs they show via `obdManager.setParameterDemand(consumer, ids)`.
2. **Primitives** — `frontend/gauges/` (`CircularGauge`, `ArcGauge`, `BarGauge`, `LinearGauge`, `DigitalReadout`, `SparklineGauge`, `WarningLight`, plus the self-binding `GForceGauge` and `CompassGauge`) and media widgets in `frontend/dashboards/widgets/` (`NowPlayingWidget`, `MediaControlsWidget`). PID-bound primitives share one binding API: set `paramId`; everything else defaults from the PID and the theme and can be overridden per widget.
3. **Widget catalog** — `frontend/dashboards/widget-catalog.json` is the single registry of widget types: default span, `supportedKinds`, label/range/color capability flags, curated `editableProps`. `DashboardManager` serves it as `widgetCatalog` (published as `docs/api/widget-catalog.json`) and `normalizeSpec` checks props against it; the QML singleton `WidgetCatalog.qml` adds only the type → url map. The renderer, editor palette, properties panel and every other frontend read the same data. A widget needs both a catalog entry and a url to render or be placed.
4. **JSON specs** — a dashboard is a `"schema": 1` grid spec rendered by `frontend/dashboards/DashboardRenderer.qml` (bad cells are skipped/placeheld, never crash). Built-ins ship read-only in `frontend/dashboards/presets/`; user dashboards live in `<config>/dashboards/*.json` (hot-reloaded).
5. **Persistence + editor** — `DashboardManager` (`src/managers/dashboardmanager.{h,cpp}`) enumerates, saves, duplicates, deletes, exports, and imports (sanitized, data-only, ids always regenerated). The in-app editor (`frontend/dashboards/editor/`) is pure QML over that API: tap-to-place, drag-to-move, corner-grip resize, PID picker, properties panel.

Entry point: the square "Dashboards" button at the top-right of `OBDMenu.qml` opens the chooser (New / Edit / Copy / Delete / Share / Import); a horizontal swipe cycles Parameter Cards and every dashboard. Remaining on-device checks: `TODO/dashboards-roadmap.md`; next step (shareable widget presets/composites, code widgets only behind an opt-in): `TODO/user-primitives.md`.

**When the user asks you to build a new gauge or dashboard, read `docs/GAUGE_AUTHORING.md` first.** It is the complete, stand-alone spec: shared binding API, every primitive's props with defaults, theme tokens, angle math for needles, the full list of 129 supported parameter IDs, and step-by-step recipes for adding a new dashboard or primitive. Treat that doc as the source of truth and update it in the same commit whenever you change the gauge API or add/remove a primitive.

## TODO folder

The `TODO/` directory at the repo root (indexed, easiest to hardest, in `TODO/README.md`) holds standalone plans for work that has been **intentionally deferred** — things we know we want to do eventually but aren't committing to right now. Each plan is a single markdown file that can be picked up cold by a future session (yours, mine, or a collaborator's) without needing conversation history.

**What belongs in `TODO/`:**
- Refactors that are risky, scoped, and require setup before they're safe to do (e.g. splitting oversized manager files)
- Feature work that's been scoped but deprioritized (e.g. in-app error notification UI)
- Time-sensitive maintenance with a known deadline (e.g. third-party API migrations)
- Test-suite expansions we know we want but haven't committed to
- Anything the user said "let's come back to this later" about

**What does NOT belong in `TODO/`:**
- Active in-progress work (that's what tasks/plans/branches are for)
- Bug reports (file those as issues)
- General ideas or wishlists — only plans concrete enough that someone could act on them
- Notes about things we already did

**Format for new TODO files:**
- Lead with a `**Status:**` line (deferred / parked / time-sensitive) and a `**Last updated:**` date in ISO format
- Explain **why it's parked**, not just what it is — future-you needs to know whether the rationale still applies
- Include enough context, file paths, and line numbers that someone could start work without re-investigating
- Call out prerequisites and dependencies between TODO items (e.g. "don't split `obd_manager.py` until the ELM327 test suite exists")
- End with a recommended order of operations and a "delete this file when done" reminder

**When the user parks something:** offer to write it up as a TODO file. When you write a TODO file, mention any cross-references to other TODO items so the folder stays internally consistent. New and trimmed files open with a one-sentence **Goal:** and give each step an **Effort:** (S/M/L/XL) and the device it needs; add a row to `TODO/README.md`. When a TODO item is completed, **delete the file** (and its README row) — stale TODOs rot faster than stale code.
