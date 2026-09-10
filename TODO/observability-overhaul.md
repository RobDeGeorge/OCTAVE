# Observability overhaul — make OCTAVE post-mortem-debuggable

**Status:** in progress — Chunks 1–4 shipped 2026-09-10. Remaining: Chunk 5 (Java BLE bridge → OCTAVE log) and optional Chunk 6 (UI watchdog).
**Last updated:** 2026-09-10

## Why this exists

OCTAVE is a daily-driver head unit in a real Jeep. When the app crashes, an OBD adapter drops mid-drive, or the UI freezes, **the user currently cannot tell why after the fact.** The Python backend has decent rotating file logging, but Python is desktop-only — the Android sideload is C++, and the C++ build has effectively no persistent logging. This file plans the work to fix that.

This is not a feature ask. This is so that real-world failures stop being unrecoverable mysteries.

## What the audit found (2026-05-07)

| Area | Status | Notes |
|---|---|---|
| Python rotating logs (`backend/logging_config.py`) | ✓ working | 5 MB × 4 main, 2 MB × 6 error, 10 MB × 3 debug. Used by 9 of 10 backend managers. |
| Python `elm327_protocol.py` | ✗ **0 logger calls** | Exception handlers at lines 226, 254, 294 swallow errors silently. The layer where mysterious OBD drops actually happen is dark. |
| C++ message handler in `src/main.cpp` | ✗ **not installed** | No `qInstallMessageHandler()`. Every `qDebug()` / `qCDebug()` (57 in `obdmanager.cpp` alone) goes to stderr and dies on app exit. |
| C++ logging categories | ✓ used (re-audit 2026-09-10) | 13 managers now log through their category; the stale audit row is kept for history. |
| C++ file rotation | ✗ none | No `QFile` rotator, no third-party logger. |
| Crash handlers (both langs) | ✗ none | No `sys.excepthook`, no `threading.excepthook`, no `qInstallMessageHandler` for `qFatal`, no `signal(SIGSEGV)`, no `faulthandler`, no breakpad/crashpad. Segfaults leave no trace. |
| QML console output | ✗ stderr only | 108 `console.log/warn/error` calls across `frontend/` go to stderr/logcat — never reach disk. |
| Android log retrieval | ✗ broken | Logs land in `/data/data/.../files/...` private app storage. No in-app viewer, no "Share Logs" button, no `FileProvider`. Recovery requires laptop + `adb pull`. |
| Java `OctaveOBDBridge.java` BLE state | ⚠ logcat-only | Lines 256–258 / 262–282 use `Log.i(TAG, ...)` — never reach OCTAVE's own log files. |
| Watchdog for hung event loop | ✗ none | If Qt event loop locks for 5 seconds mid-drive, no record exists. |

## The plan

Six chunks, sequenced so each one delivers value standalone. Stop after any of them and the codebase is still in a better state.

### Chunk 1 — C++ message handler + rotating file logger (highest leverage) — DONE 2026-09-10

**Goal:** every `qDebug`, `qCDebug`, `qWarning`, `qCritical`, `qFatal`, and QML `console.*` call ends up in a rotating file on disk in the same directory the Python build uses.

**Touch:**
- `src/main.cpp` — install `qInstallMessageHandler` BEFORE `QGuiApplication` is constructed. Handler writes to a shared `Logger` singleton.
- New `src/util/logger.{h,cpp}` — owns the rotating file (size + count match Python: 5 MB × 4). Thread-safe via `QMutex`. Knows the OS-specific log dir from `SettingsManager::getAppDataDir() + "/logs/"`.
- Set `QT_LOGGING_RULES` env or `QLoggingCategory::setFilterRules()` so that the categories already declared in `src/managers/*.cpp` actually emit.
- Format: `[YYYY-MM-DD HH:MM:SS.zzz] [LEVEL] [category] message (file:line)` to match Python format.

**Done when:**
- A debug-build run produces `~/.config/OCTAVE/logs/octave-cpp.log` populated with messages from at least 3 different managers.
- Killing the app with `kill -9` and reopening still leaves the prior log intact.
- Rotation triggers at 5 MB (verify by writing junk in a loop).

**Effort:** ~3 hours. No external deps.

### Chunk 2 — Crash handlers (both backends) — DONE 2026-09-10 (simple signal()/faulthandler approach; no breakpad)

**Goal:** an unhandled exception or signal logs a stack trace before death.

**Python** (`main.py`):
- `sys.excepthook` writes traceback to error log and re-raises.
- `threading.excepthook` (Python 3.8+) catches dead worker threads (BerryIMU, gesture, BT, OBD).
- `faulthandler.enable(file=open(error_log_path, 'a'))` for native segfaults.

