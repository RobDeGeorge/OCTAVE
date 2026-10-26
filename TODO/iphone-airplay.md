# iPhone over AirPlay: the checks that need an iPhone

**Status:** active. Code done on the desktop 2026-10-25 (receiver, Bluetooth mouse, page, settings, `tst_airplay`); nothing has met an iPhone yet.
**Last updated:** 2026-10-25

**Goal:** an iPhone mirrors to OCTAVE and is driven from OCTAVE's screen, with no app, no developer mode and no extra hardware.

What exists: `airPlayManager` (`src/managers/airplaymanager.{h,cpp}`), the receiver over UxPlay's library (`src/airplay/airplayreceiver.{h,cpp}`, `third_party/uxplay/`), the Bluetooth LE mouse (`src/platform/bluezhidmouse.{h,cpp}`), the pointer path (`src/airplay/pointerpath.{h,cpp}`), `frontend/AirPlayView.qml`, the iPhone card in Settings > Accessories > Phone Dock. Wiki: `wiki/phone-mirror-android-auto.html#airplay`. Checked without a phone: mDNS answers for `_airplay._tcp` / `_raop._tcp`, RTSP `/info` (also in `tst_airplay`), the page and settings screenshots, the mouse registering with bluetoothd and leaving the adapter as it was.

Why parked here: every step below needs an iPhone on the same network as the desktop (and later the Pi).

## Steps, in order

1. **Mirroring on the desktop.** **Effort:** S. **Device:** desktop Linux + iPhone on the same Wi-Fi.
   Turn on AirPlay; Control Center > Screen Mirroring > OCTAVE; type the PIN; the page opens and shows the screen; portrait and landscape; stop from the iPhone; reconnect without a PIN; *Forget paired iPhones* asks again. Run with `QT_LOGGING_RULES="octave.airplay*=true"` and keep the log.
2. **Audio.** **Effort:** S. **Device:** same.
   Music to OCTAVE as a speaker (ALAC) and the sound while mirroring (AAC-ELD): it plays, the iPhone's slider and OCTAVE's volume both work, mute works, the title / artist show. If AAC-ELD fails to decode, check the AudioSpecificConfig in `airplayreceiver.cpp` (`kAacEldConfig`).
3. **Touch.** **Effort:** M. **Device:** same.
   *Pair touch*, pair "OCTAVE Touch" in the iPhone's Bluetooth settings, AssistiveTouch on. Then: does a tap land where it should (calibrate first); does a drag scroll; does the wheel scroll; do the pointer's homing reports (`PointerPath::kHomeReports` = 40) reach the corner; how long a tap takes (8 ms per report, `kStep` = 12). Tune `kStep` / the report interval / the default `airPlayTouchScale` (600) from what is seen, and the calibration if the pointer doesn't show in the mirrored picture.
   Known risk (`bluezhidmouse.cpp`): after OCTAVE restarts, a bonded iPhone may reconnect without subscribing to the input report again, so `touchState` stays `waiting`. If so, find what iOS does on reconnect (it may need the Service Changed indication or a re-read) and fix it there.
4. **Music source rules.** **Effort:** S. **Device:** desktop + iPhone.
   AirPlay audio is outside `nowPlaying` today, so local music keeps playing under it. Add it as a source (`"airplay"`) on the rules in `TODO/media-source-arbiter.md`: picking OCTAVE on the iPhone is a user action, so local pauses for music (ALAC / AAC-LC), and mirroring's own sound (AAC-ELD) probably mixes or ducks. Decide by ear in step 2.
5. **The Pi.** **Effort:** M. **Device:** Orange Pi 5 Plus + iPhone on the phone's hotspot.
   libplist on the Pi's build (`libplist-dev`), the AppImage bundling it, software decode at 1280x720 at 30 fps on the RK3588 (hardware decode is `TODO/phone-mirror-robustness.md` item 6), BlueZ's version for the mouse (advertising `Discoverable` needs 5.53+), and the network: the Pi on the iPhone's hotspot, mDNS reaching it.
6. **macOS / Windows.** **Effort:** M. **Device:** a Mac, a Windows PC, an iPhone. UxPlay builds on both (Bonjour on Windows); the mouse is BlueZ-only, so touch there needs a CoreBluetooth / WinRT peripheral. Only if wanted.

When step 5 passes, flip `airPlayEnabled` to on by default? Decide then: it advertises the car on whatever network it joins (the PIN still guards it).

Related: `TODO/carplay-native.md` (CarPlay, separate), `TODO/media-source-arbiter.md` (step 4), `TODO/phone-mirror-robustness.md` (hardware decode).

Delete this file (and its README row) when steps 1-5 are done.
