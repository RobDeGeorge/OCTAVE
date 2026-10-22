# Open backend: a published contract anyone can build a frontend on

**Goal:** make the C++ managers a documented, language-neutral service so that anyone can write an OCTAVE frontend in whatever language or toolkit they like, with the QML UI in `frontend/` becoming the reference client rather than the only one.

**Status:** in progress (audit done 2026-10-21; #1 shift light, #2 dashboard ids and #4 volume moved 2026-10-22)
**Last updated:** 2026-10-22

This absorbed the earlier `backend-service-split.md` plan (2026-10-18), which was about running the managers as a separate process so a UI crash never stops the music. That benefit falls out of `--headless` once the contract exists, so the two plans are one; the process-specific constraints are in "Running the core as its own process" below.

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

Done: #1 shift light (`src/managers/shiftlightmanager.{h,cpp}`, `tests/cpp/tst_shiftlightmanager.cpp`). It is the pattern for the rest: the rules and timers in a manager, typed properties for the settings, QML binds and draws. Decided there: a legacy `{rpm}` flag's high bound is `rpm_max_rpm`. `UiDriver` now resolves context-property managers by name, so `get_prop` / `set_prop` reach them.

Done: #2 dashboard ids (`DashboardManager::uniqueIdForLabel`, `kParameterCardsId` reserved in `existingIds()` and refused by `saveDashboard`; tests in `tst_dashboardmanager.cpp`).

Done: #4 volume (`VolumeController` owns `percent`, `muted`, `effectivelyMuted`, `toPercent()`, `toggleMute()`; MediaManager's own mute and restore logic is gone). Decided with Rob: raising the volume while muted unmutes from every input; the mute toggle at 0 % restores `toPercent(startUpVolume)` (the QML copies used `startUpVolume * 100`, wrong because the setting is linear).

| # | Item | Where today | Target | Effort |
|---|---|---|---|---|
| 3 | **Dashboard spec sanitising** is split three ways: C++ `sanitizeImportedSpec` runs only on import, `DashboardEditor._sanitizeCells` on edit-load, `DashboardRenderer` per cell at render. Overlap is checked nowhere but the editor's drag path (`EditorCanvas.qml:66-81`). `saveDashboard` writes whatever QML hands it. | `dashboardmanager.cpp:735-823`, `DashboardEditor.qml:164-191`, `DashboardRenderer.qml:141-163` | One `DashboardManager::normalizeSpec()` applied on save, load and import, with overlap detection; the grid placement helper (`canPlace`, shrink-to-fit) as a small C++ value type the editor also calls. Renderer keeps a last-line guard for widgets removed from the catalog. | M |
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
| 11 | **Settings the managers never watch.** *Half done 2026-10-22: the IMU and gesture managers now follow `imuEnabled`, `sensorEmitRate` (now typed), `gestureSensorEnabled` and `gestureCooldown` (`tst_sensorsettings`); left: the typed display properties below and SensorMenu binding to them.* `ESP32VolumeManager` subscribes to its settings signals (`esp32volumemanager.cpp:103-111`); `BerryIMUManager` and `GestureManager` read once at startup and rely on the page calling `setEnabled` beside `save_*`. Live bug: the dashboard-layout widget toggle (`settings/widgets/AccessoriesWidget.qml:49-52`) only saves, so the sensor keeps its old state. The saved IMU emit rate is never applied until `SensorMenu` is first opened, and `SensorMenu` reads its unit/decimals settings once but is cached by the page stack. | `AccessoriesSettingsPage.qml:917-923, 1023-1044, 1145-1151`, `SensorMenu.qml:32-75` | `connectSettingsSignals()` in both managers, modelled on ESP32; typed `Q_PROPERTY`s for `sensorDecimalPlaces`, `sensorTempUnit`, `sensorAltitudeUnit`, `sensorShow*`, `obdCardStyleCircular`. QML then only calls `save_*`. | S |
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

## Running the core as its own process

Once `--headless` exists (contract step 2), the QML UI can run as a client of it on desktop and the Pi. The payoff is in-car reliability: a UI crash or freeze no longer stops music, OBD polling or the volume knob; the UI reconnects and the core keeps its state. Constraints that survive from the old plan:

- **Transport for the QML UI.** Qt Remote Objects was the easy option for QML (replicas bind almost unchanged) but it is Qt-only, which is the opposite of the goal. The QML UI should speak the same bridge as everyone else, or stay in-process with the conformance test (step 7) guaranteeing it could. Decide after the bridge exists and the Pi latency is measured; do not build a second transport for one client.
- **Mobile stays single-process.** iOS does not allow a separate background daemon and Android would need a foreground service plus binder or local-socket IPC. Build the managers as a core library with an in-process transport so the Android and iOS binaries link it directly; split the process only on desktop and Pi. This is the `src/main.cpp` refactor: managers constructed and wired in one function a library can own, rather than inline in `main()`. **Effort:** L.
- **Latency-sensitive paths stay in the core.** The ESP32 knob to `VolumeController::applyVolume`, the gesture sensor and the IMU at 200 Hz must never round-trip through a UI. After a split, measure slider-to-volume and knob-to-UI-feedback on the Orange Pi; local IPC should be well under a frame, but verify.
- **Video stays with whoever renders it.** Phone mirror (`src/phone_mirror/`, decoded H.264 into a `QVideoSink`) and Android Auto (`dhuframe` image provider) cannot go through a JSON socket. Keep the decode in the UI process at first: the core manages the session (adb, connection state, control messages) and hands the UI the stream. Shared memory or a dmabuf hand-off on Linux is the later option; re-streaming the H.264 for non-Qt clients is contract step 6.
- **Lifecycle on the Pi.** Run the core as a systemd user unit or under Hyprland's `exec-once` (see `deployment/orange-pi-5-plus/`); the UI reconnects with backoff. On desktop the UI binary can spawn the core if none is running.
- **Road time first.** The Jeep box needs time on v1.0 before the process model changes under it.

## How to do it safely

Never as one large update. Every item above can land as its own small change that leaves the QML UI behaving identically; that is the whole safety story.

- **One rule per change, strangler style.** For each item: add the C++ owner, point the QML at it, delete the QML copy, all in one commit. Never leave two sources of truth across commits; that is exactly how the shift-light copies drifted. Where the QML consumers are many (the value store #8, the NowPlaying facade #5), keep the old QML shape as a thin compatibility layer over the new C++ object, migrate consumers over a few commits, then remove the layer.
- **Write the test before the move.** Each rule gets a QtTest in `tests/cpp/` that pins the current behaviour first (`octave_add_test()`, temp `XDG_CONFIG_HOME` for anything touching settings). That forces the decision a refactor hides: when the copies disagree, which one is right? For the shift light: a legacy flag's high bound is redline, not 99999. Decide it in the test, then move the code to satisfy it. The test is the spec the next frontend author reads.
- **Keep the car on a tag.** The Jeep box runs a tagged release; this work happens on `main`; nothing reaches the Pi except through the release-candidate path (CI dispatch, Dropbox `_OCTAVE_rc` staging, kora VMs, then the Pi). Do not deploy `main` mid-refactor. If a step needs a road test, cut an rc for it.
- **Use the existing tooling as the regression harness.** Run `./build/octave --dev --profile` with the simulated car before and after each move: the screenshot stories (`python -m dev.screenshots`) catch visual regressions, the perf MCP catches what this refactor is most likely to change, idle CPU and redraw rate. The value-store move should show the home-screen redraw dropping; if it does not, something still binds to the whole map.
- **Order by blast radius.** The S items first (#11; #1, #2 and #4 done): each fixes a real bug and touches one page. Then the catalog and value store (#7, #8): every gauge, but mechanical. Then the facades (#5, #6). The bridge is additive and inert without a flag, like the command server today, so it is near zero risk to the car. The process split goes last, after road time, with the single-process binary kept as the fallback.
- **Commit so bisect works.** Pre-1.0 commits are bundled during scaffolding, which is fine, but for this stretch make it one logical move per commit: the first time a drive behaves oddly you will want `git bisect` to name the move.
- **Coordinate parallel sessions.** When another session is editing the same QML pages (the Jeep work touches `CarMenu.qml`, for example), run the moves when it is not mid-edit on those files, and keep the review gate: the auditing session reads the diff before the other commits.
- **Wiki in the same commit.** Each move changes a manager's public surface, so `signals-slots-reference.html` and the manager page move with it (and `docs/GAUGE_AUTHORING.md` for #7). Stale docs are worse than missing docs, and the published schema in contract step 3 will eventually replace the hand-kept page.

## Recommended order of operations

1. Prerequisite #11, second half (typed sensor display settings; #1, #2, #4 and the manager half of #11 are done).
2. #7 and #8 together (the PID catalog and value store; this also fixes the home-screen redraw).
3. #5 and #6 (NowPlaying facade, phone-mirror session state).
4. #3, #9, #10, #12, #13 as they come up; none blocks the bridge.
5. Contract steps 1–5, then 7 and 8.
6. Contract step 6 (media streams) last, after a prototype.

7. The process split on desktop and Pi (section above), after a road test on v1.0.

Prerequisites: v1.0 shipped. Contract step 2 benefits from the core-library refactor but does not require it. `TODO/octave-companion-app.md` item 2 (phone data link) and `TODO/user-primitives.md` (data-only widget presets) should target the same bridge rather than their own sockets. Contract step 6 and the three XL plans in the README share the "pull the H.264 decoder out of `scrcpyclient.cpp`" prerequisite.

Delete this file when contract step 5 lands (a non-Qt reference client works against the published schema) and the process split either has landed or has its own plan.
