# Carlinkit Dongle Port (CarPlay + wireless Android Auto)

**Goal:** support CarPlay and wireless Android Auto through a Carlinkit USB dongle that handles the phone side.
**Status:** deferred — large new subsystem, no work started. Hardware ($60 dongle) required to develop and test.
**Last updated:** 2026-10-18

## Why this is parked here

OCTAVE has no CarPlay support and its Android Auto path (`src/managers/androidautomanager.{h,cpp}`) launches Google's Desktop Head Unit subprocess and screen-scrapes its window with `QScreen::grabWindow()`. That:

- only works on desktop (DHU is x86_64 + needs Android SDK installed),
- is fragile (window focus, multi-monitor, DPI scaling all break it),
- has never worked on the Jeep's head unit (Orange Pi 5 Plus, arm64),
- can't do CarPlay at all (no Apple-sanctioned SDK exists for non-automotive Linux).

The **Carlinkit CPC200-CCPA** (wireless + wired) and **CCPW** (wired only) USB dongles solve both problems simultaneously. The dongle holds Apple's MFi certification and Google's AA certification on its own silicon — the phone's contract is with the dongle, not with OCTAVE. From the host app's perspective it's just a libusb device exposing an H.264 video stream, PCM audio, and a touch-input return channel over a documented (reverse-engineered) protocol.

The reference implementation we'd port from is **LIVI** (`f-io/LIVI`, MIT licensed), specifically:

- `src/main/services/projection/driver/dongle/dongleDriver.ts` — 775 lines, the full protocol state machine
- `src/main/services/projection/messages/{common,readable,sendable}.ts` — message framing (`MessageHeader` + typed payloads)
- `src/main/services/usb/{constants,helpers,USBService,USBWorker}.ts` — USB device matching + worker thread
- `src/main/services/projection/services/ProjectionAudio.ts` — audio output handling
- LIVI's prior art chain (cited in `CREDITS.md`): **node-carplay** (Rhys Morgan), **ludwig-v/wireless-carplay-dongle-reverse-engineering** (Ludwig), **lvalen91/CPC200-CCPA-Firmware-Dump** (Tachi91), **aasdk** (f1xpl for the AA protocol)

A local copy is at `~/Downloads/LIVI-main/` — keep a snapshot before deleting it.

**Shared decoder (done 2026-10-25):** `src/util/h264decoder.{h,cpp}` (libavcodec, SPS/PPS prepend, `AVFrame` → `QVideoFrame`), extracted from `scrcpyclient.cpp`; phone mirroring and the native Android Auto receiver (`TODO/android-auto-native-receiver.md`) use it.

**See also:** `TODO/android-auto-native-receiver.md`, the no-hardware wired alternative. Both should feed one shared projection source (H.264 out, touch in, audio out) so whichever lands second just plugs in.

## Scope

A new C++ manager `src/managers/carlinkitmanager.{h,cpp}` exposed to QML as `carlinkitManager`. It is mobile-eligible: Android sideload could use it once Android USB host mode + permissions are wired.

Replaces `androidautomanager` for the wireless case but **keep DHU as a fallback** behind a setting (`useDHUForAndroidAuto`) until the dongle path proves stable in the Jeep.

### Public API the QML expects

Modeled on the existing `AndroidAutoManager` and `PhoneMirrorManager` so `MediaRoom.qml` / future `CarPlay.qml` can reuse the video path:

```cpp
Q_PROPERTY(bool dongleConnected READ ...)
Q_PROPERTY(QString phoneMode READ ...)          // "carplay" | "androidauto" | "none"
Q_PROPERTY(QString phoneName READ ...)          // last-paired BT name from BoxInfo
Q_INVOKABLE void startProjection();
Q_INVOKABLE void stopProjection();
Q_INVOKABLE void sendTouch(qreal x, qreal y, int action);  // down/move/up
Q_INVOKABLE void sendKey(int command);                     // siri, home, prev/next, etc.
Q_INVOKABLE void setVideoSink(QVideoSink *sink);           // the QML VideoOutput's sink, as in phone mirror
signals:
    void dongleStateChanged();
```

No image provider: push decoded `QVideoFrame`s to the `QVideoSink` of a QML `VideoOutput`, the way phone mirror does (`ScrcpyClient::pushFrame`, `scrcpyclient.h:169`). The `dhuframe` image provider (`src/main.cpp`) is the DHU screen-scrape's legacy, not a pattern to copy.

## Prerequisites

