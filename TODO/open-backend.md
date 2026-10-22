# Open backend: a published contract anyone can build a frontend on

**Goal:** make the C++ managers a documented, language-neutral service so that anyone can write an OCTAVE frontend in whatever language or toolkit they like, with the QML UI in `frontend/` becoming the reference client rather than the only one.

**Status:** parked (audit done 2026-10-21, no code moved yet)
**Last updated:** 2026-10-21

Companion to [backend-service-split.md](backend-service-split.md), which is about running the managers as a separate process so a UI crash never stops the music. This file is about the **contract**: what the backend has to own and publish before an outside author can build against it. Do this one first; the process split then becomes a transport detail.

---

## Why this is the goal

Rob, 2026-10-21: "I want other individuals to feel empowered to take this backend and create a frontend in whatever style or way they want, using whatever language or tool they want." Not a second in-house UI; a backend that is genuinely reusable.

That rules out the in-process options (another Qt view, QtRO replicas) as the primary path. The boundary has to be a protocol an author can speak from Python, JavaScript, Rust, Go or a shell script, and the backend has to answer the questions a UI asks, not just forward raw sensor signals.

A side benefit worth confirming with someone who does licensing: a frontend in a separate process talking over a socket is generally not a derivative work, so the GPL-3.0 on the backend (chosen deliberately, see `LICENSE`) stays on the backend and does not push itself onto third-party UIs.

## Why it's parked

- v1.0 ships first. Every prerequisite below also helps the single-process app, so they can be picked up one at a time between releases without committing to the whole plan.
- The Jeep box needs road time on v1.0 before the process model changes under it (same caveat as the service-split plan).

## What already exists

- Every manager is a `QObject` with `Q_PROPERTY` / `Q_INVOKABLE` / signals, registered in `src/main.cpp`. Qt's meta-object system can enumerate all of it, so the API surface can be published and served generically rather than hand-written per manager.
- The dev command server (`src/dev/commandserver.{h,cpp}`, JSON lines on `127.0.0.1:9876`, `--profile` only) already has `invokeByName()` (call any slot by name with converted args), `jsonSafe()` (QVariant → JSON), and `get_prop` / `set_prop` / `list_*` actions in `src/dev/coreactions.cpp`. Its header says it is meant to grow into the service API.
- `wiki/signals-slots-reference.html` documents the manager contract by hand.
- The gitignored `dev/` tree holds Python clients (MCP servers, screenshot stories, perf loops) that drive the app over that socket. That is the seed of the SDK, and it is not in the repo.

## Prerequisite: move the rules out of QML

The 2026-10-21 audit read all 105 QML files for logic a second frontend would have to re-implement to work at all. Every item below was verified in the code. Each is a rule a third-party author would least expect to be their problem, and none is covered by `tests/cpp/`. Efforts use the README scale; all are desktop work unless noted.

### Rule engines that exist only in QML (do these first)

