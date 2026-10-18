# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OCTAVE (Open-source Cross-platform Telematics for Augmented Vehicle Experience) is a **C++ / Qt 6 / QML** infotainment system designed for vehicles. It runs on Windows, macOS, Linux, Raspberry Pi, iOS, and Android.

### Backend state: C++ only; the Python backend is frozen (deprecated 2026-10-18)

OCTAVE is a **C++ / Qt 6** app (`src/`) with a QML frontend (`frontend/`). All development happens in C++. Every shipped binary on every platform (Windows `.exe`, macOS `.dmg`, Linux AppImage, Android `.apk`, iOS `.ipa` once enabled) is built from the C++ tree.

The Python / PySide6 backend (`backend/`, `main.py`, `setup.py`) is **deprecated and frozen** as of 2026-10-18. Its last maintained state is the `python-final` branch (commit `b5463c9`, C++/Python parity as of that day); anyone wanting a Python version forks from there. Rules until it is deleted:
- **Do not port changes to `backend/`** and do not add Python peers for new C++ managers, settings or QML bindings. The parity rule is gone.
- Python stays in `main` only because `tests/` (pytest) still exercises it and is the only behavioural test suite. Once C++ QtTest / ctest targets replace it, `backend/`, `main.py`, `setup.py`, `requirements.txt` and the Python tests are deleted (`TODO/python-removal.md`). Expect the Python backend to drift and break against new QML; that is accepted. Don't fix it.
- QML may now use C++-only manager members freely; no `typeof` guards are needed for Python's sake.
- Don't reintroduce Python-on-Android (`backend/platform_config.py`, `backend/stubs.py`, `backend/android_obd_manager.py`, `backend/android_sensors.py`, `deployment/buildozer.spec`, `requirements-android.txt`) or the PyInstaller pipeline.

## Commands

### C++ build (primary)

```bash
# Configure + build (debug)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Run
./build/octave

# App store build (downloads feature stripped out)
cmake -S . -B build -DOCTAVE_ENABLE_DOWNLOADS=OFF
cmake --build build -j

# Developer tooling (OCTAVE_DEV_TOOLS: default ON only for -DCMAKE_BUILD_TYPE=Debug)
./build/octave --dev             # simulated car over a fake ELM327 + dev overlay
./build/octave --profile         # perf monitor + JSON command server on 127.0.0.1:9876
./build/octave --dev --profile   # both: what the screenshot / MCP / perf tooling launches
```

See `BUILD.md` for the full build matrix (9 targets including iOS / Android / Flatpak / app store variants).

### Python build (frozen, deprecated; don't develop here)

```bash
# First-time setup + run (creates venv, installs deps, launches app)
python setup.py

# Setup only, don't launch
python setup.py --no-run

# Run the app directly (activate venv first)
source venv/bin/activate
python main.py

# Debug mode (enables debug logging)
python main.py --debug

# Developer mode (simulated OBD data, keyboard controls for testing)
python -m dev.main_dev

# Lint (ruff — configured in pyproject.toml)
source venv/bin/activate
ruff check .

# Smoke tests (headless — imports + safe manager instantiation)
QT_QPA_PLATFORM=offscreen pytest tests/
```

A minimal smoke test suite lives in `tests/` and runs in CI on every push and PR to `main`. Ruff runs in warn-only mode (`continue-on-error` on the `lint` job in `.github/workflows/build.yml`); flipping critical rules (E722, F821) to blocking is deferred until the Phase 1 cleanup lands.

## Architecture

**A C++ / Qt 6 backend (`src/`) + a QML frontend** (`frontend/`) communicating via Qt Signals/Slots. The frozen Python backend (`backend/`) still exposes the same API surface as of `python-final`, but is no longer kept in step.

### Backend — C++ (`src/`, primary for builds & distribution)

Every feature is a C++ **manager class** inheriting from `QObject`. Managers expose state to QML via `Q_PROPERTY`, `Q_INVOKABLE`, and Qt signals. All managers are instantiated in `src/main.cpp` and registered as QML context properties via `engine.rootContext()->setContextProperty(...)`. Build system is CMake; sources are listed explicitly in `CMakeLists.txt`'s `qt_add_executable(octave ...)` call.

