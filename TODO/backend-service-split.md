# Run the backend as a headless service so any frontend can connect

**Status:** deferred: an idea scoped on 2026-10-17, after v1.0 and after `TODO/python-removal.md` (Python frozen 2026-10-18). No work started.
**Last updated:** 2026-10-17

## What it is

Today OCTAVE is one process. The C++ managers (`src/managers/`) are registered as QML context properties in `src/main.cpp`, and the QML in `frontend/` binds straight to their `Q_PROPERTY`s and calls their `Q_INVOKABLE`s. The idea is to split that into:

- **`octave-core`**: a headless service that owns every manager (media, OBD, sensors, ESP32 knob, volume, settings, dashboards, downloads, phone mirror control). It exposes them over an IPC API.
- **Frontends**: separate processes that connect to the core. The existing QML UI becomes one client. **Non-QML frontends** (web UI, Slint/Rust, Flutter, a native phone app, a terminal UI) become possible because the API doesn't depend on Qt.

## Why it's worth doing

- **In-car reliability** (see the field-use memory: the box is daily-driven). A UI crash or freeze no longer stops music, OBD polling or the volume knob. The UI reconnects, and the core keeps its state.
- **Non-QML frontends.** For example, a browser dashboard of OBD gauges on a second screen, or a phone remote for media. `TODO/octave-companion-app.md`'s phone data link (item 2: notifications, now-playing, GPS) could share this API instead of defining its own socket.
- **Headless testing.** Drive and assert on the core through the API without rendering anything.

## Why it's parked

- **v1.0 comes first.** Splitting the process is a big structural change, and doing it during the parity era would double it (C++ and Python).
- **Starting point:** the C++ command server exists since 2026-10-18 (`src/dev/commandserver.cpp`, JSON lines on `localhost:9876`, `--profile` only, compiled in with `OCTAVE_DEV_TOOLS`). Grow the service API from it rather than starting over; it would need to leave the dev-only build option first.
- The car needs road time on v1.0 before the process model changes under it.

## Design decisions to make

### The IPC protocol

| Option | QML client | Non-Qt clients | Notes |
|---|---|---|---|
| Qt Remote Objects (QtRO) | Easiest: replicas bind in QML almost unchanged | Practically Qt-only | Least work for the existing UI |
| WebSocket + JSON | Needs a QML-side proxy layer | Any language, including browsers | Define property snapshots, change events and method calls ourselves |
| gRPC | Needs a proxy layer | Any language with codegen | Heavier, needs a schema, weaker in the browser |

**Likely answer: a hybrid.** QtRO for the main QML UI, plus a WebSocket/JSON gateway in the core for everything else. Grow the gateway out of the command-server protocol so the dev and MCP tooling keeps working.

The API surface should be the manager contract that already exists (properties, slots, signals per manager, documented in `wiki/signals-slots-reference.html`). Generate the WebSocket schema from it where possible, so the two don't drift.

### Video frames

Phone mirror (`src/phone_mirror/`, decoded H.264 pushed into a `QVideoSink`) and Android Auto (`dhuframe` image provider) can't go through a JSON socket cheaply. Options:
- Keep the decode in the UI process. The core only manages the session (adb, connection state, control messages).
- Pass decoded frames through shared memory or a dmabuf hand-off (Linux/Pi).
- For non-Qt clients, re-stream the H.264 (or WebRTC) and let them decode it.

Start with the first option: it's simplest and doesn't regress the QML UI.

### Latency-sensitive paths

The ESP32 knob → `VolumeController::applyVolume`, the gesture sensor and the IMU (200 Hz) should stay inside the core and never round-trip through the UI. Measure the slider → volume and knob → UI feedback loops after the split. Local IPC should be under 1 ms, but verify on the Orange Pi.

### Mobile

iOS doesn't allow a separate background daemon like this, and Android would need a foreground service plus binder/local-socket IPC. **Keep mobile as a single process.** Build the core and the QML client as libraries that can link into one binary (an in-process transport), and split them only on desktop and Pi.

### Lifecycle

On the Pi, run the core as a systemd user unit (or under Hyprland's exec-once, see `deployment/orange-pi-5-plus/`), and have the UI reconnect with backoff. On desktop, the UI binary can spawn the core if it isn't already running.

## Move logic out of QML before any non-QML frontend

Some logic that acts like backend code lives in QML today. A second frontend would have to reimplement it, so move the **data** parts into the core first and leave only presentation in QML:

- `frontend/OBDParameterModel.qml` (476 lines): per-PID metadata (title, unit, min, max, `kind`) and the vehicle support gate. Move it to a core-side model and expose it as data.
- `frontend/dashboards/WidgetCatalog.qml` (257 lines): widget types, default spans, `supportedKinds`, `editableProps`. Make it a data file both the core and the clients read (the validation logic belongs in `DashboardManager`).
- `frontend/dashboards/DashboardRenderer.qml` (231 lines): spec → grid rendering. Rendering stays per-frontend, but the core should hand out **validated** specs, so each client doesn't re-implement the "bad cells are skipped" rules.
- `frontend/EnvironmentTheme.qml` (105 lines): album-art colour extraction. Compute the palette in the core and publish it as a property.
- Volume and navigation glue in `BottomBar.qml` and the page files: audit them for state that should belong to the core.

Update `docs/GAUGE_AUTHORING.md` when the PID metadata and catalog move.

## Recommended order of operations

1. (Prerequisite) Python sunset Phase 1: the C++ command server, designed as the service API.
2. Move the data logic out of QML (the list above). This helps even with a single process.
3. Refactor `src/main.cpp` so the managers live in a core library, with an in-process transport for mobile.
4. Split the process on desktop/Pi with QtRO for the QML UI. Keep video decode in the UI. Road-test in the Jeep.
5. Add the WebSocket/JSON gateway. Build a first small non-QML client (an OBD gauges web page or a media remote) to prove the API.
6. Fold the companion app's data link (`TODO/octave-companion-app.md`) onto the same gateway.
7. Update the wiki (`architecture.html`, `signals-slots-reference.html`, `development.html`, `building.html`) and `CLAUDE.md`.

Delete this file when step 5 lands and the remaining steps have their own plans.
