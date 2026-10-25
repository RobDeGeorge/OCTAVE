# Phone mirror with an Android tablet as the host

**Goal:** let OCTAVE's Android build, running on a tablet in the dash, mirror a phone the way the Orange Pi does today.
**Status:** deferred — scoped, no work started. Bigger than it looks: OCTAVE would need its own adb client.
**Last updated:** 2026-10-18

## Why this is parked here

Running OCTAVE's Android build on a tablet and mirroring a phone onto it is a natural setup: a cheap tablet in the dash instead of a Pi. Today it can't work, for two independent reasons:

1. **No decoder.** `CMakeLists.txt:87` only looks for FFmpeg under `if(NOT ANDROID)`, so the APK has no `OCTAVE_HAVE_FFMPEG`, and `ScrcpyClient::available()` is false.
2. **No adb.** The client drives everything by running the `adb` binary through `QProcess`: `devices`, `push`, `forward`, `shell app_process`, `pair`, `connect` (`phonemirrormanager.cpp:42` onward, `findAdb()` at line 117). An Android app can't ship and run adb this way, and wouldn't have the host side's USB or pairing state if it did.

Because of that, `phonemirrormanager` is a mobile stub (`phonemirrormanager.h:481` onward) and its `nativeAvailable` is false (`:545`), which hides the bottom-bar button. Everything above the transport carries over unchanged: the scrcpy protocol client, the `phone_server/` jar, touch mapping, MirrorKeeper, media/queue messages and the QML view.

It's parked because the adb client is a protocol implementation in its own right, and the Jeep runs the Orange Pi 5 Plus, not a tablet.

**Shared decoder (done 2026-10-25):** `src/util/h264decoder.{h,cpp}` (libavcodec, SPS/PPS prepend, `AVFrame` → `QVideoFrame`), extracted from `scrcpyclient.cpp`; phone mirroring and the native Android Auto receiver (`TODO/android-auto-native-receiver.md`) use it. A MediaCodec backend goes behind the same `H264Decoder` interface.

## What has to be built

### 1. An adb client inside OCTAVE (the big piece)

OCTAVE needs only a small slice of adb: connect + auth, `shell:` (for `app_process`, `pkill`, `getprop`), `sync:` SEND (push the jar), and a local stream to `localabstract:scrcpy_<scid>`. That last one replaces `adb forward`: on-device there's no forward, so the client opens the stream directly and hands it to `ScrcpyClient` as a socket-like `QIODevice`.

- **Wire protocol:** 24-byte message header (CNXN, AUTH, OPEN, OKAY, WRTE, CLSE, STLS), version and max-payload negotiation.
- **Auth:** an RSA key pair stored in the app's data dir. The phone shows its "Allow USB debugging?" prompt with the tablet's fingerprint, once.
- **Transports:**
  - **Wi-Fi (do first).** Wireless debugging on Android 11+ uses TLS (the STLS upgrade) after a one-time **pairing**. The phone's "Pair device with QR code" flow (the same QR that `PhoneMirrorManager` already draws with `qrcodegen`) advertises `_adb-tls-pairing._tcp`; the tablet finds it with `NsdManager` (JNI) and runs the SPAKE2 pairing handshake. SPAKE2 is in BoringSSL, not OpenSSL. Check what the bundled Android OpenSSL (`android/openssl/`, `QT_ANDROID_EXTRA_LIBS`) provides, and whether a small standalone SPAKE2 (e.g. the one in libadb-android) is easier. After pairing, find `_adb-tls-connect._tcp` and connect.
  - **USB.** The phone is a USB *device* and the tablet the *host*: `UsbManager` (JNI, `android/src/org/octave/app/`, next to `OctaveOBDBridge.java`), find the adb interface (class 0xFF / subclass 0x42 / protocol 0x01), then bulk endpoints. Needs `<uses-feature android:name="android.hardware.usb.host" android:required="false"/>` and a USB-permission flow. Risk: phone-to-tablet USB-C role negotiation is unreliable (both sides want to charge or to be host). An OTG adapter on the tablet end usually fixes it. Treat USB as second priority.
