# OCTAVE Companion: the phone app

**Goal:** one Android app that links the phone to the car and does what a shell process can't: traffic stop mode, phone GPS into OCTAVE, notifications / calls / battery on the head unit, keeping the mirror connected, and the car's status while you're away. Also a first-run walkthrough for the mirror (pure QML, no app needed).
**Status:** deferred: scoped, not started. **Decided 2026-11-07: build a separate app** (`companion/`) rather than extending `phone_server/` for these features (see "Why an app").
**Last updated:** 2026-11-09

Already shipped: adb pairing and connect over Wi-Fi in the phone mirror via QR pairing (`f100f60`), with mDNS discovery (`phonemirrormanager.cpp:1218-1274`) and a remembered phone (`wirelessAddress`). Now-playing metadata ships through `phone_server`'s `PhoneMedia.java`. No app was needed for either.

## Why it's parked

Nothing here blocks daily use in the Jeep. A separate app is its own product (build, signing, release cycle, Android permission model), and its first real customer, traffic stop mode (`traffic-stop-mode.md`), is still at the spike stage.

## Why an app (decided, don't re-litigate)

- `phone_server/` lives only while adb is connected. Its camera session ends when the socket drops (`Server.java:136-145`), and it can't run after a reboot. Traffic stop mode needs recording that survives walk-away and screen lock.
- A real `NotificationListenerService`, `TelephonyCallback`, `SmsManager`, location and a Companion Device Manager association with the OBD adapter all need an installed app.
- Works with no mirror session: the app holds its own link to the car.

## Constraints

- **The mirror stays on `phone_server/`.** Screen capture with no consent dialog, virtual displays and touch injection need shell privileges. MediaProjection is not worth it (consent every session on Android 14+, no virtual display, FLAG_SECURE blackout).
- **Wireless debugging after a reboot:** most devices turn it off, and the user must turn it back on. The app can detect that and prompt; it can't toggle it.
- **Distribution: sideload / F-Droid, not Google Play.** Play limits `SEND_SMS` to default SMS apps and scrutinises notification access, and app-store removals are a real risk for traffic stop mode (Apple pulled ICEBlock-style apps in Oct 2025). Sign with the permanent APK key from `build.yml`'s Sign APK step.
- **Android only, minSdk 30.** iPhone background limits would gut most features; iPhones get AirPlay (`iphone-airplay.md`) and nothing more.

## Design rules

- **One app, one foreground service, one persistent notification.** Every feature rides the same car link, so battery cost doesn't stack per feature.
- **Each feature is an off-by-default switch** that asks for its own Android permission only when turned on.
- **No OCTAVE-run server, no telemetry.** Cloud features use storage the user owns.
- **The phone is a client of the service protocol** (`src/service/`): it pairs with the 6-digit code (`servicepairing`, `octave --pair-code`) and holds scopes (`servicescopes.h`) like any other client. New phone → car data goes through manager members in `docs/api/`, not a private side channel.

## The car link (foundation for everything below)