| # | Item | Where today | Target | Effort |
|---|---|---|---|---|
| 1 | **Shift light.** Parse `rpm_flags` JSON, migrate legacy `{rpm}` flags, find the band containing the RPM, run the flash timer. Three copies that disagree: `Main.qml` and `HomeOBDView.qml` default a legacy flag's high bound to 99999, `OBDMenu.qml` migrates it to redline. Band match runs twice per RPM sample on the GUI thread. | `frontend/Main.qml:60-90, 1297-1332`, `HomeOBDView.qml:98-134`, `OBDMenu.qml:1555-1583` | New `src/managers/shiftlightmanager.{h,cpp}` fed by `OBDManager::rpmChanged`: `flags` (QVariantList, migrated once, persisted structured), `activeFlag`, `flashOn` (C++ QTimer), `enabled`, `fullScreenFlashOpacity`. QML overlays just bind. QtTest the band match and migration. | S |
| 2 | **Dashboard id generation.** `_uniqueId` mirrors `DashboardManager::uniqueUserIdFromLabel` and has drifted: C++ reserves hidden built-in ids (`existingIds()`, `dashboardmanager.cpp:100`), QML reserves the Parameter Cards sentinel `"grid"`, neither does both. Delete built-in "Sport" then create "Sport": save fails. Duplicate as "Grid": unreachable entry. | `frontend/dashboards/editor/DashboardEditor.qml:343-360` | Expose `Q_INVOKABLE QString uniqueIdForLabel(label)` with `"grid"` reserved in C++; delete the QML copy. | S |
| 3 | **Dashboard spec sanitising** is split three ways: C++ `sanitizeImportedSpec` runs only on import, `DashboardEditor._sanitizeCells` on edit-load, `DashboardRenderer` per cell at render. Overlap is checked nowhere but the editor's drag path (`EditorCanvas.qml:66-81`). `saveDashboard` writes whatever QML hands it. | `dashboardmanager.cpp:735-823`, `DashboardEditor.qml:164-191`, `DashboardRenderer.qml:141-163` | One `DashboardManager::normalizeSpec()` applied on save, load and import, with overlap detection; the grid placement helper (`canPlace`, shrink-to-fit) as a small C++ value type the editor also calls. Renderer keeps a last-line guard for widgets removed from the catalog. | M |
| 4 | **Volume rules.** The curve's inverse is hand-written as `Math.round(Math.sqrt(volume) * 100)` in six places; the "volume 0 → restore startup volume" and "raise while muted → unmute" rules are triplicated with different behaviour; `BottomBar.qml:843` calls `mediaManager.setVolume` directly. | `BottomBar.qml:1475, 2514`, `MediaRoom.qml:586, 617, 1743`, `settings/MediaSettingsPage.qml:167` | `VolumeController::toPercent()`, `toggleMute()`, `Q_PROPERTY(bool effectivelyMuted)`. Sliders bind to `settingsManager.currentVolume` and stop listening to `mediaManager.volumeChanged`. | S |
| 5 | **Which media source is live** (phone session vs local) is re-derived in seven files, each with its own play-state sync in separate `Connections` blocks; phone position is polled by 500 ms timers in two of them. | `BottomBar.qml:21-55`, `MediaRoom.qml:96-122, 162-172`, `MediaPlayer.qml:219-225`, `MainMenu.qml:29-72, 66-72`, `Main.qml:273-284`, `dashboards/widgets/NowPlayingWidget.qml:39-89`, `MediaControlsWidget.qml:207-238` | A `NowPlaying` facade (`src/managers/nowplaying.{h,cpp}`): `source`, `title`, `artist`, `album`, `artUrl`, `durationMs`, `positionMs` (notifying; the facade owns the phone extrapolation tick), `playing`, plus `togglePlay()`, `next()`, `previous()`, `seek()`, `toggleShuffle()`. Same pattern `VolumeController` set for volume. `PhoneMirrorManager` gets a notifying `mediaPosition` and a distinct track-changed signal. | M |
| 6 | **Phone-mirror auto-reconnect** keys on a regex over error prose (`/disconnected|No Android device|not authorized|offline|USB permissions/i`) and two polling timers with a 10 s grace window. `phonemirrormanager.cpp:476` explicitly preserves the wording for it. | `frontend/PhoneMirrorView.qml:26-62, 662-788` | `PhoneMirrorManager`: `Q_PROPERTY(QString sessionState)` (`idle | starting | streaming | reconnecting | waitingForPhone | failed`), `lastErrorKind` (`device | setup | fatal`), `scrcpyError(kind, text)`; grace window and device polling as one QTimer in the manager, gated by `setViewActive(bool)`. | M |

### Tables duplicated between QML and C++