Key managers (`src/managers/*.{h,cpp}`):
- `settingsmanager` — Central settings store, persists JSON to OS-specific config dirs
- `mediamanager` — Local audio playback via QMediaPlayer
- `obdmanager` + `elm327protocol` — OBD-II diagnostics
- `obdadapterfinder` (+ `src/platform/bluezpairer`) — Bluetooth OBD adapter scan / PIN pairing / trust / connect (desktop Linux so far; see `TODO/obd-adapter-discovery.md`)
- `esp32volumemanager` — Wireless rotary encoder (serial)
- `berryimumanager` — I²C sensor hub
- `gesturemanager` — PAJ7620U2 gesture sensor
- `audioanalyzer` — FFT waveform visualization
- `androidautomanager`, `phonemirrormanager` — Android Auto / phone mirroring (built-in scrcpy-protocol client in `src/phone_mirror/`, no external scrcpy)
- `downloadmanager` — yt-dlp wrapper, compiled out for app-store builds via `OCTAVE_ENABLE_DOWNLOADS` CMake option
- `clock`, `networkmanager`, `volumecontroller` — self-explanatory

Developer tooling lives in `src/dev/` (committed, compiled only with the `OCTAVE_DEV_TOOLS` CMake option, inert without `--dev` / `--profile`): `devruntime` wires it up from `main.cpp`, `commandserver` + `coreactions` + `uidriver` are the TCP command server (same protocol and actions as `backend/command_server.py`), `perfmonitor` the profiler, `carsim` + `simelm327` a simulated car behind the *real* `OBDManager` (registered with `OBDConnectionWorker::setCustomTransport()` for port `sim://elm327`), `devimufeed` simulated IMU motion, `collagebridge` the collage tool's in-app half, `qml/` the dev overlay (shared with Python dev mode). Time hot paths with `OCTAVE_PERF_SCOPE("label")` from `src/util/perfscope.h` (empty in Release). The clients in the gitignored `dev/` tree (MCP servers, screenshot / story / collage / perf scripts, dashboard drivers) launch the C++ binary through `dev/app_launch.py` (`OCTAVE_BIN` to pick one, `OCTAVE_BACKEND=python` for the Python backend).

When adding a new manager: add `.h`/`.cpp` to `src/managers/`, list both in `CMakeLists.txt`, instantiate in `main.cpp`, register as a QML context property.

### Backend — Python (`backend/`, frozen; reference only)

Each feature is a Python **manager class** inheriting from `QObject`. Managers expose state to QML via `Signal`, `Slot`, and `Property` decorators. All managers are instantiated in `main.py` and registered as QML context properties.

Key managers:
- `settings_manager.py` — Central settings store, persists to `settingsConfigure.json` in OS-specific config dirs
- `media_manager.py` — Local MP3 playback via QMediaPlayer
- `obd_manager.py` — OBD-II vehicle diagnostics with threaded connection worker
- `obd_adapter_finder.py` — Bluetooth OBD adapter scan / PIN pairing (BlueZ agent over QtDBus) / trust / connect
- `esp32_volume_manager.py` — Wireless rotary encoder volume control over serial
- `berryimu_manager.py` — I2C accelerometer/gyro/magnetometer/barometer sensor
- `gesture_manager.py` — PAJ7620U2 I2C gesture sensor for touchless control
- `audio_analyzer.py` — FFT waveform visualization from audio files
- `android_auto/` — Android Auto via Google Desktop Head Unit (DHU)
- `phone_mirror/` — Phone screen mirroring via the built-in scrcpy-protocol client (`scrcpy_client.py`); talks to `phone_server/` (our fork of the scrcpy server, prebuilt jar in `tools/phone-server/`) over adb, nothing to install

Do not add or change Python managers (see Backend state above).

### Change workflow

1. Implement in C++ (`src/`); QML in `frontend/`.
2. Build: `cmake --build build -j`. Drive the result with `./build/octave --dev --profile` and the dev tooling (screenshots, stories, dashboard drivers, MCP tools) when it touches the UI.
3. Don't touch `backend/`. If a C++ change breaks a Python test in `tests/`, mark that test skipped with a reason pointing at the Python freeze rather than porting the change.
4. Update the wiki (below) and `docs/GAUGE_AUTHORING.md` when the gauge API changes.

### Frontend (`frontend/`)

QML files using Qt Quick 2.15. Entry point is `Main.qml`.

- Top-level QML files are full views/pages (e.g., `MediaPlayer.qml`, `CarMenu.qml`, `OBDMenu.qml`)
- `BottomBar.qml` — Persistent navigation and volume control
- `EnvironmentTheme.qml` — Dynamic theming system (colors adapt to album art)
- `Style.qml` / `Spacing.qml` — Design tokens
- `settings/` — Settings pages and reusable settings UI components (cards, toggles, sliders, etc.)

### Communication Pattern

Backend managers are registered as QML context properties at app startup.

