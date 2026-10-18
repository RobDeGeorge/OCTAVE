# Phone mirror robustness: what is left after 0.9.5

**Goal:** make phone mirroring start fast, recover cleanly and work on phones other than Samsung.
**Status:** deferred. Most items are small fixes; the rest need a non-Samsung phone, or are polish.
**Last updated:** 2026-10-18

## Why this is parked here

Phone mirroring (`src/phone_mirror/scrcpyclient.{h,cpp}`, `src/managers/phonemirrormanager.{h,cpp}`, `frontend/PhoneMirrorView.qml`, server fork in `phone_server/`) was audited on 2026-10-17. The 0.9.5 pass fixed every gap a new user on a common setup hits first (that list is in `wiki/phone-mirror-android-auto.html`, "Errors and recovery"). None of the items below affect the Jeep's normal use (Orange Pi 5 Plus + S22/S26), only start-up time, other phone brands, or polish. Items 3–6 came from 0.9.5 RC testing (formerly `TODO/esp32-port-and-firewall.md (formerly 0.9.5-rc-followups.md)` items 7, 5, 6, 2).

Dropped on purpose: a manual fallback when QR pairing times out without mDNS / with AP isolation. The 120 s timeout with no fallback was a deliberate choice; the Avahi fallback stays Linux-only.

## Remaining work, easiest first

1. **Pass `display_ime_policy=local` to the server.** **Effort:** S. **Device:** S22 or S26 in virtual-display (DeX-style) mode.
   - The server already parses it (`phone_server/src/main/java/org/octave/phoneserver/CleanUp.java:104-107`); add it to the option list in `scrcpyclient.cpp:555-575` when `displaySize` is non-empty.
   - Check that the on-screen keyboard now appears on the phone, not on the virtual display (or vice versa, whichever reads better in the car).
2. **Ignore touches that start in the letterbox.** **Effort:** S. **Device:** none (QML test).
   - `relPos()` in `PhoneMirrorView.qml:124-129` clamps to 0..1. Instead, drop a touch point whose *press* is outside `video.contentRect`, and keep clamping only for moves of a point that started inside.
   - Only affects phone-screen mode; the virtual display is sized to the view (no letterbox, comment at line 72).
   - Cover it in `tests/cpp/tst_phonemirrorview.cpp`.
3. **"No adb" hint is developer-facing.** **Effort:** S. **Device:** none (Pi OS VM to confirm wording).
   - `getInstallInstructions()` (`phonemirrormanager.cpp:101-107`) and `describeDeviceState("no-adb")` (`:436`) say "run scripts/fetch_platform_tools.py or install android-tools". Release users have no `scripts/`, and Debian's package is `adb`.
   - Reword to "Install adb (on Raspberry Pi OS / Debian: sudo apt install adb)"; keep the script hint only when running from a source checkout.
4. **A dead Wi-Fi transport in the phone picker costs ~50 s.** **Effort:** S. **Device:** Orange Pi + S26 (move the phone to another network).
   - After the phone changes network, adb keeps listing the old `ip:5555` as `device`. Picking it: pkill/getprop time out (13 s), `adb push` times out (30 s), then the stale-retry. Spinner ran 49.6 s (2026-10-17).
   - Option: probe each network transport (`adb -s X shell true`, 2 s) before listing it as usable in `devices`.
   - Option: on a network serial, treat a prep timeout as stale (`adb disconnect`, re-probe, pick again) and use a ~10 s push timeout.
5. **Reattach is keyed on time, not on the phone.** **Effort:** S. **Device:** two Android phones (unit-testable with the fake in `tst_phonemirror.cpp` first).
   - Since `c0898bc` reattach works over any transport. If phone A drops and phone B is auto-picked within the ~55 s persist window, the manager tries A's scid on B, fails after the 4 s attach deadline, then starts fresh. It never lands on the wrong phone, but costs 4 s.
   - Fix: record `getprop ro.serialno` in the start prep (next to the sdk getprop) and reattach only when it matches. mDNS serials embed it; ip:port serials don't, so probe rather than parse.
