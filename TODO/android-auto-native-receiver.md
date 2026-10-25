# Native Android Auto receiver (replace the DHU screen-scrape)

**Goal:** let a phone run Android Auto straight into OCTAVE, first over the adb connection phone mirroring already uses, later over a plain USB accessory link, with no Google Desktop Head Unit and no extra hardware.
**Status:** in progress on branch `android-auto-native` — the adb transport is proven end to end in a Python spike (2026-10-25); C++ next. Big subsystem (bigger than the phone-mirror client). Needs a test phone with Android Auto installed and USB debugging on; no extra hardware.
**Last updated:** 2026-10-25

## Why this is parked here

Android Auto today (`src/managers/androidautomanager.{h,cpp}`) launches Google's Desktop Head Unit (DHU) as a subprocess, grabs its window with `QScreen::grabWindow()`, serves the frames to `frontend/AndroidAutoView.qml` through the `dhuframe` image provider, and forwards clicks back into the DHU window. That:

- needs the Android SDK and the DHU binary installed (desktop x86_64 only),
- has never worked on the Jeep's head unit (Orange Pi 5 Plus, arm64), and can't work on the Android build (`TODO/android-cpp-port.md` lists `androidautomanager` as a stub),
- is fragile (window focus, DPI, multi-monitor, X11 vs Wayland),
- has extra latency because it captures pixels instead of decoding the stream.

A native receiver makes OCTAVE itself the head unit: the phone talks the Android Auto Protocol (AAP) straight to OCTAVE, sends H.264 video and PCM audio, and OCTAVE sends back touch, sensor and car data. The video and touch path already exists for phone mirroring, so most of the UI side is reuse.

**Common prerequisite:** an extracted shared H.264 decoder class (pulled out of `src/phone_mirror/scrcpyclient.cpp:764` onward, `pushFrame` at `scrcpyclient.h:188` / `scrcpyclient.cpp:1228`) is needed here, by `TODO/carlinkit-dongle-port.md` and by `TODO/phone-mirror-android-host.md`; whichever starts first extracts it.

It's parked because it's a large protocol implementation and it carries legal and platform risk (see **Risks**) that should be accepted on purpose, not discovered halfway through.

### Licensing (resolved)

