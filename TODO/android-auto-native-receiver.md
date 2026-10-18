# Native Android Auto receiver (replace the DHU screen-scrape)

**Status:** deferred — scoped, no work started. Big subsystem (bigger than the phone-mirror client). Needs a wired test phone with Android Auto installed; no extra hardware.
**Last updated:** 2026-10-18

## Why this is parked here

Android Auto today (`src/managers/androidautomanager.{h,cpp}`, 787 lines) launches Google's Desktop Head Unit (DHU) as a subprocess, grabs its window with `QScreen::grabWindow()`, serves the frames to `frontend/AndroidAutoView.qml` through the `dhuframe` image provider, and forwards clicks back into the DHU window. That:

- needs the Android SDK and the DHU binary installed (desktop x86_64 only),
- has never worked on the Pi in the Jeep, and can't work on the Android build (`TODO/android-cpp-port.md` lists `androidautomanager` as a stub),
- is fragile (window focus, DPI, multi-monitor, X11 vs Wayland — see `TODO/kiosk-eglfs-pi.md`),
- has extra latency because it captures pixels instead of decoding the stream.

A native receiver makes OCTAVE itself the head unit: the phone talks the Android Auto Protocol (AAP) straight to OCTAVE over USB, sends H.264 video and PCM audio, and OCTAVE sends back touch, sensor and car data. The video and touch path already exists for phone mirroring, so most of the UI side is reuse.

It's parked because it's a large protocol implementation and it carries legal and platform risk (see **Risks**) that should be accepted on purpose, not discovered halfway through.

### Licensing (resolved)

