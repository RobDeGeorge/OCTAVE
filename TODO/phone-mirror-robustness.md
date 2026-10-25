# Phone mirror robustness: what is left after 0.9.5

**Goal:** make phone mirroring start fast, recover cleanly and work on phones other than Samsung.
**Status:** deferred. Most items are small fixes; the rest need a non-Samsung phone, or are polish.
**Last updated:** 2026-10-18

## Why this is parked here

Phone mirroring (`src/phone_mirror/scrcpyclient.{h,cpp}`, `src/managers/phonemirrormanager.{h,cpp}`, `frontend/PhoneMirrorView.qml`, server fork in `phone_server/`) was audited on 2026-10-17. The 0.9.5 pass fixed every gap a new user on a common setup hits first (that list is in `wiki/phone-mirror-android-auto.html`, "Errors and recovery"). None of the items below affect the Jeep's normal use (Orange Pi 5 Plus + S22/S26), only start-up time, other phone brands, or polish. Items 1 and 2 came from 0.9.5 RC testing (items 7, 5, 6 and 2 of the 0.9.5 RC follow-ups file, now `TODO/windows-adb-firewall.md`).

Dropped on purpose: a manual fallback when QR pairing times out without mDNS / with AP isolation. The 120 s timeout with no fallback was a deliberate choice; the Avahi fallback stays Linux-only.

## Remaining work, easiest first

Done 2026-10-18: `display_ime_policy=local` for the virtual display, letterbox touches ignored, the "no adb" hint per platform, reattach keyed on `ro.serialno`, and client attach tests (`tst_scrcpyclient`, `tst_phonemirror`, `tst_phonemirrorview`). Still to check on a phone: the keyboard now opens on the mirrored display.

1. **A dead Wi-Fi transport in the phone picker costs ~50 s.** **Effort:** S. **Device:** Orange Pi + S26 (move the phone to another network).
   - After the phone changes network, adb keeps listing the old `ip:5555` as `device`. Picking it: pkill/getprop time out (13 s), `adb push` times out (30 s), then the stale-retry. Spinner ran 49.6 s (2026-10-17).
   - Option: probe each network transport (`adb -s X shell true`, 2 s) before listing it as usable in `devices`.
   - Option: on a network serial, treat a prep timeout as stale (`adb disconnect`, re-probe, pick again) and use a ~10 s push timeout.
2. **Reattach after a USB reset wastes ~4 s.** **Effort:** S. **Device:** S26 (or any phone that re-enumerates twice after `adb tcpip`).
   - With `scrcpyAutoWifi` on, the first plug-in after a phone boot runs `adb tcpip`; USB re-enumerated twice (adbd restart, then again ~4 s later). The second reset killed the new session and its server; the manager then tried to reattach to that dead server (`onDisconnected` persist at `phonemirrormanager.cpp:974`, `continueStart` attach at `:755`), so plug-to-picture took 14.4 s.
   - Options: skip the reattach when the USB transport changed (transport_id from `adb devices -l` differs); or after tcpip wait until the device has been stable ~3 s; or cap the attach deadline on USB at ~1.5 s.
3. **iPhone detection off Linux.** **Effort:** S–M. **Device:** an iPhone on a Windows and a macOS machine (kora VMs with USB passthrough).
   - `deviceHint` reads sysfs only (`phonemirrormanager.cpp:295-303`, vendor `05ac`), so Windows and macOS just say "No Android device". Use SetupAPI (Windows) / IOKit (macOS) for the vendor ID; no USB library needed.
4. **Host keyboard and text input.** **Effort:** M. **Device:** USB keyboard on the Orange Pi + S22/S26.
   - Send the scrcpy `INJECT_TEXT` control message (type 1; `scrcpyclient.cpp:66-73` has only 0, 2, 10, 100–103) from QML key events in `PhoneMirrorView.qml`, plus keycodes for Enter/Backspace/arrows.
   - Optional, same session: clipboard sync. Today `clipboard_autosync=false` (`scrcpyclient.cpp:558`) and incoming clipboard messages (`:926-931`) are ignored.
5. **Virtual display on non-Samsung phones.** **Effort:** M. **Device:** a Pixel (and ideally Android 11–13).
    - The Back/Home/Recents row is hidden in virtual-display mode on the assumption that a DeX-style taskbar exists (`PhoneMirrorView.qml:150,155`). Show it unless a taskbar is detected, or make it a setting.
    - If no launcher draws within a few seconds, fall back to phone-screen mode (today it only launches the launcher, `phonemirrormanager.cpp:692`).
    - Note: the `TRUSTED` flag is only set on API 33+ (`phone_server/.../video/NewDisplayCapture.java:185`).
6. **Slow links.** **Effort:** L. **Device:** Orange Pi 5 Plus (RK3588) over Wi-Fi.
    - Let adaptive bit rate lower `max_fps` and size once it hits the 2 Mbps floor (`kAdaptiveMinBps`, `phonemirrormanager.h:202`; `src/phone_mirror/adaptivebitrate.h` only changes bit rate).
    - Try hardware decode on the RK3588 (`h264_rkmpp` / v4l2m2m). Do it in the shared decoder (`src/util/h264decoder`), so phone mirroring and Android Auto both get it.

## Hardware checks still open

- From `docs/PHONE_MIRROR_NATIVE_PLAN.md`: multi-finger gestures, cable pull, and starting with nothing plugged in.
- Not yet tried on a real phone: the 0.9.5 encoder fallback and the Xiaomi notice (tested only against log lines).
- A second phone brand (Pixel, Xiaomi), and Android 11–13 if one can be borrowed.

## Recommended order

Items 1 and 2 with a phone on the Orange Pi, then 3 and 4, then 5 once a Pixel is available, then 6 when there's a reason to.

Related: `TODO/phone-mirror-android-host.md` (tablet as host), `TODO/octave-companion-app.md` (setup walkthrough and phone data), `TODO/android-auto-native-receiver.md` (shares the decoder and touch path), `TODO/future-tests.md`.

**Delete this file when** items 1–5 are done and anything left has moved to a smaller TODO.