**C++:**
- `qInstallMessageHandler` already catches `qFatal`/assertions (Chunk 1).
- For SIGSEGV/SIGABRT: either bundle [Google Breakpad/Crashpad](https://chromium.googlesource.com/breakpad/breakpad/) or, simpler first pass, `signal(SIGSEGV, handler)` writing `backtrace_symbols()` output to the log on Linux/Android. Windows uses `SetUnhandledExceptionFilter` + `CaptureStackBackTrace`.

**Done when:** intentionally raising `SIGSEGV` (`kill -SEGV $(pgrep octave)`) leaves a stack trace in the error log on both Linux and Android.

**Effort:** ~4 hours for the simple `signal()` approach; ~1 day for breakpad integration with proper symbol upload.

### Chunk 3 — Instrument `elm327_protocol` on both sides — DONE 2026-09-10 (Python: python-obd's own `obd.*` logger tree now writes to our files; C++: `octave.elm327` category, TX/RX/timeout lines in `OBDConnectionWorker::sendCommand`)

**Goal:** every ELM327 command sent, response received, parse failure, and timeout is logged with enough context to reconstruct a failed OBD session.

**Touch:**
- `backend/elm327_protocol.py` — add `logger = get_logger(__name__)`, log at DEBUG for command/response pairs, WARNING for timeouts, ERROR for parse failures. Replace the silent `except` at lines 226, 254, 294 with `logger.exception(...)`.
- `src/managers/elm327protocol.cpp` — mirror with `qCDebug(elm327Cat) << ...`.
- Make sure command logging includes the AT command string and the raw bytes received (truncated to 200 chars).

**Done when:** running an OBD session with `--debug` produces a readable transcript of every protocol exchange in the debug log.

**Effort:** ~2 hours. Pure instrumentation, no logic changes.

### Chunk 4 — Android log retrieval (in-app) — DONE 2026-09-10 as a Diagnostics card on the About page (`DiagnosticsManager`, both backends). Export copies the files to Downloads instead of a share intent: androidx (FileProvider) is deliberately not bundled (see android/build.gradle), and the sideload build already holds MANAGE_EXTERNAL_STORAGE. Untested on the Android tablet; desktop verified.

**Goal:** the user can extract logs from the Jeep without a laptop.

**Touch:**
- New "Diagnostics" page under Settings → System (or wherever the dev/about screen lives). Shows: app version, last 100 log lines, "Share logs" + "Copy to clipboard" + "Email logs to me" buttons.
- Android `FileProvider` declared in `AndroidManifest.xml` for the log directory so the share intent works.
- "Email logs" auto-attaches `octave.log` + `octave-error.log` + device info (model, Android version, app version) and pre-fills subject `OCTAVE bug report — <date>`.
- Add a quick-access trigger: long-press the version label on the splash/about screen → opens diagnostics. (Discoverability without cluttering the main UI.)

**Done when:** from the parked Jeep, the user can hit "Share logs" and email themselves the last 32 MB of logs without plugging anything in.

**Effort:** ~1 day. Most of it is the QML page; the `FileProvider` is small.

### Chunk 5 — Java BLE bridge logging into OCTAVE's log files

**Goal:** `OctaveOBDBridge.java` BLE state transitions show up in `octave-cpp.log`, not just logcat.

**Touch:**
- `android/src/org/octave/app/OctaveOBDBridge.java` — JNI callback `nativeOnBLEEvent(String level, String message)` that crosses into the C++ logger.
- C++ side registers the JNI method, forwards into the same logger Chunk 1 wired up.
- Replace `Log.i(TAG, ...)` calls at the connection/disconnection/error sites (lines ~256–282) to also call the JNI method.

**Done when:** an Android BLE disconnect produces a log line in OCTAVE's own log file, retrievable via Chunk 4's share button.

**Effort:** ~3 hours.

### Chunk 6 — UI watchdog (optional, lowest priority)

**Goal:** detect when the Qt event loop is hung and log it (and ideally write a thread dump).

**Touch:**
- Background `QTimer` on a separate `QThread` that pings the main thread every 2 s. If the main thread doesn't respond within 5 s, log `WARNING — UI thread blocked for Ns` and dump active thread states.
- Threshold tuning matters — don't trigger on legitimate slow operations (image decoding, dashboard JSON load).

**Done when:** intentionally `sleep(10)` in a QML `onClicked` handler produces a watchdog log entry.

**Effort:** ~3 hours.

## Order of operations

1. **Chunk 1** (C++ logger) first — everything else depends on having a place for C++ logs to land.
2. **Chunk 2** (crash handlers) immediately after — pairs naturally with Chunk 1 since the C++ side reuses the Chunk 1 file.
3. **Chunk 4** (Android share) next — the user gains the ability to actually retrieve logs from the Jeep, which makes everything afterward verifiable in the field.
4. **Chunk 3** (ELM327 instrumentation) — small, isolated, can be done any time after Chunk 1.
5. **Chunk 5** (Java BLE forwarding) — depends on Chunk 1.
6. **Chunk 6** (watchdog) — last, optional.

Total estimated effort: **2–3 focused days** for Chunks 1–5.

## Prerequisites and blockers

- None blocking. All chunks are self-contained code work in this repo.
- Chunk 4 needs the in-flight media settings revamp committed first to avoid merge conflicts in `frontend/settings/`.
- Parity rule (`CLAUDE.md`): Chunk 3 must touch both `backend/elm327_protocol.py` AND `src/managers/elm327protocol.cpp` in the same change set.

## Cross-references

- `TODO/error-notification-ui.md` — driver-actionable error UI is deferred by design, but a Diagnostics page (Chunk 4) is *not* a notification UI; it's a parked-and-tap recovery tool. Different scope.
- `TODO/android-cpp-port.md` Phase 4 (sideload polish) — Chunks 1, 2, 4, 5 are arguably part of Phase 4. Could be folded in or kept separate. Keeping separate so the work is concrete.
- `TODO/god-object-splits.md` — no overlap, but observability lands first because splitting god objects without logs is reckless.

## Delete this file when done

Delete after Chunks 1–5 ship. Watchdog (Chunk 6) is optional and can be re-parked in its own file if abandoned.
