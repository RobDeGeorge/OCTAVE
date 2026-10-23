# Open backend: what is left

**Goal:** the C++ managers as a documented, language-neutral service that anyone can write an OCTAVE frontend against, in any language or toolkit, with the QML UI as the reference client rather than the only one.

**Status:** the contract is done (2026-10-23): every rule a frontend needs is in C++, the service serves all of it with pairing, and five frontends in five languages (TypeScript, Python, Go, C, Rust) prove it. Left: small desktop follow-ups, checks that need the Pi, a phone or the Jeep, and the process split (parked until v1.0 has road time).
**Last updated:** 2026-10-23

Rob, 2026-10-21: "I want other individuals to feel empowered to take this backend and create a frontend in whatever style or way they want, using whatever language or tool they want." A side benefit to confirm with someone who does licensing: a frontend in another process talking over a socket is generally not a derivative work, so the backend's GPL-3.0 stays on the backend.

Where it is documented: `docs/api/` (the published API, its `README.md`: versioning, compatibility table, Streams, Pairing), `wiki/writing-a-frontend.html` (the walkthrough), `wiki/utilities.html#service-protocol` (how the server works), `frontends/README.md` and `frontends/PARITY.md` (the frontends and what each can do).

---

## Done

- **2026-10-21:** the audit of all 105 QML files for logic a second frontend would have to re-implement.
- **2026-10-22:** the thirteen rule moves out of QML (shift light, dashboard ids and spec rules, volume, settings the managers never watched, the PID catalog, the value store `paramStore`, `nowPlaying`, phone-mirror recovery, the analyzer and trip pollers, JSON strings, smaller duplicates, platform and persistence work). Contract steps 1-5, 7 and 8: the service in every build (`hello` / `describe` / `get` / `set` / `call` / `subscribe` / `rows`), `--headless` on `AppCore`, `docs/api/` generated and checked by `tst_apidocs`, `protocolVersion`, `sdk/python` and `sdk/web`, `tst_conformance`, the wiki.
- **2026-10-23 (overnight run, lanes A-D):**
  - Backend: `--headless --dev` (the simulated car with no UI), `--web-root` / `serviceWebRoot`, demand released when a client's connection closes, `clock.time`, `GET /art` (current, `?song=`, `?queue=`) with `nowPlaying.artRevision`, `audioAnalyzer.setDemand` for the levels, the widget catalog and the theme tables as data (`dashboardManager.widgetCatalog`, `themeCatalog` with `activeTheme`), `settingsManager.apply_settings` and the Now Playing presets in C++, `mediaManager.browse_music_folders`, `GET /export/trip|dashboard|logs`, freeze-frame values and plain-language DTC descriptions, `octave --help`, fixes from the frontend lanes (QVariant returns, `close()` state, subscribe ordering, `saveDashboard` without an id, `lastError`, `hello` with a rejected token).
  - Contract step 6, media streams: the `stream` op sends the phone's H.264 untouched (tapped before the in-app decoder) and Android Auto's frames as JPEG as binary records, with config events, keyframe joins and drop-to-keyframe; a dev-only `sim_video` source; checked end to end with WebCodecs in Chromium and once against a real phone over USB.
  - Protocol 2, pairing: loopback stays open by default; everything else pairs with a 6-digit code from Settings > Device > Service, gets a token (stored hashed), and uses short-lived media tickets in URLs; revoking closes the device's connections. `frontends/retro-cluster` pairs too.
  - SDKs: `sdk/python` (stdlib only; pairing, stream records), `sdk/web` (the reference page, pairing form, media tickets), `sdk/typescript` (lane B: typed, the types generated from `docs/api/` and checked in CI).
  - A security review of the service and its fixes: the line protocol only from `{` and HTTP GET only (a browser POST could run an op), `call` / `get` / `set` / `subscribe` limited to what `describe` lists, connection and line limits, write backpressure, an address required in every HTTP `Host`, and a batch of smaller hardening.
  - Frontends (lanes B and C): `frontends/glass` (TypeScript + Svelte, web), `console` (Python + Textual), `sunlight` (Go + Gio), `arcade` (C + SDL2) and `retro-cluster` (Rust + egui), with `frontends/PARITY.md` mapping every QML feature to them.
  - A desktop load baseline (below) and the CMake plan for the core library (below), neither of which needs the car.