| # | Item | Where today | Target | Effort |
|---|---|---|---|---|
| 7 | **PID catalog.** 129-entry table of id/title/unit/min/max/kind plus 84 hand-written `on<Signal>Changed` handlers mapping to parameter ids. C++ already has `PidEntry` (names only) and emits the generic `parameterSampled(id, value)` for every reading (`obdmanager.cpp:455-464`), so the handler block is redundant now. 20 titles already differ between the tables. Unit/range/kind exist only in QML, which is why `TripStore` writes CSV with bare ids and no units. Every gauge linear-scans the table for its PID. | `frontend/OBDParameterModel.qml:19-174, 177-208, 417-520` | Extend `PidEntry` / `ExtendedSignal` (or a `ParameterCatalog` in `elm327protocol`) with unit, min, max, kind, decimals, shortTitle; expose `Q_PROPERTY(QVariantList allParameters CONSTANT)` and `Q_INVOKABLE QVariantMap parameterInfo(id)` on `OBDManager` (hash lookup). Keep the eight IMU ids in the same table. Also publish it as a JSON resource so non-Qt clients read the same file. Update `docs/GAUGE_AUTHORING.md`. | M |
| 8 | **Live value store.** `paramValues` is replaced wholesale every 33 ms and every binding that reads it re-evaluates whichever PID changed; the `_holders` side-channel exists only because QML has no per-key notify. `HomeOBDView` does not use the holders, which is the home-screen redraw noted in memory. | `OBDParameterModel.qml:210-274` | A C++ per-parameter value store (`QQmlPropertyMap` subclass or per-id value objects) fed from `parameterSampled` and the IMU signals; `paramValues` stays as a compatibility map. `tests/cpp/tst_vehiclegate.cpp` already drives the singleton with a fake `obdManager`. | M |
| 9 | **JSON strings parsed in QML.** `obdSavedAdapters` (three parse sites), custom themes (`get_custom_theme` per theme, twice), `albumArtColors`, download search results and per-download status patches, the ESP32 port list (`get_ports_with_descriptions` while a `QVariantList availablePorts` already exists). The download page rebuilds the whole queue and progress map from signals although `DownloadManager` keeps `m_downloadTasks` and nobody calls `get_download_queue()`. | `OBDAdapterSheet.qml:36`, `settings/OBDSettingsPage.qml:78, 896`, `Main.qml:124-132, 190-206`, `Style.qml:991-1028`, `DownloadPage.qml:107-222`, `settings/AccessoriesSettingsPage.qml:22-31` | Typed `QVariantList` / `QVariantMap` properties; two `QAbstractListModel`s on `DownloadManager` (search results, queue); delete the string getters. | M |
| 10 | **Smaller duplicates.** Gesture names and action list (`AccessoriesSettingsPage.qml:36-46`, `gesturemanager.cpp:147`, `settingsmanager.cpp:294`); IMU id list (`Main.qml:971` vs `obdmanager.cpp:781`); connection-log ring buffer capped at 40 in `OBDHome.qml:49-51` and 60 in `OBDSettingsPage.qml:34-36`; OBD status classified by `indexOf("Connecting")` in `OBDSettingsPage.qml:54-60` and `widgets/OBDWidget.qml:384-390` with an 18-entry colour table keyed on exact strings; time formatting four ways (`MainMenu.qml:107`, `MediaRoom.qml:261`, `MusicDeviceScene.qml:37`, `DownloadPage.qml:958`) while `MediaManager::_format_duration` goes unused; heading-to-cardinal three times; mph→km/h in `CarMenu.qml:69`, °F and feet in `SensorMenu.qml:52-59`. | as listed | `GestureManager` exposes names/actions; `BerryIMUManager` takes consumer demand like OBD; `Q_PROPERTY(QStringList connectionLog)` on `OBDManager`; `Q_ENUM ConnectionState` on `OBDManager`; a `src/util/units.{h,cpp}` singleton (`cardinal`, `kphFromMph`, `fahrenheit`, `feet`, `mmss`) and a unit preference the catalog in #7 applies. | S each |