1. **Hardware** — buy a Carlinkit CPC200-CCPA. Don't start work without one; the dongle protocol can't be unit-tested without a real device + paired phone.
2. **libusb dependency** — add `libusb-1.0` to `CMakeLists.txt` (`pkg_check_modules(LIBUSB libusb-1.0)`). Already a transitive dep on most Linux distros; needs vcpkg or homebrew formula on Windows/macOS.
3. **H.264 decode path** — settled: libavcodec → `QVideoFrame` → `QVideoSink` already ships in phone mirror (`OCTAVE_HAVE_FFMPEG`, `CMakeLists.txt:86-110`). Reuse it through the shared decoder class above instead of copying it.
4. **udev rule for Linux** — the dongle needs a `/etc/udev/rules.d/99-carlinkit.rules` granting non-root access. LIVI prompts to install it on first launch via pkexec — port that UX into a new `frontend/settings/CarPlaySettingsPage.qml`. The same pattern applies to the ESP32 volume knob's serial port on Linux (`src/managers/esp32volumemanager.cpp`), and `packaging/linux/51-android.rules` is the existing example of a shipped udev rule.
5. **Read the Ludwig reverse-engineering writeup first** — `github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering` documents the exact message types and ordering that `dongleDriver.ts` implements. Saves a week of guessing.

## Order of operations

1. **Spike (1 day)** — **Effort:** M. **Hardware:** CPC200-CCPA dongle + an iPhone (or Android phone), desktop Linux. Write a throwaway C++ binary that opens the dongle by VID `0x1314` / PID `0x1520` or `0x1521`, sends the open message, and dumps incoming frames to stdout. Prove libusb + framing work before building a manager around them.
2. **Port the message protocol** — **Effort:** M. **Hardware:** none for the unit tests. `src/managers/carlinkit/messages.{h,cpp}` mirroring LIVI's `common.ts` + `readable.ts` + `sendable.ts`. Pure data classes, no I/O. Unit-testable.
3. **Port the driver state machine** — **Effort:** L. **Hardware:** dongle + phone. `src/managers/carlinkit/driver.{h,cpp}` mirroring `dongleDriver.ts`. Heartbeat timer, error counting, mode switching (CarPlay ↔ Android Auto), BoxInfo parsing. Runs on a worker `QThread` — do NOT block the GUI thread on USB I/O.
4. **H.264 decode + audio routing** — **Effort:** M (the shared decoder exists). **Hardware:** dongle + phone. Feed the NAL units to the shared decoder → `QVideoSink`; route audio through the existing `VolumeController` so the global volume curve applies (per `CLAUDE.md`, never call manager `setVolume` directly).
5. **QML surface** — **Effort:** M. **Hardware:** dongle + phone; Orange Pi 5 Plus touch screen. `frontend/CarPlay.qml` showing the video, top-bar status (phone mode + connection), and a touch overlay that calls `carlinkitManager.sendTouch(...)` with normalized coordinates. New `frontend/settings/CarPlaySettingsPage.qml` for resolution / FPS / mic type config + udev rule install button.
6. **Replace AA path** — **Effort:** M. **Hardware:** dongle + Android phone, then the Jeep. Once CarPlay is solid, plumb the same dongle into the Android Auto code path. Keep `androidautomanager` reachable behind a settings toggle (`mediaSettings.useDHUForAndroidAuto`) for ~1 release as a rollback.
7. **Wiki updates** — **Effort:** S. New `wiki/carplay-manager.html`, update `wiki/hardware-managers.html`, `wiki/hardware-setup.html`, `wiki/signals-slots-reference.html`, rebuild search index per `CLAUDE.md`.

## Things to watch out for

- **MFi compliance noise** — this is the question that always comes up. The dongle holds the MFi cert; OCTAVE talks to the dongle, not to the phone. Same legal posture as every other open-source CarPlay head-unit project on the planet. Add the same disclaimer LIVI uses (see their README) to OCTAVE's README and the in-app About page.
- **Android sideload (`TODO/android-cpp-port.md`)** — Android USB host mode + `UsbManager` permission flow is its own subproject. Get desktop working first; tackle Android USB after the sideload polish in `TODO/android-cpp-port.md` ships.
- **Heartbeat cadence matters.** The dongle drops the connection if heartbeats stall >2s. LIVI's `MAX_ERROR_COUNT = 5` and the `_heartbeatInterval` pattern are load-bearing — port them verbatim.

## Delete this file when

CarPlay is working in the Jeep on the Orange Pi 5 Plus for at least two weeks without manual intervention, the DHU fallback toggle has been removed, and the wiki pages above exist.
