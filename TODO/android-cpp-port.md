# Android C++ Port

**Status:** active — Phases 0–3 complete. OBD-over-BLE and YT Music search/download both shipped. Remaining work is Phase 4 (sideload polish).
**Last updated:** 2026-05-07

## Why this is parked here

OCTAVE's C++ / Qt 6 backend (`src/`) is the primary desktop build across Windows, macOS, Linux, Raspberry Pi, and is now the **only** Android build. The Python / PySide6 Android APK (buildozer/p4a) was deleted on 2026-05-02 — Python is desktop-only and there is no longer a Python reference build for Android.

This TODO tracks finishing the C++ APK feature work. Build + sideload already work:

```bash
cmake --build build-android -j
adb install -r build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Remaining work: Android-specific manager paths (OBD Bluetooth, runtime permissions, Play Store polish).

## Target devices

- Samsung Galaxy Tab S6 (`SM-T860` / `SM-T865`) — arm64-v8a, Android 11+
- Samsung Galaxy S22 Ultra — arm64-v8a, Android 12+

Single ABI (`arm64-v8a`) for now. Add `armeabi-v7a` only if a real target emerges.

## Scope decisions (locked in 2026-04-20)

- **Distribution:** LGPL Qt, dynamically linked, open-source-licenses screen required in-app before Play Store submission.
- **Play Store:** deferred to a dedicated later milestone after sideload is proven. Paid app (Qt for Small Business or ongoing LGPL compliance — decide closer to submission).
- **Spotify:** **excluded from Android entirely**. Spotify is desktop-only.
- **Downloads (`yt-dlp`):** kept for sideload (`OCTAVE_ENABLE_DOWNLOADS=ON` default), stripped for Play Store (`OFF`) — YouTube downloads violate Play policy.
- **OBD transport on Android:** **BLE-first** (`QBluetoothDeviceDiscoveryAgent` restricted to `LowEnergyDiscoveryMethod` for scan; `QLowEnergyController` for connect). Manual MAC entry as a fallback for when the adapter is already known. Classic RFCOMM (`QBluetoothSocket`) only as a last-ditch fallback if a discovered device is explicitly tagged Classic. The QML UI in `frontend/settings/OBDSettingsPage.qml` (Android branch) is already wired for this — backend just needs the C++ implementation; the now-deleted `backend/android_obd_manager.py` is the reference for the API surface QML expects.
- **CAMERA + RECORD_AUDIO permissions:** declared aspirationally in manifest (future reverse-camera / voice-command features). Review before Play Store submission — unused permissions trigger review friction.

## Android-minimum manager matrix

Mirror what Python's `main.py` + `backend/platform_config.py` already does: every manager always instantiated so QML context properties exist, hardware-absent ones stubbed to no-ops.

| Manager (C++) | Android behavior | Notes |
|---|---|---|
| `settingsmanager` | Full | Storage path via `QStandardPaths::AppDataLocation` |
| `clock` | Full | No change |
| `mediamanager` | Full | Local audio playback via QMediaPlayer — already portable |
| `spotifymanager` | **Stub** | Excluded from Android per scope decision |
| `audioanalyzer` | Full (file FFT only) | Mic input deferred — `RECORD_AUDIO` aspirational |
| `obdmanager` + `elm327protocol` | Full (new BT transport) | Refactor to `IOBDTransport` with serial + BT impls |
| `volumecontroller` | Full (phone mirror + ESP32 nulled) | Match Python: pass nullptr to those deps on Android |
| `networkmanager` | Full | No change |
| `dashboardmanager` | Full | JSON format must stay cross-platform compatible |
| `berryimumanager` | **Replaced** | New `AndroidSensorManager` using `QSensor` (QtSensors module) — accel/gyro/mag/baro from device |
| `gesturemanager` | Stub | PAJ7620U2 absent on Android |
| `androidautomanager` | Stub | DHU is desktop-only (receives-from-phone, not runs-on-phone) |
| `phonemirrormanager` | Stub | scrcpy is desktop-only |
| `esp32volumemanager` | Stub | USB serial absent on Android |
| `downloadmanager` | Gated by `OCTAVE_ENABLE_DOWNLOADS` | ON for sideload, OFF for Play Store |

## Phased plan

### Phase 0 — Toolchain setup (user-owned)
Follow `ANDROID_BUILD_SETUP.md` at repo root. Installs Qt for Android 6.11.0, Android SDK + NDK, wires env vars, enables USB debugging on both test devices. Blocking for all code work below.

### Phase 1 — CMake Android wiring + stubs
1. Create `android/` at repo root with `AndroidManifest.xml` (permissions per scope decisions), `build.gradle`, icon resources, `res/values/libs.xml` for `androiddeployqt`.
2. `CMakeLists.txt`:
   - `if(ANDROID)` branch excluding hardware-only `.cpp` files
   - Force `OCTAVE_ENABLE_DOWNLOADS=OFF` for `PlayStore` build variant
   - `QT_ANDROID_PACKAGE_SOURCE_DIR` → `${CMAKE_SOURCE_DIR}/android`
   - Add `qt_finalize_executable(octave)` so `androiddeployqt` runs
   - Link `Qt6::Bluetooth`, `Qt6::Sensors`, `Qt6::CorePrivate` (for `QtAndroidPrivate` permissions API)
3. Write stub managers (`src/managers/stubs/`): same public API as real ones, no-op implementations, emit "unavailable" signals where QML expects them.
4. `src/main.cpp`: conditional `#ifdef Q_OS_ANDROID` to instantiate stubs in place of hardware managers.
5. Mirror the Python parity for `spotify` exclusion: add `StubSpotifyManager` to the Python side too (`backend/platform_config.py` already has the gating pattern — extend it).