## Left: small follow-ups (desktop)

- **Leftovers noted during the rule moves:** Trip History shows the recorded units, not the gauge units (convert its series through `toDisplay` if wanted); the Sensors page keeps its own `sensorTempUnit` (default °F), so the IMU temperature can read °F there and °C on a dashboard; the persisted `supportedOBDParameters` holds the raw scan without aliases while `vehicleSupportedParameters` has them; `OBDManager::obdParameterChanged` and the 84 typed per-PID signals have no listener left; the gesture worker's flag table is a second spelling of the gesture names; a file QAudioDecoder rejects waits out the 30 s watchdog instead of failing at the error. Each **Effort:** S.
- **`tst_conformance` gap:** a QML alias (`property var sm: settingsManager`) escapes it; none exist today.

## Left: checks that need hardware

Nothing here can be done on the desktop. Each was noted when its change landed.

| Where | Check |
|---|---|
| Orange Pi | Idle CPU and redraw rate against v0.9.5 with `--profile` (the value store); waveform CPU with local and phone playback; the Sensors home on a box without qt6-quick3d; the IMU rate with page demand; window size restored after a resize and a restart (also Windows) |
| Orange Pi | The service load script (`tools/stream/service_load.py`) against the baseline below, with the real phone stream instead of the 300 kbit/s test file |
| Orange Pi phone rig | Phone-mirror recovery: a USB pull mid-stream (badge after 2 s, back within the grace, then one past it), unplug on the error page and replug, leaving the page while reconnecting and returning after >10 s, an unauthorized prompt accepted late, a Wi-Fi drop with the stale-serial retry |
| A phone | Media Room's phone-side track animation and the phone-art theme in a real session; a pairing from a phone's browser on the car's network |
| Android | An SD-card music folder from the picker (if `/storage/<volume>` isn't readable under All Files Access, persist the tree grant and scan through the content URI: a bigger change, its own TODO row); the picker returning the externalstorage provider for internal and SD trees; `QDir` listing `assets:/frontend/assets/fonts`; the BLE connect states |
| Any device | The dashboard editor's drag feel with `canPlace` as a C++ call per frame; one real download through the new queue model |

## Service load baseline (desktop, 2026-10-23)

To compare on the Orange Pi. One `octave --headless --dev` (Debug build of night/backend with the wake pipe, AMD Ryzen 7 9700X, Arch Linux), the simulated car, a local track playing; the script is `tools/stream/service_load.py`.

| | Backend CPU (of one core) | Backend RSS |
|---|---|---|
| 60 s, no clients | 0.5 % | 400 MB |
| 120 s, 5 clients (below) | 1.1 % | 414-418 MB |

Clients: `paramStore` every one of the 129 ids with `setParameterDemand` on all, `throttleMs` 40 (15.9 events/s); `nowPlaying` six properties (20.1/s); `audioAnalyzer.levels` with `setDemand` (3.5/s, only on change); `sim_video` over WebSocket and over TCP (30.3 records/s each, no drops). Event latency, as the excess over the fastest delivery seen (the server's `t` is its own monotonic clock, in whole ms): p50 0.5 ms, p95 1.0 ms, max 1.1 ms on every subscription. Measured the same way before the wake pipe (the 20 ms read poll alone): 0.7 % / 1.2-1.4 % CPU, p50 10 ms, max 21 ms.
Not covered: the test video is 300 kbit/s where a phone sends 8-16 Mbit/s; the RSS is a Debug build's; Windows keeps the 20 ms poll (no wake pipe there).

## Left: running the core as its own process

The payoff is in-car reliability: a UI crash or freeze no longer stops music, OBD polling or the volume knob; the UI reconnects and the core keeps its state. **Parked** until v1.0 has road time on the Jeep, and with the single-process binary kept as the fallback.

Constraints:

- **Transport for the QML UI.** The QML UI speaks the same service as everyone else, or stays in-process with `tst_conformance` guaranteeing it could. No second transport (Qt Remote Objects) for one client. Decide after the Pi latency is measured.
- **Mobile stays single-process.** iOS allows no background daemon; Android would need a foreground service plus IPC. Android and iOS link the core library directly; the process splits only on desktop and the Pi.
- **Latency-sensitive paths stay in the core.** The ESP32 knob to `VolumeController::applyVolume`, the gesture sensor and the 200 Hz IMU never round-trip through a UI. After a split, measure slider-to-volume and knob-to-UI-feedback on the Pi.
- **Video stays with whoever renders it.** The QML UI keeps decoding in its own process at first; other clients use the `stream` op. Shared memory or a dmabuf hand-off on Linux is the later option.
- **Lifecycle on the Pi.** The core as a systemd user unit or Hyprland `exec-once` (`deployment/orange-pi-5-plus/`); the UI reconnects with backoff. On desktop the UI can spawn the core if none is running.

**The core library: CMake plan** (written 2026-10-23, not started: only the Linux desktop build can be checked locally, and the move touches every platform's link). Today `qt_add_executable(octave ...)` lists every source and each test re-lists the ones it needs.

1. *Library with the same sources, the app links it, nothing else changes.* `add_library(octave_core STATIC ...)` with everything under `src/` except `main.cpp`, `src/dev/` and QML-engine-only code; AUTOMOC on it. As **PUBLIC** definitions, so the app and the library see identical headers: `OCTAVE_VERSION`, `OCTAVE_ENABLE_DOWNLOADS` (it changes `downloadmanager.h`, `androidmediabridge.h`, `androidprocessadapter.h`), `OCTAVE_HAVE_FFMPEG`, `OCTAVE_BLUEZ`, `Q_OS_MOBILE` (Android, wasm), `OCTAVE_WASM`; a mismatch is an ODR bug, not a build error. Also onto the library: the WebAssembly source swaps (`src/platform/wasm/*` and the stand-in `QSerialPort` include dir), SerialPort / Bluetooth / DBus / Multimedia / Network / Qml / Quick (AndroidAutoManager's image provider), sqlite, TagLib and FFmpeg per platform, Android's Sensors / CorePrivate. Kept on the app: `main.cpp`, every `qt_add_resources` (the phone-server jar `ScrcpyClient` reads is a resource: add it to each executable that links the core, or `Q_INIT_RESOURCE` it, since a static library's resources are otherwise dropped by the linker), Quick3D, WebEngine / WebView, `src/dev/`. Android's `qt_add_executable` makes a shared library and iOS links statically; both take a static core. Verify: Linux build and ctest, `packaging/wasm/build.sh`, then a CI dispatch of `build.yml` (Windows, macOS, Linux x86 / arm64, Android) before merging. **Effort:** M.
2. *Tests link `octave_core`* instead of re-listing sources (most `SOURCES` go from `octave_add_test`; a big cut in test build time). Mechanical, one commit. **Effort:** S.
3. *`octave-core`*: a second, desktop and Pi only executable whose `main` is today's `runHeadless`, plus the Pi's systemd user unit. **Effort:** S.
4. *The QML UI as a client* (`octave --connect HOST:PORT`): after 1-3 and the Pi latency numbers. **Effort:** L.

## How to do the rest safely

- **Keep the car on a tag.** The Jeep runs a tagged release; nothing reaches the Pi except through the release-candidate path (CI dispatch, Dropbox `_OCTAVE_rc` staging, kora VMs, then the Pi). If a step needs a road test, cut an rc for it.
- **One logical change per commit**, test first (`tests/cpp/`, a temp `XDG_CONFIG_HOME` for anything touching settings), wiki and `docs/api/` in the same commit, so `git bisect` can name a move.
- **The regression harness:** `./build/octave --dev --profile` with the simulated car before and after, the screenshot stories, the perf MCP; `tools/stream/service_load.py` for the service; `frontends/check_all.sh` (one throwaway backend, every frontend's own checks) after any change to the service or `docs/api/`.

## Recommended order of operations

1. The hardware checks above, as the devices are at hand.
2. After v1.0 has road time: CMake plan step 1 with a CI dispatch for every platform, then steps 2 and 3, then the Pi unit and a road test.
3. Step 4 (`--connect`) only if the measurements say the split is worth it.

Related: `TODO/octave-companion-app.md` item 2 (phone data link) and `TODO/user-primitives.md` (data-only widget presets) should use this service rather than their own sockets. The three XL plans in `TODO/README.md` share the "pull the H.264 decoder out of `scrcpyclient.cpp`" prerequisite.

Delete this file when the process split has landed or has its own plan, and the hardware checks are done or moved to the device TODOs.
