# OCTAVE Companion: phone data and setup walkthrough

**Goal:** get the phone's notifications, calls and battery onto OCTAVE, and walk new users through turning on debugging instead of showing a block of text.
**Status:** deferred — scoped, not started
**Last updated:** 2026-10-18

Already shipped: the wireless half of the original plan (adb pairing and connect over Wi-Fi) landed in the phone mirror via QR pairing (`f100f60`), with mDNS discovery (`phonemirrormanager.cpp:1218-1274`) and a remembered phone (`wirelessAddress`). Now-playing metadata also ships, through `phone_server`'s `PhoneMedia.java`. No separate app was needed for either.

## Why it's parked

Neither item blocks daily use in the Jeep. A separate app is its own product surface (Play Store or sideload, its own release cycle, Android permission model), so it should only be built if extending `phone_server/` can't do the job.

## Constraints (don't re-litigate)

- The mirror stays on `phone_server/` (our scrcpy fork, launched over adb). Screen capture without a per-session consent dialog, virtual displays and touch injection need shell privileges, which only an adb-launched process gets. MediaProjection is not worth using: consent dialog every session on Android 14+, no virtual display, FLAG_SECURE blackout.
- Wireless debugging must be re-enabled by the user after a phone reboot on most devices. An app can detect that and prompt; it cannot toggle it.

## Remaining work, easiest first

1. **First-run setup walkthrough.** **Effort:** M. **Device:** S22 or S26, starting from developer options off.
   - Replace the text block in `frontend/PhoneMirrorView.qml` (around line 498) with a step-by-step flow: enable developer options, USB debugging, then optionally Wireless debugging + QR pairing. Drive each step from `phoneMirrorManager.deviceState` / `deviceHint` so it advances on its own when the phone is seen.
   - Can be pure QML over the existing manager API; no app needed.
   - Overlaps with `TODO/phone-mirror-robustness.md` item 3 (the "no adb" wording); do them together.
2. **Phone data channel: notifications, calls, battery (optionally SMS, phone GPS).** **Effort:** L. **Device:** S22 or S26.
   - **Decide first: extend `phone_server/` or build a separate app.**
     - *Extend `phone_server/`* (shell-privileged, already running during a mirror session, already has a device-message channel: `DeviceMessage.java` types 100–104 and a bulk lane in `DeviceMessageSender.java`). Battery and call state are easy from the shell; notifications need a `NotificationListener`, which a shell process may not be able to register. Only works while a mirror session runs.
     - *Separate app* (`companion/`, Kotlin, minSdk 30): a real `NotificationListenerService`, `TelephonyCallback` and a foreground service with a persistent notification, so it works with no mirror session. Costs a second build, signing and release cycle (the permanent APK key from `build.yml`'s Sign APK step can be reused) and its own pairing (JSON over one TCP socket or WebSocket).
     - Spike: try `NotificationListener` access from the shell process in `phone_server` first. If it works, extend `phone_server`; if not, build the app.
   - OCTAVE side either way: new device-message types on the existing client (`scrcpyclient.cpp`, next to `kDevMsgOctave*`) or a new `src/managers/companionmanager.{h,cpp}`; a card in `frontend/settings/AccessoriesSettingsPage.qml`; a notification / incoming-call overlay in QML.
   - Calls and SMS need explicit user consent on the phone; say so in the UI.

## Recommended order

1. Setup walkthrough (independent, no new phone-side code).
2. Spike the shell-`NotificationListener` question, decide, then build the data channel.

Update `wiki/phone-mirror-android-auto.html` (and a new `wiki/companion-app.html` if a separate app is built) as each lands.

**Delete this file when** both items ship, or when the decision is not to build the data channel (then move the walkthrough to `TODO/phone-mirror-robustness.md` if it isn't done).