C++ (`src/main.cpp`):
```cpp
engine.rootContext()->setContextProperty("mediaManager", &mediaManager);
```

Python (`main.py`):
```python
engine.rootContext().setContextProperty("mediaManager", media_manager)
```

QML accesses them identically regardless of backend:
```qml
mediaManager.next_track()
```

Custom QML types (for embedded video frames) are registered with `qmlRegisterType` under `OCTAVE.AndroidAuto` and `OCTAVE.PhoneMirror` modules.

### Volume System

Volume uses a **quadratic curve** (`(volume/100)^2.0`) for the UI percent → linear audio mapping. `src/managers/volumecontroller.{h,cpp}` (C++, primary) / `backend/volume_utils.py` (Python, legacy) owns the curve math and the `VolumeController` QObject, which is the single entry point for routing a 0–100 percent to every output (local media, phone mirror, ESP32 LED). All handlers (gesture sensor, ESP32 knob) and QML slider widgets call `volume_controller.applyVolume(percent)` — never touch the individual manager `setVolume` methods or hardcode the curve. Changing the curve only requires editing `to_linear()` / `toLinear()` in one place.

### Threading

Heavy I/O runs on worker threads to avoid blocking the UI:
- OBD connection uses a dedicated `QThread` worker with progress signals
- I²C sensors (BerryIMU, gesture) are read in a loop on their own thread (`QThread` worker in C++, daemon thread in Python; the IMU loop is paced at 200 Hz) and deliver values to the GUI thread by signal; `QTimer`s only drive lifecycle (delayed start, retry back-off). The ESP32 knob uses `QSerialPort::readyRead` in C++ and a blocking-read thread in Python

### Image Providers

A custom `QQuickImageProvider` streams video frames directly to QML for Android Auto (`dhuframe`), avoiding file I/O. Phone mirror decodes H.264 itself (PyAV / libavcodec) and pushes `QVideoFrame`s to the `QVideoSink` of the QML `VideoOutput`.

### Settings Persistence

User settings stored in `settingsConfigure.json` at OS-specific paths:
- Windows: `%APPDATA%/OCTAVE/`
- macOS: `~/Library/Application Support/OCTAVE/`
- Linux: `~/.config/OCTAVE/`

### Logging

Rotating log files in `logs/` subdirectory of the config path:
- Python: `octave.log` — General (5 MB, 3 backups); `octave-error.log` — ERROR+ (2 MB, 5 backups); `octave-debug.log` — Debug mode only via `--debug` flag (10 MB, 2 backups)
- C++: `octave-cpp.log`, `octave-cpp-error.log` (WARNING+ plus crash backtraces), `octave-cpp-debug.log` — same sizes and backup counts (`src/util/logger.cpp`)

### Build & CI

**C++:** CMake + Qt 6 produces native executables per platform (see `BUILD.md`). App store builds use the `OCTAVE_ENABLE_DOWNLOADS=OFF` CMake option to strip out yt-dlp.

**Python:** No build pipeline — Python is a developer-only backend that runs from a venv (`pip install -r requirements.txt && python main.py`). The PyInstaller pipeline was removed; do not reintroduce `build_scripts/build.py`, `octave.spec`, `requirements-build.txt`, or per-platform `build_*.{sh,bat}` shell wrappers. Mobile (Android `.apk` / iOS `.ipa`) always goes through the C++ / Qt for Mobile pipeline.

GitHub Actions (`.github/workflows/build.yml`) runs `lint` (ruff, Python only — keeps the dev backend honest), `test` (headless pytest smoke suite), and `cpp-compile-check` (Linux CMake configure + build of the C++ tree plus a `qmllint` error pass over `frontend/`) on every push and PR to `main` — so a C++ or QML break cannot land unnoticed between release tags. On version tags (`v*`) or manual dispatch, the C++ matrix runs (`cpp-build-windows`, `cpp-build-macos`, `cpp-build-linux`, `cpp-build-linux-arm64`, `cpp-build-android`) and the `release` job attaches every produced artifact (`.exe`, `.dmg`, x86_64 and aarch64 AppImages, `.apk`) to a single GitHub Release. Build jobs depend on lint + test passing.

## Key Conventions

- Qt Quick Controls style is forced to `"Basic"` for full slider customization
- QML import path includes `frontend/` — QML files there are importable by name
- The `dev/` directory is gitignored and fully isolated from production code
- Backend work lands in C++ only (`src/`); the Python backend is frozen at `python-final`
- Python modules use hierarchical loggers via `get_logger(__name__)`; C++ uses `QLoggingCategory`

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