### Managers that leave their own job to the page

| # | Item | Where today | Target | Effort |
|---|---|---|---|---|
| 11 | **Settings the managers never watch.** `ESP32VolumeManager` subscribes to its settings signals (`esp32volumemanager.cpp:103-111`); `BerryIMUManager` and `GestureManager` read once at startup and rely on the page calling `setEnabled` beside `save_*`. Live bug: the dashboard-layout widget toggle (`settings/widgets/AccessoriesWidget.qml:49-52`) only saves, so the sensor keeps its old state. The saved IMU emit rate is never applied until `SensorMenu` is first opened, and `SensorMenu` reads its unit/decimals settings once but is cached by the page stack. | `AccessoriesSettingsPage.qml:917-923, 1023-1044, 1145-1151`, `SensorMenu.qml:32-75` | `connectSettingsSignals()` in both managers, modelled on ESP32; typed `Q_PROPERTY`s for `sensorDecimalPlaces`, `sensorTempUnit`, `sensorAltitudeUnit`, `sensorShow*`, `obdCardStyleCircular`. QML then only calls `save_*`. | S |
| 12 | **Pollers in place of signals.** Waveform visualizer: a 16 ms QML timer calls `get_position` + `update_position` 60×/s then loops 96 repeater items per FFT frame, because `AudioAnalyzer` is not wired to `MediaManager` in `main.cpp` (only the phone PCM feed is). Trip History polls the live trip every 5 s although `TripRecorder` emits `currentSampleCountChanged`. `SensorHome.qml:68` answers "is Quick3D available" by synchronously compiling `CarMenu.qml`. | `MediaRoom.qml:1311-1357, 1387-1409`, `TripHistory.qml:168-179`, `SensorHome.qml:66-70` | Connect analyzer to media in C++, expose levels as a property; `currentTripElapsedS` + throttled `liveSummaryChanged` on `TripRecorder`; `hasQuick3D` context property from `main.cpp`. | S–M |
| 13 | **Platform and persistence work in pages.** Android `content://` tree URIs and Windows drive letters decoded by hand in `settings/MediaSettingsPage.qml:52-82` while `dashboardmanager.cpp:498` has its own file-URL conversion; linear↔dB for phone audio gain in `AccessoriesSettingsPage.qml:2042-2053, 2111-2122`; screen size saved on every resize from both `Main.qml:1283-1295` and `DisplaySettingsPage.qml:196-269` with range limits only in a QML validator; album-art extraction triggered from `Main.qml:275-284` although `MediaManager::_on_theme_changed` already reacts to the same signal (double decode); `Main.qml:1236-1266 updateTheme()` has no callers; font discovery via `FolderListModel` + dynamic `FontLoader`s (`Main.qml:1101-1226`); home-grid add/remove/move and the cap of 8 in `OBDSettingsPage.qml:1159-1172, 1503-1537` duplicating `settingsmanager.cpp:1413`. | as listed | `SettingsManager::save_media_folder_url(QUrl)`; dB properties; clamp in `save_screen_*`; delete the QML extraction and dead function; `QFontDatabase` at startup; `add/remove/move_home_obd_parameter` slots. | S each |

### Data that is QML today but is really a resource