OCTAVE moved to GPL-3.0-or-later on 2026-10-12 (commit `3352b92`). That makes the GPLv3 references usable directly: **aasdk** (C++, the library behind OpenAuto / Crankshaft) and its `.proto` definitions can be linked or copied. **Open Headunit** (`github.com/andreknieriem/headunit`, a revival of Mike Reid's `mikereidis/headunit`) is **AGPL-3.0**: read it, don't copy from it, unless OCTAVE goes AGPL too.

## Verified so far (2026-10-25)

Python spike (`dev/aa-spike/`, gitignored) against an SM-S948U with Android Auto 17.9.664044 over wireless adb: the aasdk certificate is accepted, service discovery with video + three audio sinks + mic + touch + sensors opens every channel, 1280x720 H.264 and 48 kHz media PCM stream, bye-bye ends cleanly. Byte layouts and the message flow are in `docs/ANDROID_AUTO_PROTOCOL.md`. Two surprises that change the plan:

- The head unit server can't be started over adb any more (`startHeadunitServer()` in `androidautomanager.cpp` fails both ways on 17.9). The user turns on Android Auto developer mode and taps *⋮ > Start head unit server* once; it then serves session after session and survives USB unplug. Onboarding has to walk through that.
- Android Auto refuses a head unit without an audio sink and a microphone source, so audio and mic channels are part of Phase 1, not Phase 2.

## Two transports, one session

Why not push a dex the way phone mirroring does? The mirroring jar runs under `app_process` as the adb shell user, which may capture the screen and inject input, so the phone has nothing to opt into. Android Auto is different: the Android Auto app only draws its car UI after a complete AAP session with a head unit, including TLS with a Google-chained client certificate. A shell-user dex can't fake that without root, and mirroring Maps onto a virtual display is phone mirroring, not Android Auto. So nothing new runs on the phone; the phone-side half is the Android Auto app itself.

What carries over from mirroring is the **transport**. Android Auto's developer mode has a *head unit server* (`com.google.android.projection.gearhead/.companion.DeveloperHeadUnitNetworkService`) that listens on phone port 5277. The DHU is just an AAP client on that socket, reached through `adb forward tcp:5277 tcp:5277`. `androidautomanager.cpp` already starts the server over adb (`startHeadunitServer()`, `am start` with `START_HEAD_UNIT_SERVER`, then `am startservice`), checks it with `dumpsys` (`isHeadunitServerRunning()`) and sets up the forward (`setupAdbForward()`). OCTAVE can speak AAP on that socket itself and drop the DHU.

| | **adb transport** (Phases 0–3) | **USB accessory (AOA) transport** (Phase 5) |
|---|---|---|
| Link | `adb forward tcp:5277` → `QTcpSocket` | libusb bulk endpoints after the AOA switch |
| Phone setup | USB debugging + Android Auto developer mode + head unit server (auto-started over adb) | Android Auto "Unknown sources" only |
| Host setup | none beyond the adb mirroring already ships (`tools/platform-tools/`) | udev rule for both USB IDs (Linux), WinUSB/Zadig (Windows) |
| Wireless | yes, over wireless adb, same as mirroring | no (wireless AA is a separate RFCOMM + Wi-Fi handshake) |
| Who it's for | the Jeep, developers, this repo's owner | end users with a stock phone |
| Android-tablet host | no (needs an adb client on the tablet) | yes (`UsbManager` accessory host) |

Both transports feed the same `AapSession`: it takes bytes in and gives bytes out and never knows which link it's on. That makes it testable against a fake phone in a QtTest with no device at all.

## Relationship to `TODO/carlinkit-dongle-port.md`

They complement each other. Neither replaces the other.

| | Native receiver (this plan) | Carlinkit dongle |
|---|---|---|
| Extra hardware | none (USB cable, or wireless adb) | ~$60 dongle |
| Wired Android Auto | yes | yes |
| Wireless Android Auto | over wireless adb (developer path); real wireless AA hard (see Risks) | yes, the dongle handles it |
| CarPlay | no | yes |
| Works in the Android build | AOA transport only (USB host on the tablet) | later, needs Android USB host too |
| Google certificate problem | ours to deal with | the dongle's |
| Video/touch reuse | phone-mirror path | phone-mirror path |

Both should feed the same QML view and decode path. Build whichever ships first so it's easy for the second to plug into: a small `ProjectionSource` interface (H.264 packets out, touch in, audio out) that DHU, the native receiver and the dongle all implement.

## How the protocol works (verify against aasdk before relying on it)

Taken from the aasdk and headunit sources. Byte-level details must be checked against aasdk while implementing and written down in a new `docs/ANDROID_AUTO_PROTOCOL.md`, the same way `docs/PHONE_MIRROR_NATIVE_PLAN.md` holds the scrcpy layouts.

1. **Link.** adb transport: TCP to the forwarded port 5277, AAP starts at step 2. AOA transport: the phone first shows up as a normal USB device; the host sends AOA control requests (get protocol 51, six identification strings 52, start 53) and the phone re-enumerates as an accessory (Google VID `0x18D1`, PID `0x2D00`/`0x2D01`) with one bulk-in and one bulk-out endpoint.
2. **Framing.** Each frame has a channel id, a flags byte (first/last fragment, control vs. data, encrypted), and a length. The first fragment of a multi-fragment message also carries the total size. A message is a 2-byte message id followed by a protobuf payload. (Check whether the TCP head unit server uses the same framing byte for byte; the DHU suggests it does.)
3. **Handshake on the control channel.** Version request/response. Then a TLS handshake run *inside* AAP frames, so OpenSSL (3.x on this box) has to drive it through a memory BIO pair, not a socket. Then auth-complete. Everything after that is encrypted.
4. **Service discovery.** The phone asks what the head unit supports. OCTAVE answers with its channels: video (resolution, fps, DPI), media / speech / system audio, microphone, touch input, sensors (driving status, night mode, optionally GPS and speed), and optionally navigation status, media status and Bluetooth.
5. **Channel open + streaming.** The phone opens each channel. Video arrives as H.264 NAL units and needs a media ack per frame (flow control). If acks stall, the stream stops.
6. **Keep-alive.** Ping/pong on the control channel; the phone drops the session if it stops.

On the phone, Android Auto refuses uncertified head units unless developer mode is on (tap the version 10 times) and **Unknown sources** is enabled. The adb transport also needs the head unit server running, which OCTAVE starts over adb. The onboarding UI has to say so.

## Scope

### C++ (leads)

- `src/android_auto/` (new, next to `src/phone_mirror/`):
  - `aaptransport.h` — the interface: `write(QByteArray)`, `bytesReceived(QByteArray)`, `closed()`.
  - `adbtransport.{h,cpp}` — start the head unit server, `adb forward tcp:5277`, `QTcpSocket`. Moves the adb code out of `androidautomanager.cpp` and reuses the adb path / device selection phone mirroring uses (wireless adb included).
  - `aoatransport.{h,cpp}` — (Phase 5) device scan, AOA switch, bulk read/write on a worker thread. libusb on Linux/Pi/macOS/Windows; `UsbManager` accessory host via JNI behind `#ifdef Q_OS_ANDROID`.
  - `aapframer.{h,cpp}` — frame assembly/fragmentation, channel demux. Pure, unit-tested.
  - `aapcrypto.{h,cpp}` — OpenSSL with a memory BIO pair, client certificate + key loaded at runtime (see Risks).
  - `aapproto.{h,cpp}` — the handful of protobuf messages actually used, hand-encoded with a tiny wire-format reader/writer (varint, length-delimited). No libprotobuf: it isn't installed here, and it's a burden on Android/iOS/WASM. Field numbers from aasdk's `.proto` files, cited per message.
  - `aapsession.{h,cpp}` — the control-channel state machine (version → TLS → auth → discovery → channel opens → ping), transport-agnostic.
  - `aapchannels.{h,cpp}` — video (hand NALs to the decoder, send acks), audio (to `QAudioSink`), input (touch out), sensors (driving status = parked/unrestricted so the keyboard isn't locked, night mode from the theme).
- **Reuse from phone mirror:** the libavcodec H.264 decode → `QVideoSink` path and the touch mapping (`PhoneMirrorManager::injectTouch`, `phonemirrormanager.h:330`). Pull the decoder out into a shared class instead of copying it.
- `androidautomanager` gains a native mode. Keep the class name and the `androidAutoManager` context property; DHU stays as a fallback behind a setting until the native path proves itself in the Jeep. New public members are `Q_CLASSINFO("octave.experimental", ...)` and go through `--dump-api` / `check_compat.py` per CLAUDE.md.
- CMake: everything behind `OCTAVE_HAVE_NATIVE_AA` (needs OpenSSL + FFmpeg; off on WASM), like `OCTAVE_HAVE_FFMPEG`. libusb-1.0 (1.0.30 here) only once Phase 5 starts.

### Tests (no phone needed)

- `tst_aapframer` — fragment/reassemble round trips, channel demux, malformed lengths.
- `tst_aapproto` — encode/decode each message against byte strings taken from aasdk.
- `tst_aapsession` — the session against a scripted fake phone over an in-memory transport, up to the TLS step; TLS itself against a local OpenSSL server with a self-signed cert.
- Phone mirroring's existing tests must still pass after the decoder extraction.

### Settings

- `androidAutoBackend`: `"native"` | `"dhu"` (default `"dhu"` until Phase 3, then `"native"`).
- `androidAutoCertPath`: path to the head-unit certificate + key the user supplies.
- `androidAutoResolution`: `"800x480"` | `"1280x720"` | `"1920x1080"`.
- `androidAutoFps`: 30 | 60.
- `androidAutoDpi`: int.

Update `wiki/settings-reference.html`, `wiki/settings-manager.html` and `wiki/phone-mirror-android-auto.html` along with them.

## Risks

- **Google's head-unit certificate.** The phone only accepts a TLS client certificate chained to Google. Every open-source receiver ships one extracted from Google's own software (DHU or old head-unit firmware). It's a legal gray area, and Google can revoke it, which would break every user at once. Decision so far: **OCTAVE never ships one.** It loads a cert + key from `androidAutoCertPath`, or extracts them from a local DHU install at runtime. Nothing lands in the repo or the release artifacts. The build box has no DHU installed (`~/Android/Sdk/extras/google/auto` is absent), so nothing past the TLS step can be tested until a cert is available.
- **Head unit server lifetime.** Unknown whether it survives an Android Auto update, a phone reboot or the app being killed, and whether `am startservice` from the shell user works on current Android Auto. `startHeadunitServer()` already tries both; log which path worked. If neither does, the user starts it from the Android Auto developer menu and the UI says so.
- **Wireless AA is getting harder.** Android Auto 17.4+ blocks the older ways of starting a wireless session (Open Headunit's README warns about it). Real wireless AA needs a Bluetooth RFCOMM handshake that hands Wi-Fi credentials to the phone, then TCP. Wireless adb sidesteps that for the developer path; for end users the Carlinkit dongle is the dependable wireless answer.
- **Protocol churn.** Google changes the phone side without notice. Pin a known-good Android Auto version in the docs and log the version the phone reports, so breakage is diagnosable from the Diagnostics export.
- **USB permissions (AOA only).** Linux needs a udev rule for the phone before and after the AOA switch (both VIDs). Ship it in `packaging/linux/` and the AppImage docs. Windows may need WinUSB bound to the accessory interface (Zadig-style). macOS should just work with libusb. The adb transport needs none of this.
- **Head-unit performance.** 1080p60 decode on the Orange Pi 5 Plus needs hardware decode (RK3588 `h264_rkmpp` / v4l2m2m, also wanted by `TODO/phone-mirror-robustness.md` item 6). Start at 800x480/1280x720 at 30 fps, then measure against the phone-mirror numbers.

## Recommended order

0. **Groundwork, no phone (one night).** **Effort:** L. **Device:** none.
   a. Extract the shared H.264 decoder from `scrcpyclient.cpp`; phone mirroring behaves the same and its tests pass. One commit.
   b. `docs/ANDROID_AUTO_PROTOCOL.md` from aasdk: framing, the message ids and protobuf fields used, the version/auth sequence.
   c. `aaptransport.h`, `aapframer`, `aapproto`, `aapcrypto`, `aapsession` with the fake-phone tests above, behind `OCTAVE_HAVE_NATIVE_AA`. Not wired into the UI.
1. **Spike against the phone over adb.** Done 2026-10-25 in Python (see *Verified so far*). Next: the same in C++ through `adbtransport`.
2. **Wired video, touch, audio, sensors.** **Effort:** L. **Device:** same. Video into `AndroidAutoView.qml` through the shared decoder, frame acks, ping, touch out, media/speech/system audio through the volume system (`VolumeController::applyVolume`, never a separate volume path), driving status, night mode from `EnvironmentTheme`. Microphone for Assistant last. DHU stays the default.
3. **Orange Pi 5 Plus + Jeep.** **Effort:** M–L. **Device:** Orange Pi 5 Plus in the Jeep + phone. Hardware decode, test in the car over the adb link the Jeep already uses for mirroring, then flip `androidAutoBackend` to `"native"`. Diagnostics export logs the session (phone model, AA version, negotiated resolution, which head-unit-server start path worked).
4. **Windows/macOS.** **Effort:** S–M. **Device:** Windows and macOS machines + phone. The adb transport needs no USB setup, so this is mostly docs and a smoke run.
5. **AOA transport.** **Effort:** L. **Device:** desktop Linux + phone, then the Android tablet. `aoatransport` (libusb, udev rule, WinUSB docs), then `UsbManager` on the Android build (`TODO/android-cpp-port.md`). This is what makes it work for users without developer mode. Wireless AA only if it's still feasible by then.

Update `wiki/phone-mirror-android-auto.html`, `wiki/signals-slots-reference.html`, `wiki/hardware-setup.html` (phone developer settings, udev for Phase 5) and `README.md` as each phase lands.

**Delete this file when** the native receiver is the default on desktop and the Orange Pi, DHU is either removed or explicitly kept as a documented fallback, and any leftover work (AOA transport, wireless, Android build) has its own smaller TODO.