- **Bluetooth:** a CDM association with the OBD adapter (custom adapter or, until it exists, the head unit's own Bluetooth) wakes the app when you get in and marks "in the car" / "left the car".
- **Wi-Fi:** a paired service-protocol connection to the Pi (WebSocket on the service port; needs `--service-bind` or the setting that opens it to the car network).
- **A new manager on the OCTAVE side,** `src/managers/companionmanager.{h,cpp}` (exposed as `companion`): phone connected, battery, signal, location, notifications, call state. With its `octave.scope` declarations per CLAUDE.md.
- **Settings card** in `frontend/settings/AccessoriesSettingsPage.qml`: pair the app, see what each feature shares.

## Features

### v1: core (build alongside traffic stop mode)

1. **First-run mirror walkthrough** (no app). Replace the text block in `frontend/PhoneMirrorView.qml` (around line 498) with a step-by-step flow driven by `phoneMirrorManager.deviceState` / `deviceHint`: developer options, USB debugging, then optionally Wireless debugging + QR pairing. **Effort:** M. **Device:** S22 or S26 from developer options off.
2. **Car link + `companionmanager`** as above. **Effort:** L. **Device:** S22, Pi.
3. **Traffic stop recorder:** pre-armed camera / mic service, encrypted hash-chained pieces, SMS beacon, cloud + Pi + adapter copies. Everything is specified in `traffic-stop-mode.md`; this app is where it runs. **Effort:** L–XL. **Device:** S22, S26.
4. **Phone GPS into OCTAVE.** The Pi has no GPS. Feed location into `companionmanager` and the black box (`triprecorder` / `tripstore`: a location column per sample or per second), giving trip maps on `frontend/TripHistory.qml` and location for the traffic stop beacon. When the adapter's sensor pod GNSS exists, prefer the better source (`diy-obd-adapter.md`). **Effort:** M. **Device:** S22, Jeep.
5. **Notifications, calls, battery on the head unit.** `NotificationListenerService` (per-app allow list), `TelephonyCallback` for call state, battery and signal. OCTAVE side: an incoming-call / notification overlay in QML. Calls and SMS need explicit consent on the phone; say so in the UI. **Effort:** L. **Device:** S22, Pi.
6. **Keep the mirror connected.** Detect that wireless debugging is off after a reboot and post a one-tap notification that opens the right settings screen; report the adb state to OCTAVE so the mirror page can say "turn on wireless debugging on the phone" instead of failing silently. **Effort:** S–M. **Device:** S22.
7. **"Send to OCTAVE" share target.** Addresses, songs, podcasts, links from any app to the head unit (now-playing queue, a pinned address card until navigation exists). **Effort:** M. **Device:** S22, Pi.

### v2: the car from the phone

8. **Passenger remote.** Music, volume, dashboard switching, settings, a client of the existing service API (the reference is `sdk/web`). **Effort:** M. **Device:** S22, Pi.
9. **Where you parked.** Save the phone's location when the car link drops at engine-off; a map card in the app. **Effort:** S. **Device:** S22.
10. **Car health while you're away.** Sync the last trip summary, battery voltage, DTCs and the black-box health trends (`TripStore::computeHealth`) to the phone at engine-off; optional "check engine light: P0420" notification. **Effort:** M. **Device:** S22, Jeep.
11. **Internet for the Pi.** Turn on the phone's hotspot when the car is seen. A normal app can't toggle tethering; the shell can (spike: `cmd wifi start-softap` / `svc` variants on the S22), so this works while adb is up, with a prompt otherwise. Enables OCTAVE updates, Spotify and cloud upload without touching the phone. **Effort:** M. **Device:** S22, Pi.
12. **Custom adapter tools.** Firmware update over BLE, panic-button setup, microSD check, and reading DTCs straight from the adapter in a car without OCTAVE (`diy-obd-adapter.md`, after its step 3). **Effort:** L. **Device:** adapter, S22.

### Later

13. **Crash detection.** The car's IMU (`berryimumanager` or the adapter / pod) plus OBD speed detect the crash, which is far more reliable than a phone's sensors. The phone texts contacts after a cancellable countdown (Pixel-style). This one is automatic, unlike traffic stop mode, so the countdown and a loud cancel button are mandatory. **Effort:** L. **Device:** Jeep, S22.
14. **Tow / break-in alert.** The adapter wakes on motion while parked (its LIS2DH12, on Rev 0) and tells the phone; only in Bluetooth range unless the optional cellular hardware exists. **Effort:** M. **Device:** adapter, S22.
15. **Driver profiles.** The phone is the identity: connecting it loads that driver's music, dashboards and settings. **Effort:** M. **Device:** two phones, Pi.
16. **Settings and dashboard backup** to the phone or the user's cloud. **Effort:** S–M. **Device:** S22.
17. **One-tap bug report:** pull OCTAVE's logs (`<config>/logs/`) over the service and share them. **Effort:** S. **Device:** S22, Pi.

## Spikes

1. CDM association with an ESP32 dev board advertising BLE: does presence wake the app reliably on the S22 / S26? (Shared with `traffic-stop-mode.md` spike 2.)
2. The hotspot toggle from the shell (item 11).
3. Battery cost of the foreground service plus BLE presence across a full day.

## Cross-references

- `traffic-stop-mode.md`: v1 item 3 lives there; its spikes 1–3 are this app's first code.
- `diy-obd-adapter.md`: the adapter's BLE identity is the app's car-presence anchor; items 12 and 14 need its ESP32 bridge (its step 3).
- `phone-mirror-robustness.md`: item 6 complements it.
- `iphone-airplay.md`: what iPhone owners get instead.
- `frontend-platform.md` / `open-backend.md`: the app is a service-protocol client, so it follows the pairing and scope rules there.

## Recommended order

1. Walkthrough (item 1, independent, no app).
2. Spikes, together with traffic stop mode's.
3. Car link (2), then GPS (4), which pays off immediately in trip history.
4. Traffic stop recorder (3), keep-the-mirror-connected (6), notifications / calls (5), share target (7).
5. v2 in any order; parking (9) and bug reports (17) are quick wins.

Update `wiki/phone-mirror-android-auto.html` and a new `wiki/companion-app.html` as each lands; regenerate `docs/api/` when `companionmanager` changes.

**Delete this file when** v1 ships and the v2 / later items have either shipped or moved to their own plans.
