# OCTAVE Companion app (Android)

**Status:** deferred — scoped, not started
**Last updated:** 2026-09-09

## What it is

A small Android app that pairs a phone with OCTAVE and streams the things adb
cannot see. It is **not** a replacement for the phone-side mirror server:
screen capture without a per-session consent dialog, creating a virtual
display, and injecting touch into other apps all require shell privileges,
which only an adb-launched process gets. The mirror stays on
`phone_server/` (our fork of the scrcpy server, launched over adb).

## Why it's parked

The mirror itself just landed (built-in client, forked server) and needs road
time first. The companion app is a separate product surface (Play Store or
sideload, its own release cycle, Android permission model) and should not be
bundled into the mirror work. Revisit once the mirror has been used in the car
for a few weeks and the wireless-connection pain is real rather than assumed.

## Scope, in priority order

1. **Wireless mirroring without a cable.** Android 11+ wireless debugging can
   replace USB after a one-time pairing. The app can show the pairing code,
   advertise the phone on the car's Wi-Fi (mDNS `_adb-tls-connect._tcp`), and
   OCTAVE's `PhoneMirrorManager` gains `adb pair` / `adb connect` handling and a
   "known phones" list. Needs: mDNS discovery on the OCTAVE side (Qt has
   `QMdnsEngine`-style options or use avahi on Linux), pairing UI on both ends.
2. **Phone data over the same link:** notifications (NotificationListener),
   calls and SMS (with the user's consent), now-playing metadata
   (MediaSession), battery, phone GPS as a fallback for the car's own. Simple
   JSON over one TCP socket or WebSocket; OCTAVE side is a new `CompanionManager`
   (both backends, per CLAUDE.md parity).
3. **Setup UX:** a first-run flow that walks the user through enabling USB /
   wireless debugging instead of the text block in `PhoneMirrorView.qml`.

## Constraints and gotchas

- MediaProjection is not worth using: consent dialog every session on Android
  14+, no virtual display, FLAG_SECURE blackout. Do not re-litigate this.
- Accessibility-service gesture injection works when sideloaded but Play Store
  policy restricts it; the mirror's control socket already covers touch.
- Background execution: a foreground service with a persistent notification
  is required for anything that streams while the screen is off.
- Wireless debugging must be re-enabled by the user after every phone reboot
  on most devices (it is a developer option, not a persistent state). The app
  can detect that and prompt; it cannot toggle it.

## Where the code would live

- `companion/` — Android app (Kotlin, Gradle, minSdk 30 for wireless debugging).
- `backend/companion_manager.py` + `src/managers/companionmanager.{h,cpp}`.
- `frontend/settings/AccessoriesSettingsPage.qml` — pairing card.
- Wiki: new `companion-app.html`, plus `phone-mirror-android-auto.html` for the
  wireless path.

## Prerequisites

- `phone_server/` fork building in CI and validated on the Pi (in progress).
- A decision on distribution (Play Store vs sideload) — Play Store implies
  signing keys and review; see `TODO/android-signing-keystore.md`.

## Order of operations

1. Wireless adb on the OCTAVE side alone (no app yet): `adb connect <ip>` with a
   manual pairing — proves the mirror works over Wi-Fi and measures latency.
2. Companion app v0: pairing helper + mDNS advertisement.
3. Phone data channel.
4. Setup UX.

Delete this file when the companion app ships (or when the decision is not to build it).
