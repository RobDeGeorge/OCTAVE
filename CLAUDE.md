# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OCTAVE (Open-source Cross-platform Telematics for Augmented Vehicle Experience) is a **C++ / Qt 6 / QML** infotainment system designed for vehicles. It runs on Windows, macOS, Linux, Raspberry Pi, iOS, and Android.

### Backend state: C++ is the only shipped binary; Python is a developer/tinkerer backend

OCTAVE intentionally maintains **two parallel backends** — C++ / Qt 6 (`src/`) and Python / PySide6 (`backend/`, `main.py`) — that expose the same API surface to the shared QML frontend (`frontend/`). This is deliberate: Python stays alive for tinkering, accessibility, rapid prototyping, and hardware hacking on Raspberry Pi / single-board setups where a Python REPL is invaluable; C++ is the **sole distribution path** — every shipped binary on every platform (Windows `.exe`, macOS `.dmg`, Linux AppImage / `.deb`, Android `.apk`, iOS `.ipa` once enabled) is built from the C++ tree. Python is not compiled or packaged by CI; tinkerers run it via `python setup.py` or `python main.py` from a checkout.

**Mobile (Android/iOS) is C++-only.** The Python backend is desktop-only (Linux / Windows / macOS / Raspberry Pi). Python-on-Android via buildozer/p4a has been removed — do not reintroduce `backend/platform_config.py`, `backend/stubs.py`, `backend/android_obd_manager.py`, `backend/android_sensors.py`, `deployment/buildozer.spec`, or `requirements-android.txt`. Mobile-specific code lives behind `#ifdef Q_OS_ANDROID` / `#ifdef Q_OS_IOS` in C++ and does **not** need a Python mirror.

**Parity is the rule, not the goal — on desktop.** When you add a feature, fix a bug, or change behavior in one backend, you must do the equivalent work in the other in the same change set for desktop functionality (or explicitly note why parity is deferred and open a `TODO/` entry). The QML frontend must continue to work identically against either backend — no QML file should know or care which backend is running, beyond `typeof someManager !== "undefined"` guards for features legitimately absent on one platform.

**Concrete parity expectations (desktop):**
- Every C++ manager in `src/managers/foo.{h,cpp}` has a Python peer at `backend/foo.py` (and vice versa) for desktop functionality, with the same class name, same QML context property name, same public Slots/Signals/Properties, and equivalent semantics.
- Settings schemas must match: a key introduced in `SettingsManager` (either language) is added to the other in the same commit.
- JSON persistence formats (e.g. settings, dashboards) are shared on disk — both backends must read and write compatible files.
- Volume curves, threading models, logging categories/loggers, image providers, and custom QML types are mirrored.
- When a QML file gains a new binding or call (`fooManager.bar()`), both backends must expose `bar` before the QML ships — *unless* the binding is mobile-only, in which case only the C++ side needs `bar`.

