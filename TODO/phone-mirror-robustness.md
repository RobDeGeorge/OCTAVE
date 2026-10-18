# Phone mirror robustness: what is left after 0.9.5

**Status:** deferred. The first-user gaps (Windows decoder, GUI-thread adb, error messages, auto-Wi-Fi default, encoder fallback, multi-phone picker, Python parity, iOS gating) shipped in 0.9.5. What is left needs a non-Samsung phone, or is polish.
**Last updated:** 2026-10-17

## Why this is parked here

Phone mirroring (`src/phone_mirror/scrcpyclient.{h,cpp}`, `src/managers/phonemirrormanager.{h,cpp}`, Python peers in `backend/phone_mirror/`, `frontend/PhoneMirrorView.qml`, server fork in `phone_server/`) was audited on 2026-10-17 for "anyone can plug in a phone". The 0.9.5 pass fixed every gap that a new user on a common setup hits first. The items below are either unverifiable without other hardware (a Pixel or Xiaomi, Android 11–13) or only matter on slow links. None of them affect the Jeep (Orange Pi + S22 Ultra).

Done in 0.9.5, for context. Both backends got the same behaviour, and it is documented in `wiki/phone-mirror-android-auto.html`, "Errors and recovery":
- Windows builds link FFmpeg (`find_path` / `find_library`), and `OCTAVE_REQUIRE_FFMPEG` is on in CI. `--smoke` logs `phone mirror native: 1`, which the Windows job checks.
- adb probes and start prep run on a probe thread. `deviceState` / `deviceStateChanged` replace polling, and `getDeviceState()` returns the cached value.
- New messages: `no-permissions` (plus `packaging/linux/51-android.rules`), the adb version clash and iPhone hints (`deviceHint`), and `mirrorNotice` (Xiaomi INJECT_EVENTS, no audio before Android 11, software encoder). The view no longer retries forever after an encoder failure.
- One retry with `c2.android.avc.encoder` after a hardware encoder failure.
- `scrcpyAutoWifi` defaults to off. Only the default changed; a saved `true` is kept.
- Phone picker (`devices`, `selectDevice()`).
- The bottom-bar button is gated on `nativeAvailable`.
- Python: a bad packet is skipped instead of ending the session, and non-YUV420P frames are dropped (as in C++).
- `adb devices` parsing skips the "* daemon" start-up lines; before, they showed up as a device called `*`.
- Tests: `tests/fake_scrcpy_server.py` + `test_scrcpy_fake_server.py` (Python client), `test_phone_mirror_probes.py`, `test_phone_mirror_view_qml.py`.

## Remaining gaps

1. **Virtual display on non-Samsung phones (M).** Needs a Pixel to verify.
   - The Back/Home/Recents row is hidden in virtual-display mode on the assumption that a DeX-style taskbar exists (`PhoneMirrorView.qml`, `virtualDisplay` in the nav `Row`). Show it unless a taskbar is detected, or make it a setting.
   - Pass `display_ime_policy=local`.
   - If no launcher draws within a few seconds, fall back to phone-screen mode.
   - Note: the `TRUSTED` flag is only set on API 33+ (`NewDisplayCapture.java:184`).
2. **Input (M).**
   - Host keyboard and text through `INJECT_TEXT` (already defined as `MSG_INJECT_TEXT` in Python, unused).
   - Ignore touches that start in the letterbox instead of clamping them (`relPos()` in `PhoneMirrorView.qml`).
   - Optional clipboard sync.
3. **Slow links (L).**
   - Let adaptive bit rate lower `max_fps` and size below the 2 Mbps floor.
   - Try hardware decode on the RK3588 (`h264_rkmpp` / v4l2m2m).
4. **Wi-Fi without mDNS / AP isolation (S).** QR pairing still times out after 120 s with no manual fallback, which was a deliberate choice. The Avahi fallback is Linux-only.
5. **iPhone detection off Linux (S).** `deviceHint` reads sysfs, so Windows and macOS just say "No Android device".

## Tests still missing

- The C++ client against `tests/fake_scrcpy_server.py`. There is no C++ test target yet; see `TODO/python-removal.md`, which plans one.
- Persist / re-attach flow in the fake server.

## Hardware checks still open

- From `docs/PHONE_MIRROR_NATIVE_PLAN.md`: multi-finger gestures, cable pull, and starting with nothing plugged in.
- Not yet tried on a real phone: the 0.9.5 encoder fallback and the Xiaomi notice. Both are tested only against log lines.
- Add a second phone brand (Pixel, Xiaomi), and Android 11–13 if one can be borrowed.

## Recommended order

1. Gap 1, once a Pixel is available.
2. Gaps 2 and 5.
3. Gaps 3 and 4 when there's a reason to.

Related: `TODO/phone-mirror-android-host.md` (tablet as host; its prerequisites from this file, async adb and nativeAvailable gating, are done), `TODO/android-auto-native-receiver.md` (shares the decoder and touch path), `TODO/future-tests.md`.

**Delete this file when** gaps 1 and 2 are done, and anything left has moved to a smaller TODO.