The Java program that runs on the phone during mirroring is OCTAVE's fork of the scrcpy server (Apache-2.0, forked at v3.3.4, package `org.octave.phoneserver`). Both backends push the prebuilt jar `tools/phone-server/octave-phone-server` over adb and speak the scrcpy 3.x protocol to it (`backend/phone_mirror/scrcpy_client.py`, `src/phone_mirror/scrcpyclient.{h,cpp}`). After editing anything under `phone_server/src`, rebuild the jar with `phone_server/build_without_gradle.sh` (see `BUILD.md`) and commit it together with the source — the `phone-server` CI job rebuilds the jar, fails if either jar lacks the `org/octave/phoneserver/Server` class or the version string, and warns when the two hashes differ. The version string in `build_without_gradle.sh` (`SCRCPY_VERSION_NAME`) must equal `SERVER_VERSION` / `kServerVersion` in both clients; the server refuses a mismatch. Protocol byte layouts live in `docs/PHONE_MIRROR_NATIVE_PLAN.md`; when pulling upstream scrcpy changes, re-verify them against `phone_server/src/main/java/org/octave/phoneserver/` (`DesktopConnection`, `control/ControlMessageReader`, `device/Streamer`).

## Gauges & Dashboards

Dashboards are built in layers, each feeding the next:

1. **Data** — `frontend/OBDParameterModel.qml` (singleton) mirrors every `obdManager` value into `paramValues[paramId]` plus per-PID metadata (title / unit / min / max / `kind`) and the vehicle support gate. Widgets never touch `obdManager` directly. Polling is demand-driven: pages register the PIDs they show via `obdManager.setParameterDemand(consumer, ids)`.
2. **Primitives** — `frontend/gauges/` (`CircularGauge`, `ArcGauge`, `BarGauge`, `LinearGauge`, `DigitalReadout`, `SparklineGauge`, `WarningLight`, plus the self-binding `GForceGauge` and `CompassGauge`) and media widgets in `frontend/dashboards/widgets/` (`NowPlayingWidget`, `MediaControlsWidget`). PID-bound primitives share one binding API: set `paramId`; everything else defaults from the PID and the theme and can be overridden per widget.
3. **`WidgetCatalog`** (`frontend/dashboards/WidgetCatalog.qml`, singleton) — the single registry the renderer, editor palette, properties panel, and validation all read: type → url, default span, `supportedKinds`, label/range/color capability flags, curated `editableProps`. A widget not in the catalog can't render or be placed.
4. **JSON specs** — a dashboard is a `"schema": 1` grid spec rendered by `frontend/dashboards/DashboardRenderer.qml` (bad cells are skipped/placeheld, never crash). Built-ins ship read-only in `frontend/dashboards/presets/`; user dashboards live in `<config>/dashboards/*.json` (hot-reloaded).
5. **Persistence + editor** — `DashboardManager` (C++ `src/managers/dashboardmanager.{h,cpp}` ; the frozen Python peer is `backend/dashboard_manager.py`) enumerates, saves, duplicates, deletes, exports, and imports (sanitized, data-only, ids always regenerated). The in-app editor (`frontend/dashboards/editor/`) is pure QML over that API: tap-to-place, drag-to-move, corner-grip resize, PID picker, properties panel.

Entry point: the square "Dashboards" button at the top-right of `OBDMenu.qml` opens the chooser (New / Edit / Copy / Delete / Share / Import); a horizontal swipe cycles Parameter Cards and every dashboard. The chooser's "Primitives Gallery" is a dev showcase with hardcoded values, marked for removal now that the editor exists. Remaining on-device checks: `TODO/dashboards-roadmap.md`; next step (shareable widget presets/composites, code widgets only behind an opt-in): `TODO/user-primitives.md`.

**When the user asks you to build a new gauge or dashboard, read `docs/GAUGE_AUTHORING.md` first.** It is the complete, stand-alone spec: shared binding API, every primitive's props with defaults, theme tokens, angle math for needles, the full list of 129 supported parameter IDs, and step-by-step recipes for adding a new dashboard or primitive. Treat that doc as the source of truth and update it in the same commit whenever you change the gauge API or add/remove a primitive.

## TODO folder

The `TODO/` directory at the repo root holds standalone plans for work that has been **intentionally deferred** — things we know we want to do eventually but aren't committing to right now. Each plan is a single markdown file that can be picked up cold by a future session (yours, mine, or a collaborator's) without needing conversation history.

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

**When the user parks something:** offer to write it up as a TODO file. When you write a TODO file, mention any cross-references to other TODO items so the folder stays internally consistent. When a TODO item is completed, **delete the file** — stale TODOs rot faster than stale code.