- **Reference implementations, reviewed for license (OCTAVE is GPL-3.0+ since `3352b92`):**
  - libadb-android (MuntashirAkon, GPL-3.0, Java): pairing and TLS; usable directly.
  - Google's `adb` sources (Apache-2.0): the protocol reference.
  - dadb (mobile.dev, Apache-2.0, Kotlin).
  - Easy Control / scrcpy-on-Android apps: proof that the whole chain works on-device. Check each one's license before copying anything.
- **Language:** either C++ over `QTcpSocket` / `QSslSocket` plus a JNI shim for USB and NSD, or a Java/Kotlin adb library behind a JNI bridge that hands back file descriptors. Decide in the spike. The deciding factor is SPAKE2 and how hard a TLS client certificate with a self-signed key is on Qt's Android SSL backend.

### 2. Decoder

Use `MediaCodec` (JNI) for hardware H.264. Decode either to a `Surface` backed by the `QVideoSink`'s texture, or to ByteBuffers wrapped as `QVideoFrame` (simpler; costs one copy). Don't bundle libavcodec for arm64: it's software decode and drains the tablet's battery. Pull the decode step behind a small interface so `ScrcpyClient` doesn't care which backend it has. `TODO/android-auto-native-receiver.md` uses the same shared decoder class.

### 3. Manager wiring

- Replace the mobile stub with the real `PhoneMirrorManager`, built on an `AdbTransport` interface: `QProcess` adb on desktop, the built-in client on Android.
- The bottom-bar button is already gated on `phoneMirrorManager.nativeAvailable` (0.9.5), so it appears once the Android build reports true.
- Audio: `QAudioSink` works on Android. Check that ducking goes through `MediaManager.setDucking()` the same way.
- Settings: none new expected.

### 4. Networking in the car

Wireless debugging needs the phone on Wi-Fi, and Android turns it off while the phone is itself the hotspot. So the **tablet** (or a car AP) has to host the network, the same constraint the Pi has today (QR pairing over the box's own AP, `f100f60`). Document it on the onboarding screen.

## Risks

- Android keeps tightening wireless debugging: it turns off on network change and needs re-pairing after some updates. Auto-reconnect has to rediscover the connect port over mDNS every session. The desktop code already handles this (mDNS lookup in `phonemirrormanager.cpp:1218-1274`, `adb connect` at `:1281`); port that logic.
- Background limits: if the tablet app goes to the background, Android may kill the sockets. That's acceptable for a head unit running in the foreground, but say so.
- Play Store policy on apps that act as adb hosts is unclear. Sideload first, as with the rest of the APK.

## Recommended order

Already done (0.9.5): async adb (probe thread, `deviceStateChanged`) and `nativeAvailable` gating; write the Android client against that async API.

1. **Shared decoder interface.** **Effort:** S. **Device:** none (desktop; regression-check on the Orange Pi + S22). The libavcodec path is already `src/util/h264decoder`; give it a backend switch so a MediaCodec implementation can slot in.
2. **Spike (1–2 sessions).** **Effort:** L. **Device:** desktop Linux + S22/S26 with Wireless debugging. Built-in adb client over TCP, pairing + TLS + `shell:` + `sync:` + a `localabstract:` stream to a running phone server. This proves the protocol without Android packaging in the way.
3. **`AdbTransport` on desktop.** **Effort:** M. **Device:** Orange Pi 5 Plus + S22/S26 for a soak test. Keep `QProcess` adb as the default, with the built-in client behind an env var, so the Pi can soak-test it.
4. **Android build.** **Effort:** L. **Device:** an Android tablet + S22/S26. NSD + pairing UI, MediaCodec decode, un-stub the manager. Test on the tablet and the S22 (never inject adb taps unless OCTAVE is the foreground app).
5. **USB host transport.** **Effort:** L. **Device:** tablet + phone + USB-C OTG adapter.
6. **Optional.** **Effort:** S–M. **Device:** Orange Pi 5 Plus. Make the built-in client the default on desktop too, which drops the bundled adb and the arm64 adb gap.

Update `wiki/phone-mirror-android-auto.html`, `wiki/hardware-setup.html` (tablet setup, hotspot) and the "Hardware managers on Android" section of `TODO/android-cpp-port.md` (`phonemirrormanager`) as phases land.

**Delete this file when** a phone mirrors onto the Android build over Wi-Fi, and USB has either shipped or moved to its own TODO.