**Done when:** `cmake --build build-android --target apk && adb install` puts an app on both test devices that launches to the main menu. OBD/media/dashboards visible in UI, OBD disconnected.

### Phase 2 — Android runtime permission flow
1. Helper class wrapping `QtAndroidPrivate::requestPermissions` for `BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, `ACCESS_FINE_LOCATION`, `READ_MEDIA_AUDIO`.
2. QML permission prompts on first use (not on startup — Play Store prefers just-in-time).
3. Graceful denial handling (show explainer, let user re-prompt or open app settings).

**Done when:** fresh install on S22 Ultra walks through a clean permission flow for OBD + media library.

### Phase 3 — OBD over Bluetooth — DONE (2026-05-07)
Shipped via JNI bypass of Qt's broken BLE stack. `OctaveOBDBridge` handles BLE on Android; `OBDManager` is wired through it. The QtBluetooth `IOBDTransport` refactor was abandoned in favor of the JNI path — Qt's BLE implementation was unreliable enough that the fallback became the primary. Live PIDs stream on the S22 Ultra. Bluetooth permission auto-requested on first connect.

Reference commits: `f1901d9` (BLE working via JNI), `2d05676` (OBDManager wired to OctaveOBDBridge), `08e8245` (auto-request Bluetooth permission), `542b446` (live connection log).

### Phase 4 — Sideload polish
- Splash screen, app icon (all mipmap densities).
- Landscape lock in manifest (`screenOrientation="landscape"` — Python uses env var, C++ uses manifest).
- Audio focus: verify QMediaPlayer pauses on incoming call and resumes after.
- Scoped storage: confirm `settingsConfigure.json` + dashboard JSON land in `AppDataLocation`.
- "Open-source licenses" screen in settings (LGPL compliance prerequisite).
- Version code / version name scheme.
- `adb install -r` smoke test on both devices after each major commit.

**Done when:** daily-drivable APK. Owner uses it in the Jeep for a week without bugs that block a drive.

### Phase 5 (deferred — separate TODO when Phase 4 ships)
Play Store submission. Write `TODO/android-play-store.md` at that point covering: upload keystore generation, signed AAB, privacy policy URL, permission justifications, internal → closed → production track progression, pricing model decision.

## Prerequisites and blockers

- Phases 0–3 complete.
- **Phase 4** is the only remaining open phase — daily-drivable APK polish.
- **Phase 5 blocked on Phase 4 AND on an LGPL-vs-commercial-Qt license decision.**

## Cross-references

- `ANDROID_BUILD_SETUP.md` — user-facing toolchain install walkthrough at repo root.
- `BUILD.md` — desktop build matrix, app-store build flag explanation.
- `deployment/buildozer.spec` — Python APK's current manifest / permissions / requirements, reference for parity decisions.
- `backend/android_obd_manager.py` — Python BT OBD implementation, reference for JNI fallback if QtBluetooth fails in Phase 3.
- `backend/platform_config.py` — Python's `IS_ANDROID` gating pattern, mirror on C++ side via `#ifdef Q_OS_ANDROID`.
- `.private-notes.md` (gitignored) — NEXAS MAC for test sessions.

## Order of operations

1. User finishes Phase 0 (install Qt for Android, SDK, NDK, USB debugging).
2. User says "toolchain is ready."
3. Claude starts Phase 1 (CMake scaffolding + stubs) — can actually start earlier if needed, since CMake wiring doesn't require the SDK to be installed, just the Qt for Android path. Safer to wait.
4. Phases 2–4 land in order with a sideload verification on both devices after each.
5. Phase 5 gets its own TODO when Phase 4 is proven in the wild.

## Delete this file when done

When a signed release AAB ships to the Play Store production track and the port is self-sustaining (CI building APKs on every main push, documented in `wiki/`), delete this TODO. Until then it's the source of truth for intent.

## Observability, verify on device (added 2026-09-10)

The observability overhaul (formerly `TODO/observability-overhaul.md`, all six chunks shipped 2026-09-10) has two pieces that could only be built blind here and need one on-device pass on the Android tablet:

- **Diagnostics card** (Settings → About): `DiagnosticsManager::exportLogs()` copies `octave*.log*` into `QStandardPaths::DownloadLocation` (`/storage/emulated/0/Download/OCTAVE-logs-<timestamp>/`). Confirm the folder appears in the Files app and that a mail app can attach from it (the sideload build holds `MANAGE_EXTERNAL_STORAGE`; no FileProvider because androidx is not bundled).
- **BLE bridge log forwarding**: `OctaveOBDBridge.pollLog()` drained in `OBDManager::onAndroidBlePoll()`. Connect the ELM327 dongle, then check `octave-cpp.log` for `[BLE] ...` lines (connect, service discovery, notifications enabled) and pull the plug for a `[BLE]` warning.
- Also confirm `octave-cpp.log` exists at all on Android (`getAppDataDir()` resolves under the app's files dir) and that a forced crash (`kill -SEGV`) leaves a `==== FATAL SIGSEGV ====` block; bionic has no `execinfo`, so expect the "no backtrace support" line, not a stack.