OCTAVE moved to GPL-3.0-or-later on 2026-10-12 (commit `3352b92`). That makes the GPLv3 references usable directly: **aasdk** (C++, the library behind OpenAuto / Crankshaft) and its `.proto` definitions can be linked or copied. **Open Headunit** (`github.com/andreknieriem/headunit`, a revival of Mike Reid's `mikereidis/headunit`) is **AGPL-3.0**: read it, don't copy from it, unless OCTAVE goes AGPL too.

## Relationship to `TODO/carlinkit-dongle-port.md`

They complement each other. Neither replaces the other.

| | Native receiver (this plan) | Carlinkit dongle |
|---|---|---|
| Extra hardware | none (USB cable) | ~$60 dongle |
| Wired Android Auto | yes | yes |
| Wireless Android Auto | hard (see Risks), Phase 5 at best | yes, the dongle handles it |
| CarPlay | no | yes |
| Works in the Android build | yes (USB host on the tablet) | later, needs Android USB host too |
| Google certificate problem | ours to deal with | the dongle's |
| Video/touch reuse | phone-mirror path | phone-mirror path |

Both should feed the same QML view and decode path. Build whichever ships first so it's easy for the second to plug into: a small `ProjectionSource` interface (H.264 packets out, touch in, audio out) that DHU, the native receiver and the dongle all implement.

## How the protocol works (verify against aasdk before relying on it)

Taken from the aasdk and headunit sources. Byte-level details must be checked against aasdk while implementing, the same way `docs/PHONE_MIRROR_NATIVE_PLAN.md` holds the scrcpy layouts.

1. **USB accessory switch (AOA).** The phone first shows up as a normal USB device. The host sends AOA control requests: get protocol (51), six identification strings (52: manufacturer, model, description, version, URI, serial), then start (53). The phone re-enumerates as an accessory (Google VID `0x18D1`, PID `0x2D00`/`0x2D01`), with one bulk-in and one bulk-out endpoint.
2. **Framing.** Each frame has a channel id, a flags byte (first/last fragment, control vs. data, encrypted), and a length. The first fragment of a multi-fragment message also carries the total size. A message is a 2-byte message id followed by a protobuf payload.
3. **Handshake on the control channel.** Version request/response. Then a TLS handshake run *inside* AAP frames, so OpenSSL has to drive it through a memory BIO pair, not a socket. Then auth-complete. Everything after that is encrypted.
4. **Service discovery.** The phone asks what the head unit supports. OCTAVE answers with its channels: video (resolution, fps, DPI), media / speech / system audio, microphone, touch input, sensors (driving status, night mode, optionally GPS and speed), and optionally navigation status, media status and Bluetooth.
5. **Channel open + streaming.** The phone opens each channel. Video arrives as H.264 NAL units and needs a media ack per frame (flow control). If acks stall, the stream stops.
6. **Keep-alive.** Ping/pong on the control channel; the phone drops the session if it stops.

On the phone, Android Auto refuses uncertified head units unless **Unknown sources** is enabled in Android Auto's developer settings (tap the version 10 times). The onboarding UI has to say so.

## Scope

### C++ (leads)

- `src/android_auto/` (new, next to `src/phone_mirror/`):
  - `aoatransport.{h,cpp}` — device scan, AOA switch, bulk read/write on a worker thread. libusb on Linux/Pi/macOS/Windows; `UsbManager` accessory host via JNI behind `#ifdef Q_OS_ANDROID`.
  - `aapframer.{h,cpp}` — frame assembly/fragmentation, channel demux.
  - `aapcrypto.{h,cpp}` — OpenSSL with a memory BIO pair, client certificate + key.
  - `aapsession.{h,cpp}` — the control-channel state machine (version → TLS → auth → discovery → channel opens → ping).
  - `aapchannels.{h,cpp}` — video (hand NALs to the decoder, send acks), audio (to `QAudioSink`), input (touch out), sensors (driving status = parked/unrestricted so the keyboard isn't locked, night mode from the theme).
  - Protobuf: either link libprotobuf and the aasdk `.proto` files, or hand-encode the small set of messages actually used. Decide in Phase 0; the deciding factor is protobuf on Android/iOS.
- **Reuse from phone mirror:** the libavcodec H.264 decode → `QVideoSink` path (`src/phone_mirror/scrcpyclient.h`, `pushFrame` at line 155) and the touch mapping (`PhoneMirrorManager::injectTouch`). Pull the decoder out into a shared class instead of copying it.
- `androidautomanager` gains a native mode. Keep the class name and the `androidAutoManager` context property; DHU stays as a fallback behind a setting until the native path proves itself in the Jeep.
- New CMake deps: libusb-1.0 (desktop), OpenSSL (already bundled on Android in `android/openssl/` via `QT_ANDROID_EXTRA_LIBS`), and maybe protobuf. All optional with an `OCTAVE_HAVE_NATIVE_AA` flag, like `OCTAVE_HAVE_FFMPEG`.

### Settings

- `androidAutoBackend`: `"native"` | `"dhu"` (default `"dhu"` until Phase 3, then `"native"`).
- `androidAutoResolution`: `"800x480"` | `"1280x720"` | `"1920x1080"`.
- `androidAutoFps`: 30 | 60.
- `androidAutoDpi`: int.

Update `wiki/settings-reference.html`, `wiki/settings-manager.html` and `wiki/phone-mirror-android-auto.html` along with them.

## Risks

- **Google's head-unit certificate.** The phone only accepts a TLS client certificate chained to Google. Every open-source receiver ships one extracted from Google's own software (DHU or old head-unit firmware). It's a legal gray area, and Google can revoke it, which would break every user at once. Decide before Phase 1 whether OCTAVE ships one, loads one the user supplies from a file, or extracts it from a local DHU install at runtime. The last option keeps the key out of the repo and the release artifacts.
- **Wireless is getting harder.** Android Auto 17.4+ blocks the older ways of starting a wireless session (Open Headunit's README warns about it; rooted phones have a workaround). Wireless needs a Bluetooth RFCOMM handshake that hands Wi-Fi credentials to the phone, then TCP. Treat it as Phase 5 at best. The Carlinkit dongle is the dependable wireless answer.
- **Protocol churn.** Google changes the phone side without notice. Pin a known-good Android Auto version in the docs and log the version the phone reports, so breakage is diagnosable from the Diagnostics export.
- **USB permissions.** Linux needs a udev rule for the phone before and after the AOA switch (both VIDs). Ship it in `packaging/linux/` and the AppImage docs. Windows may need WinUSB bound to the accessory interface (Zadig-style). macOS should just work with libusb.
- **Pi performance.** 1080p60 decode on the Pi needs hardware decode. Start at 800x480/1280x720 at 30 fps, then measure against the phone-mirror numbers.

## Recommended order

0. **Spike (1–2 sessions).** Desktop Linux, libusb, a throwaway program: AOA switch, version exchange, TLS handshake, service discovery, then dump the first H.264 frames to a file and play them with ffplay. Settle the certificate and protobuf questions here. Stop if the handshake can't be made to work.
1. **Wired video, C++, desktop Linux.** Real classes in `src/android_auto/`, shared decoder pulled out of the phone-mirror code, video into `AndroidAutoView.qml`, frame acks, ping. DHU stays the default.
2. **Touch, audio, sensors.** Touch out, media/speech/system audio through the volume system (`VolumeController::applyVolume`, never a separate volume path), driving status, night mode from `EnvironmentTheme`. Microphone for Assistant last.
3. **Pi + Jeep.** udev rule, hardware decode, test in the car, then flip `androidAutoBackend` to `"native"`. Diagnostics export logs the session (phone model, AA version, negotiated resolution).
4. **Windows/macOS** USB setup and docs.
5. **Android build** (`UsbManager` accessory host, see `TODO/android-cpp-port.md` Phase 4). Wireless only if it's still feasible by then.

Update `wiki/phone-mirror-android-auto.html`, `wiki/signals-slots-reference.html`, `wiki/hardware-setup.html` (udev, phone developer settings) and `README.md` as each phase lands.

**Delete this file when** the native receiver is the default on desktop and the Pi, DHU is either removed or explicitly kept as a documented fallback, and any leftover work (wireless, Android build) has its own smaller TODO.
