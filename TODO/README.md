# TODO index — easiest to hardest

**Last updated:** 2026-10-18 (folder audited against the code; finished and obsolete plans deleted)

One file per deferred plan. Each file opens with a one-line **Goal**, lists only the work that is left, and gives every step an **Effort** (S under an hour, M one session, L several sessions, XL a new subsystem) and the device it needs. When a plan is finished, delete its file and its row here.

Devices: **S22** = Galaxy S22 test phone, **Pi** = Orange Pi 5 Plus head unit in the Jeep, **VM** = kora test VMs (Windows 11 / Linux / Pi OS).

## 1. Quick wins (S items, mostly desktop)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [windows-adb-firewall.md](windows-adb-firewall.md) | Verify the new adb firewall rule in the Windows installer (code done) | S | Win11 VM, a phone |
| [phone-mirror-robustness.md](phone-mirror-robustness.md) | Mirror starts fast, recovers cleanly, works beyond Samsung (items 1–6 are S) | S … L | desktop first, then phones |
| [dashboards-roadmap.md](dashboards-roadmap.md) | Remove the Primitives Gallery, placeholder for too-new dashboards, then on-device checks | S, then checks | S22, Jeep |
| [future-tests.md](future-tests.md) | Tests for volume routing, metadata sanitizing, settings round-trip, then bigger suites | S … L–XL | desktop |
| [android-cpp-port.md](android-cpp-port.md) | APK good enough to daily-drive: icon, splash, version from CMake, log check, then permissions / audio focus | S … L | S22 |
| [sensorhome-3d-first-visit.md](sensorhome-3d-first-visit.md) | No 1.2 s freeze on the first 3D View tap | S–M | S22, Pi |
| [error-notification-ui.md](error-notification-ui.md) | App-wide notice when OBD drops mid-drive while on another page | S–M | desktop (sim) |
| [sensor-driven-ui-cost.md](sensor-driven-ui-cost.md) | Stop idle phone sensors; stop the OBD grid redrawing every card | S–M, M | S22, Pi |

## 2. One session each (M)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [yt-cookies-file-picker.md](yt-cookies-file-picker.md) | Import / test / clear the YouTube cookies file in the app | M | desktop, S22 |
| [update-checker-packaged-builds.md](update-checker-packaged-builds.md) | Packaged builds compare against the latest GitHub release | M | VMs, S22 |
| [dp-helper-consolidation.md](dp-helper-consolidation.md) | One shared `dp()` instead of 63 private copies | M | 10-min S22 check first |
| [obd-adapter-discovery.md](obd-adapter-discovery.md) | Pick-and-connect Bluetooth OBD on every platform (Jeep test S, other platforms L) | S, then L | Pi + NEXAS, S22 |
| [octave-companion-app.md](octave-companion-app.md) | Setup walkthrough for phone debugging, then phone notifications / calls / battery | M, then L | S22 |
| [phone-spotify-library.md](phone-spotify-library.md) | Step 1 (shared song rows, shuffle/repeat to the phone) M; library reader is a maybe | M … L | S22 + Spotify |

## 3. Several sessions (L)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [god-object-splits.md](god-object-splits.md) | Split the settings / media / OBD managers into helpers (after the tests above) | M–L … L–XL | desktop, S22 for BLE |
| [trip-history.md](trip-history.md) | Record drives and engine health on the box and show a Trip History page | M–L | desktop (sim), Jeep |
| [qml-precompile.md](qml-precompile.md) | Ship pre-compiled QML so first launch doesn't stall | L | Win11 VM, Pi, S22 |
| [user-primitives.md](user-primitives.md) | Save and share your own styled gauges and gauge groups | L (code widgets XL) | desktop |
| [backend-service-split.md](backend-service-split.md) | Headless core process any frontend connects to | M–L … XL | desktop, Pi |

## 4. New subsystems (XL)

| File | Goal | Effort | Needs |
|---|---|---|---|
| [phone-mirror-android-host.md](phone-mirror-android-host.md) | OCTAVE on an Android tablet mirrors a phone | XL | tablet + phone |
| [android-auto-native-receiver.md](android-auto-native-receiver.md) | Android Auto straight into OCTAVE over USB, no DHU | XL | AA phone, Pi |
| [carlinkit-dongle-port.md](carlinkit-dongle-port.md) | CarPlay / wireless Android Auto through a Carlinkit dongle | XL | $60 dongle, iPhone |

Shared prerequisite for the last three and for phone-mirror item 11 (hardware decode): pull the H.264 decoder out of `src/phone_mirror/scrcpyclient.cpp` into its own class.