6. **Reattach after a USB reset wastes ~4 s.** **Effort:** S. **Device:** S26 (or any phone that re-enumerates twice after `adb tcpip`).
   - With `scrcpyAutoWifi` on, the first plug-in after a phone boot runs `adb tcpip`; USB re-enumerated twice (adbd restart, then again ~4 s later). The second reset killed the new session and its server; the manager then tried to reattach to that dead server (`onDisconnected` persist at `phonemirrormanager.cpp:974`, `continueStart` attach at `:755`), so plug-to-picture took 14.4 s.
   - Options: skip the reattach when the USB transport changed (transport_id from `adb devices -l` differs); or after tcpip wait until the device has been stable ~3 s; or cap the attach deadline on USB at ~1.5 s.
7. **Test: persist / re-attach against the fake server.** **Effort:** S–M. **Device:** none.
   - Add the attach flow (`octave_persist_ms`, logcat follow) to `tests/cpp/tst_scrcpyclient.cpp`. The manager side is already covered by `tst_phonemirror.cpp`'s `cablePullReattachesOverWifiToTheSamePhone`. Do this before or with items 5 and 6.
8. **iPhone detection off Linux.** **Effort:** S–M. **Device:** an iPhone on a Windows and a macOS machine (kora VMs with USB passthrough).
   - `deviceHint` reads sysfs only (`phonemirrormanager.cpp:295-303`, vendor `05ac`), so Windows and macOS just say "No Android device". Use SetupAPI (Windows) / IOKit (macOS) for the vendor ID; no USB library needed.
9. **Host keyboard and text input.** **Effort:** M. **Device:** USB keyboard on the Orange Pi + S22/S26.
   - Send the scrcpy `INJECT_TEXT` control message (type 1; `scrcpyclient.cpp:66-73` has only 0, 2, 10, 100–103) from QML key events in `PhoneMirrorView.qml`, plus keycodes for Enter/Backspace/arrows.
   - Optional, same session: clipboard sync. Today `clipboard_autosync=false` (`scrcpyclient.cpp:558`) and incoming clipboard messages (`:926-931`) are ignored.
10. **Virtual display on non-Samsung phones.** **Effort:** M. **Device:** a Pixel (and ideally Android 11–13).
    - The Back/Home/Recents row is hidden in virtual-display mode on the assumption that a DeX-style taskbar exists (`PhoneMirrorView.qml:150,155`). Show it unless a taskbar is detected, or make it a setting.
    - If no launcher draws within a few seconds, fall back to phone-screen mode (today it only launches the launcher, `phonemirrormanager.cpp:692`).
    - Note: the `TRUSTED` flag is only set on API 33+ (`phone_server/.../video/NewDisplayCapture.java:185`).
11. **Slow links.** **Effort:** L. **Device:** Orange Pi 5 Plus (RK3588) over Wi-Fi.
    - Let adaptive bit rate lower `max_fps` and size once it hits the 2 Mbps floor (`kAdaptiveMinBps`, `phonemirrormanager.h:202`; `src/phone_mirror/adaptivebitrate.h` only changes bit rate).
    - Try hardware decode on the RK3588 (`h264_rkmpp` / v4l2m2m). Best done after the shared H.264 decoder class is extracted from `scrcpyclient.cpp` (see `TODO/phone-mirror-android-host.md`, `TODO/android-auto-native-receiver.md`).

## Hardware checks still open

- From `docs/PHONE_MIRROR_NATIVE_PLAN.md`: multi-finger gestures, cable pull, and starting with nothing plugged in.
- Not yet tried on a real phone: the 0.9.5 encoder fallback and the Xiaomi notice (tested only against log lines).
- A second phone brand (Pixel, Xiaomi), and Android 11–13 if one can be borrowed.

## Recommended order

Items 1–7 in order (one session, mostly no extra hardware), then 8 and 9, then 10 once a Pixel is available, then 11 when there's a reason to.

Related: `TODO/phone-mirror-android-host.md` (tablet as host), `TODO/octave-companion-app.md` (setup walkthrough and phone data), `TODO/android-auto-native-receiver.md` (shares the decoder and touch path), `TODO/future-tests.md`.

**Delete this file when** items 1–10 are done and anything left has moved to a smaller TODO.