A second frontend needs the same facts, so these become JSON files both sides read, served by the backend: the PID catalog (#7), `frontend/dashboards/WidgetCatalog.qml` (keep the QML singleton for widget URLs, load the rest from the file so `DashboardManager` can validate against it), the theme tables in `Style.qml`, the Now Playing presets (`NowPlayingStudio.qml:151-197`, `MediaSettingsPage.qml:359-405`: fourteen individual `save_*` calls per preset; make it `SettingsManager::apply_settings(QVariantMap)` with the presets in C++), and the dashboard presets already in `frontend/dashboards/presets/`. The dashboard spec schema needs a written description so other frontends can import and export the same files.

### Deliberately not moving

Gauge geometry, needle and tick math, the album-art carousel, page cache and prewarm pacing, Trip History chart math and gestures, the Jeep 3D rig and steering solver (`JeepSteering.js`, `Rig.js`: pure animation, no live steering input exists), theme tokens and behaviours, the QR pairing canvas (payload and modules are already built in C++). A new frontend rewrites all of that by definition. Roughly three quarters of the 42k QML lines are this pile.

## The contract itself

Once the prerequisites are in, the backend is property-and-slot shaped end to end and the bridge can be generic.

1. **Generic bridge.** Grow `src/dev/commandserver` into `src/service/`: for each registered manager, serve `get` / `set` for every `Q_PROPERTY`, `call` for every `Q_INVOKABLE` and public slot, and a `subscribe` that streams every signal and property change as JSON events. `invokeByName()` and `jsonSafe()` already do most of the conversion. Keep the current `action` vocabulary working so the `dev/` clients and MCP servers do not break. Move it out of the `OCTAVE_DEV_TOOLS` build option. **Effort:** M, desktop.
2. **`octave --headless`.** Managers up, no QML engine, bridge listening. On the Pi it runs as a user unit (see `deployment/orange-pi-5-plus/`). **Effort:** S once step 1 exists; Pi for the road test.
3. **Published schema.** Generate `docs/api/` from the meta-objects at build time (one file per manager: properties with types and notify signals, methods with parameter types, signals), plus the PID catalog JSON and the dashboard spec schema. This is what an author reads before writing a line. Replace the hand-kept `wiki/signals-slots-reference.html` with a page built from it. **Effort:** S–M.
4. **Versioning.** A `protocolVersion` the bridge reports on connect, bumped on any breaking change, with a short compatibility note in the schema. Outside authors will pin to it. **Effort:** S.
5. **SDK in the repo.** Bring the gitignored `dev/` Python clients into `sdk/python/` (the parts that are not machine-specific), and add a second reference client in another language: a browser page showing a few gauges and media controls is the obvious one, because it proves the contract works with no Qt at all. **Effort:** M.
6. **Media streams.** The only parts that are not property-shaped: Android Auto frames (`dhuframe` image provider), phone-mirror H.264 (`src/phone_mirror/scrcpyclient.cpp` to a `QVideoSink`), the analyzer's PCM/levels. Pass the H.264 through on a second local socket rather than decoding server-side; levels as a small binary frame. Prototype this before promising it in the schema; it is the expensive, uncertain piece. **Effort:** L. Shares the "pull the decoder out of `scrcpyclient.cpp`" prerequisite noted at the bottom of `TODO/README.md`.
7. **Conformance test.** Keep the QML UI in-process for latency, but add a test that everything QML touches is reachable through the bridge, so the reference UI cannot quietly use a private path. **Effort:** M.
8. **Wiki and CLAUDE.md.** `architecture.html`, `development.html`, `building.html`, the generated reference page, and a new "Writing a frontend" page. **Effort:** S.

## Recommended order of operations

1. Prerequisites #1, #2, #4, #11 (all S, each fixes a bug users can hit today).
2. #7 and #8 together (the PID catalog and value store; this also fixes the home-screen redraw).
3. #5 and #6 (NowPlaying facade, phone-mirror session state).
4. #3, #9, #10, #12, #13 as they come up; none blocks the bridge.
5. Contract steps 1–5, then 7 and 8.
6. Contract step 6 (media streams) last, after a prototype.

Prerequisites: v1.0 shipped. Step 2 of the contract benefits from the core-library refactor in [backend-service-split.md](backend-service-split.md) step 2 but does not require it. `TODO/octave-companion-app.md` item 2 (phone data link) and `TODO/user-primitives.md` (data-only widget presets) should target the same bridge rather than their own sockets.

Delete this file when contract step 5 lands (a non-Qt reference client works against the published schema); move anything left into [backend-service-split.md](backend-service-split.md).
