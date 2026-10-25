# TODO index — easiest to hardest

**Last updated:** 2026-10-25 (media-source-arbiter.md: Android Auto part done)

One file per deferred plan. Each file opens with a one-line **Goal**, lists only the work that is left, and gives every step an **Effort** (S under an hour, M one session, L several sessions, XL a new subsystem) and the device it needs. When a plan is finished, delete its file and its row here.

Devices: **S22** = Galaxy S22 test phone, **Pi** = Orange Pi 5 Plus head unit in the Jeep, **VM** = kora test VMs (Windows 11 / Linux / Pi OS).

## 1. Quick wins (S items, mostly desktop)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [windows-adb-firewall.md](windows-adb-firewall.md) | Verify the new adb firewall rule in the Windows installer (code done) | S | Win11 VM, a phone |
| [phone-mirror-robustness.md](phone-mirror-robustness.md) | Mirror recovers fast from network and USB changes, works beyond Samsung | S … L | phone on the Pi, then other phones |
| [android-cpp-port.md](android-cpp-port.md) | APK good enough to daily-drive: icon, splash, log check, then permissions / audio focus | S … L | S22 |
| [sensorhome-3d-first-visit.md](sensorhome-3d-first-visit.md) | Check on the phone that the first 3D View tap no longer freezes (code done) | S | S22, Pi |
| [error-notification-ui.md](error-notification-ui.md) | Notice for phone-mirror failures off-page; check the OBD notice on the Pi | S | desktop, Pi |
| [sensor-driven-ui-cost.md](sensor-driven-ui-cost.md) | Measure on the phone: idle sensors stopped, the grid fix, the Full Grid warnings (code done) | S | S22, Pi |

## 2. One session each (M)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [dashboards-roadmap.md](dashboards-roadmap.md) | Prove the dashboard editor and demand-driven polling on a touchscreen and in the car | M | S22, Jeep |
| [future-tests.md](future-tests.md) | Signal wiring test (after moving main.cpp connects into a function), then QML integration and OBD replay suites | M … L–XL | desktop, Jeep for OBD recordings |
| [update-checker-packaged-builds.md](update-checker-packaged-builds.md) | Packaged builds compare against the latest GitHub release | M | VMs, S22 |
| [dp-helper-consolidation.md](dp-helper-consolidation.md) | One shared `dp()` instead of 63 private copies | M | 10-min S22 check first |
| [obd-adapter-discovery.md](obd-adapter-discovery.md) | Pick-and-connect Bluetooth OBD on every platform (Jeep test S, other platforms L) | S, then L | Pi + NEXAS, S22 |
| [octave-companion-app.md](octave-companion-app.md) | Setup walkthrough for phone debugging, then phone notifications / calls / battery | M, then L | S22 |
| [phone-spotify-library.md](phone-spotify-library.md) | Step 1 (shared song rows, shuffle/repeat to the phone) M; library reader is a maybe | M … L | S22 + Spotify |
| [arm64-qt-ffmpeg-lgpl.md](arm64-qt-ffmpeg-lgpl.md) | aarch64 AppImage ships an LGPL FFmpeg for Qt's music playback instead of Ubuntu's GPL one | M (or with a Qt upgrade) | arm64 CI, Pi |
| [frontend-platform.md](frontend-platform.md) | The frontend platform landed on the desktop (2026-10-23); left: watch the first release run of the `--sim` smoke steps, an installed exec frontend on Windows, macOS and the Pi, many clients measured on the Pi, two items in review (`--pair-code`, web-frontend scopes on the head unit) | S-M | CI, Win11 VM, Pi |

## 3. Several sessions (L)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [media-source-arbiter.md](media-source-arbiter.md) | One music source at a time: done for Android Auto; left: by-ear checks, phone mirror on the same rules, visualizer | S | desktop, AA phone, Pi |
| [trip-history.md](trip-history.md) | Health trends across the drives the black box now records (recorder + Trip History page shipped 2026-10-21) | M | desktop (sim), Jeep |
| [qml-precompile.md](qml-precompile.md) | Ship pre-compiled QML so first launch doesn't stall | L | Win11 VM, Pi, S22 |
| [user-primitives.md](user-primitives.md) | Save and share your own styled gauges and gauge groups | L (code widgets XL) | desktop |
| [open-backend.md](open-backend.md) | The contract is done (2026-10-23); left: two small service follow-ups, the hardware checks noted along the way, and the core library / process split after v1.0 road time | S; library M; split L | desktop, Pi, phone, Android |

## 4. New subsystems (XL)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [phone-mirror-android-host.md](phone-mirror-android-host.md) | OCTAVE on an Android tablet mirrors a phone | XL | tablet + phone |
| [android-auto-native-receiver.md](android-auto-native-receiver.md) | Android Auto straight into OCTAVE over adb (works on desktop; Pi, AOA next), no DHU | L | AA phone, Pi |
| [carlinkit-dongle-port.md](carlinkit-dongle-port.md) | CarPlay / wireless Android Auto through a Carlinkit dongle | XL | $60 dongle, iPhone |
| [diy-obd-adapter.md](diy-obd-adapter.md) | Our own all-protocol Bluetooth OBD adapter (STM32G474 + ESP32) with native fast mode, store-and-forward, optional sensor pod | M bench steps, then L–XL hardware | Nucleo + ECU sim, Jeep |

Shared prerequisite for the last three and for phone-mirror item 6 (hardware decode): pull the H.264 decoder out of `src/phone_mirror/scrcpyclient.cpp` into its own class.
