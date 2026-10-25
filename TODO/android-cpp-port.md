# Android C++ Port: sideload polish

**Goal:** make the sideloaded APK good enough to daily-drive in the Jeep, then get ready for the Play Store.
**Status:** active. The APK builds (locally and in the `cpp-build-android` CI job), installs and runs; OBD over BLE works. What's left is polish.
**Last updated:** 2026-10-24

Done so far, in one line: APK version from `project()` and the About > licenses card (2026-10-18), CMake/`android/` wiring, `#ifdef Q_OS_ANDROID` hardware gating in `src/main.cpp`, landscape lock (`android/AndroidManifest.xml:63`), OBD over BLE through the JNI `OctaveOBDBridge` (`f1901d9`, `2d05676`), app data under `SettingsManager::getAppDataDir()`.

Test device: Galaxy S22 (arm64-v8a, see the Android phone loop in `BUILD.md`). Build and install:

```bash
cmake --build build-android -j
adb install -r build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
```

## Hardware managers on Android

These are stubs (no-ops) on Android today: `gesturemanager`, `androidautomanager` (the DHU is desktop-only), `phonemirrormanager`, `esp32volumemanager`. `berryimumanager` has no Android replacement yet (see item 6). (The music downloader was removed on 2026-10-24, along with the `MANAGE_EXTERNAL_STORAGE` permission it needed.)

## Remaining work, easiest first

1. **App icon.** `android/res/mipmap-anydpi-v26/` is empty and the manifest has no `android:icon`, so the launcher shows the stock Android icon. Add an adaptive icon (foreground + background) plus PNG fallbacks for each mipmap density, and set `android:icon` / `android:roundIcon` on `<application>`.
   **Effort:** S. **Needs:** S22 to eyeball the launcher.
2. **Splash screen.** Nothing exists. Add a `windowBackground`/splash theme (Android 12+ `SplashScreen` attributes) showing the OCTAVE mark until Qt draws its first frame.
   **Effort:** S. **Needs:** S22.
3. **On-device observability check.** Code is in place but never run on a phone:
   - Settings → About → Diagnostics: `DiagnosticsManager::exportLogs()` (`src/managers/diagnosticsmanager.cpp:97`) should create `/storage/emulated/0/Download/OCTAVE-logs-<timestamp>/`, visible in Files and attachable from a mail app.
   - Connect the ELM327 and check `octave-cpp.log` for `[BLE]` lines (`OBDManager::onAndroidBlePoll()`, `src/managers/obdmanager.cpp:2688`); pull the dongle and look for a `[BLE]` warning.
   - Confirm `octave-cpp.log` exists at all, and that `kill -SEGV` leaves a `==== FATAL SIGSEGV ====` block with the "no backtrace support" line (bionic has no `execinfo`).
   **Effort:** S. **Needs:** S22 + the BLE ELM327 dongle.
4. **Just-in-time permissions with denial handling.** Today `src/main.cpp:199-219` asks for audio and Bluetooth permissions all at once on launch, and `OctaveOBDBridge.java:143-158` re-asks on connect. Steps: drop the startup loop; ask for `READ_MEDIA_AUDIO` when the media library is first opened and Bluetooth when OBD connect is first tapped; on denial, show a short explainer with "Try again" and "Open app settings" (`Settings.ACTION_APPLICATION_DETAILS_SETTINGS` via JNI); re-scan the library once audio is granted.
   **Effort:** M. **Needs:** S22, fresh install (`adb uninstall` first).
5. **Audio focus.** No `AudioManager` code exists. Request audio focus when playback starts (JNI `AudioManager.requestAudioFocus` with an `AudioFocusRequest`), pause `MediaManager` on `AUDIOFOCUS_LOSS`/`LOSS_TRANSIENT` (incoming call), resume on `AUDIOFOCUS_GAIN` if we paused it.
   **Effort:** M. **Needs:** S22 + a second phone to place a call.
6. **AndroidSensorManager.** Nothing reads the phone's sensors, so the IMU parameters (`HEADING`, G-force, etc. in `OBDParameterModel`) are empty on Android. Add a manager using QtSensors (`QAccelerometer`, `QGyroscope`, `QMagnetometer`, `QPressureSensor`) that publishes the same values/signals as `berryimumanager`, instantiate it under `Q_OS_ANDROID`, link `Qt6::Sensors`.
   **Effort:** M–L. **Needs:** S22 (rotate/tilt to check axes and heading).
7. **Daily-drive week.** Use the APK in the Jeep for a week with no drive-blocking bugs. That is the exit for this file's polish phase.
   **Effort:** L (calendar time). **Needs:** S22 in the Jeep with the OBD dongle.

## After that: Play Store (write `TODO/android-play-store.md` then)

Upload keystore, signed AAB, privacy policy URL, permission justifications (review `CAMERA` / `RECORD_AUDIO`, unused ones cause review friction), internal → closed → production tracks. Open question: OCTAVE is GPL-3.0+ (`3352b92`, required by Qt Quick 3D / Timeline), so the store listing must ship with the source offer and the licenses screen (Settings > About, added 2026-10-18; check it on the S22); decide whether a paid listing is still wanted under GPL.

Delete this file when a signed AAB is on the Play Store production track (or Play Store is dropped and item 7 passes).