**Historical migration plan:** `.claude/plans/wondrous-toasting-biscuit.md` describes the original Python→C++ rewrite. It is **superseded** by this dual-backend policy — Python is no longer slated for deletion.

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
```

See `BUILD.md` for the full build matrix (9 targets including iOS / Android / Flatpak / app store variants).

### Python build (equal, supported)

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

**Two parallel backends (C++ / Qt 6 in `src/` and Python / PySide6 in `backend/`) + a shared QML frontend** (`frontend/`) communicating via Qt Signals/Slots. Both backends expose the same manager API surface to QML; any work in one must be mirrored in the other.

### Backend — C++ (`src/`, primary for builds & distribution)

Every feature is a C++ **manager class** inheriting from `QObject`. Managers expose state to QML via `Q_PROPERTY`, `Q_INVOKABLE`, and Qt signals. All managers are instantiated in `src/main.cpp` and registered as QML context properties via `engine.rootContext()->setContextProperty(...)`. Build system is CMake; sources are listed explicitly in `CMakeLists.txt`'s `qt_add_executable(octave ...)` call.

Key managers (`src/managers/*.{h,cpp}`):
- `settingsmanager` — Central settings store, persists JSON to OS-specific config dirs
- `mediamanager` — Local audio playback via QMediaPlayer
- `spotifymanager` — Spotify Web API integration
- `obdmanager` + `elm327protocol` — OBD-II diagnostics
- `esp32volumemanager` — Wireless rotary encoder (serial)
- `berryimumanager` — I²C sensor hub
- `gesturemanager` — PAJ7620U2 gesture sensor
- `audioanalyzer` — FFT waveform visualization
- `androidautomanager`, `phonemirrormanager` — Android Auto / phone mirroring (built-in scrcpy-protocol client in `src/phone_mirror/`, no external scrcpy)
- `downloadmanager` — yt-dlp wrapper, compiled out for app-store builds via `OCTAVE_ENABLE_DOWNLOADS` CMake option
- `clock`, `networkmanager`, `volumecontroller` — self-explanatory

When adding a new manager: add `.h`/`.cpp` to `src/managers/`, list both in `CMakeLists.txt`, instantiate in `main.cpp`, register as a QML context property. **Then immediately do the parallel work on the Python side** — see below.

### Backend — Python (`backend/`, first-class peer)

Each feature is a Python **manager class** inheriting from `QObject`. Managers expose state to QML via `Signal`, `Slot`, and `Property` decorators. All managers are instantiated in `main.py` and registered as QML context properties.

Key managers:
- `settings_manager.py` — Central settings store, persists to `settingsConfigure.json` in OS-specific config dirs
- `media_manager.py` — Local MP3 playback via QMediaPlayer
- `spotify_manager.py` — Spotify API integration (spotipy), credentials stored in OS keychain
- `obd_manager.py` — OBD-II vehicle diagnostics with threaded connection worker
- `esp32_volume_manager.py` — Wireless rotary encoder volume control over serial
- `berryimu_manager.py` — I2C accelerometer/gyro/magnetometer/barometer sensor
- `gesture_manager.py` — PAJ7620U2 I2C gesture sensor for touchless control
- `audio_analyzer.py` — FFT waveform visualization from audio files
- `android_auto/` — Android Auto via Google Desktop Head Unit (DHU)
- `phone_mirror/` — Phone screen mirroring via the built-in scrcpy-protocol client (`scrcpy_client.py`); talks to `phone_server/` (our fork of the scrcpy server, prebuilt jar in `tools/phone-server/`) over adb, nothing to install

When adding a new manager: add `foo.py` to `backend/`, import + instantiate it in `main.py`, register as a QML context property with the **same name** the C++ side uses. Mirror every `Q_PROPERTY` / `Q_INVOKABLE` / slot / signal from the C++ header as a PySide6 `Property` / `Slot` / `Signal`. Use `get_app_data_dir()` from `backend/settings_manager.py` for storage paths (matches C++ `SettingsManager::getAppDataDir()`).

### Parity workflow (do this on every change)

Whenever you touch a manager, follow this sequence so the backends don't drift:

1. Decide which backend leads (usually whichever has the higher-fidelity need — C++ for perf, Python for quick iteration).
2. Implement on the leading side.
3. Port to the other backend in the **same change set**, matching:
   - class name (`DashboardManager` ↔ `DashboardManager`),
   - QML context property name (`dashboardManager`),
   - public method names (`loadDashboard`, `saveDashboard`, `deleteDashboard`, …),
   - signal names (`dashboardsChanged`),
   - Property names and types (`QVariantList dashboards` ↔ `Property(list)`),
   - JSON on-disk formats.
4. If QML was changed, grep the QML for the new symbol and confirm both backends define it.
5. Build both: `cmake --build build -j` and `QT_QPA_PLATFORM=offscreen python -c "import main"` (or run `python setup.py --no-run`) to catch import errors.
6. If you genuinely cannot port immediately (e.g. a C++-only library with no Python binding), add a file under `TODO/` with the missing work, gate the QML with `typeof …`, and note the parity debt in the PR.

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

Volume uses a **quadratic curve** (`(volume/100)^2.0`) for the UI percent → linear audio mapping. `src/managers/volumecontroller.{h,cpp}` (C++, primary) / `backend/volume_utils.py` (Python, legacy) owns the curve math and the `VolumeController` QObject, which is the single entry point for routing a 0–100 percent to every output (local media, Spotify, phone mirror, ESP32 LED). All handlers (gesture sensor, ESP32 knob) and QML slider widgets call `volume_controller.applyVolume(percent)` — never touch the individual manager `setVolume` methods or hardcode the curve. Changing the curve only requires editing `to_linear()` / `toLinear()` in one place.

### Threading

Heavy I/O runs on worker threads to avoid blocking the UI:
- OBD connection uses a dedicated `QThread` worker with progress signals
- Spotify API calls use a thread pool (`ThreadPoolExecutor` in Python, `QThreadPool` + `QtConcurrent` in C++)
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
- Backend work lands in **both** C++ (`src/managers/`) and Python (`backend/`) in the same change set — no language is second-class
- Python modules use hierarchical loggers via `get_logger(__name__)`; C++ uses `QLoggingCategory`

## Wiki Maintenance

The project wiki lives in `wiki/` (static HTML pages — `architecture.html`, `development.html`, `media-manager.html`, `spotify-manager.html`, `obd-manager.html`, etc., plus `index.html` as the entry point and `search-index.js` for full-text search).

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

Custom OBD gauges and dashboards live under `frontend/gauges/` (reusable primitives: `CircularGauge`, `BarGauge`, `LinearGauge`, `DigitalReadout`, `ArcGauge`, `SparklineGauge`, `WarningLight`, plus the self-binding `GForceGauge` and `CompassGauge`; media widgets `NowPlayingWidget` / `MediaControlsWidget` live in `frontend/dashboards/widgets/`) and `frontend/dashboards/` (full-screen compositions). Entry point: a square "Dashboards" icon button at the top-right of `OBDMenu.qml` opens a modal chooser popup with scaled live miniatures of every registered dashboard. A secondary "Primitives Gallery" button inside the chooser opens a showcase of every widget with hardcoded demo values — temporary dev screen, see `TODO/dashboards-roadmap.md`.

The long-term plan is a three-phase path from hand-written dashboard QMLs → JSON-defined dashboards + `DashboardManager` (C++) → in-app drag-drop editor ("Tony Hawk create-a-park for dashboards"). Full plan: `TODO/dashboards-roadmap.md`.

**When the user asks you to build a new gauge or dashboard, read `docs/GAUGE_AUTHORING.md` first.** It is the complete, stand-alone spec: shared binding API, every primitive's props with defaults, theme tokens, angle math for needles, the full list of 92 supported parameter IDs, and step-by-step recipes for adding a new dashboard or primitive. Treat that doc as the source of truth and update it in the same commit whenever you change the gauge API or add/remove a primitive.

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
