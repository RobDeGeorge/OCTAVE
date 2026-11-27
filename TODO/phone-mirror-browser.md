# Phone mirror in the browser build

**Goal:** a visitor to the browser build (robdegeorge.com/OCTAVE/) plugs an Android phone into the computer and mirrors it in the page: picture, touch, sound and the phone's media controls, over WebUSB.
**Status:** deferred. Scoped, no work started. Nice for the demo site, no use in the car (the Jeep runs the native build).
**Last updated:** 2026-11-26

## Why it's parked

It's possible, but it's a new transport underneath working code, not a port of it. The Tango / ya-webadb project already runs scrcpy in Chrome over WebUSB, so the approach is proven. Most of the work is an adb client of our own, and that same client is the big piece of `TODO/phone-mirror-android-host.md`. Do the two together, or do this one after the shared adb core exists. Building two adb clients would be wasted work.

## Why it can't work today

The browser build defines `Q_OS_MOBILE` (`CMakeLists.txt:511`), so:

- `ScrcpyClient` is compiled out entirely (`src/phone_mirror/scrcpyclient.h:4`), and `PhoneMirrorManager` is the mobile stub (`src/managers/phonemirrormanager.h:595`) with `nativeAvailable` false. The button never shows.
- Even with those removed, nothing below them can run in a page:
  - **adb binary through `QProcess`.** `ScrcpyClient::adbRun` (`scrcpyclient.cpp:516`) pushes the jar (`:591`), lists, sets and removes forwards (`:600`–`:614`), and starts the server with `shell CLASSPATH=… nohup app_process …` (`:657`), whose merged stdout is the server log (`pumpServerLog`, `:677`). The manager's probe thread runs `adb devices`, and its Wi-Fi code runs `pair` / `connect` / `mdns` / `tcpip` (`runAdb`, `phonemirrormanager.h:386`). The wasm build has no `QProcess` (`QT_CONFIG(process)` is off).
  - **TCP.** Video, audio and control are `QTcpSocket`s to `adb forward`'s local port (`connectUntilReady`), read by blocking loops on `std::thread`s.
  - **libavcodec.** `src/util/h264decoder` sits behind `OCTAVE_HAVE_FFMPEG`, which is never set under `EMSCRIPTEN` (`CMakeLists.txt:185`).

These parts carry over unchanged: the scrcpy protocol code (device meta, packet framing, control messages, MirrorKeeper 100–103, `OCTAVE_END` 104, PhoneMedia / queue / art messages, `AdaptiveBitRate`), the jar itself (107 KB, already embedded in every build including wasm, `CMakeLists.txt:730`), the QML views (`PhoneMirror*`, `PhoneMirrorWidget`, the Media Room corner) and `nowPlaying`'s phone source. The server already writes raw PCM (`audio_codec=raw`, `:627`), so the browser needs no audio decoder.

## Design

```
QML ── PhoneMirrorManager ── ScrcpyClient (protocol, unchanged)
                                   │  AdbTransport (new interface)
                   ┌───────────────┼──────────────────────┐
           QProcess adb +     built-in adb client      built-in adb client
           TCP (desktop,      over WebUSB              over USB host / TLS
           as today)          (this file)              (android-host TODO)
```

1. **`AdbTransport` interface** (shared with the Android-host plan): `push(localBytes, remotePath)`, `shell(cmd) → stream`, `openLocalAbstract(name) → QIODevice`, `devices()`, `close()`. The desktop implementation wraps today's adb + `forward` + `QTcpSocket`, so `ScrcpyClient`'s session reads `QIODevice`s and never sees TCP. `connectUntilReady`'s retry (the server isn't listening yet) becomes "OPEN was refused with CLSE, try again".
2. **adb protocol core, `src/adb/`** (C++, no I/O of its own, fed a byte pipe): the 24-byte header, CNXN / AUTH / OPEN / OKAY / WRTE / CLSE, stream multiplexing, `sync:` SEND for the push, `shell:` (the legacy one: stdout and stderr merged, the same as `MergedChannels`). It runs on the session's worker thread. It's unit-testable with a recorded or fake adbd, and the Android-host plan uses it over USB host and TLS. Open only `localabstract:scrcpy_<scid>` streams directly: no `forward` and no `reverse`, so the server keeps `tunnel_forward=true`.
3. **WebUSB link, `src/platform/wasm/webusb.{h,cpp}`**, following the `webserial` / `webbluetooth` pattern: a JS helper on the main thread (`Module.octaveWebUsb`) holds the `USBDevice` (browser objects live only there) and runs `transferIn` / `transferOut` on the adb interface's bulk endpoints. A `WebPortChannel`-style pipe (`webport.h`) hands the bytes to the worker. `requestDevice` filter: `{classCode: 0xff, subclassCode: 0x42, protocolCode: 0x01}`.
4. **Decode with WebCodecs**: `VideoDecoder` with `codec: 'avc1.<profile from SPS>'` and no `description`, so Annex B goes in as it arrives (prepend SPS/PPS to the keyframe as `h264decoder` does). It runs in the JS helper. Each `VideoFrame` gets `copyTo()` into an NV12 / I420 buffer in wasm memory, wrapped as a `QVideoFrame`, and goes through the same `pushFrame` / `QVideoSink` path. Everything QML does with the picture (rounded mask, Media Room corner, aspect box) keeps working. Fallback if the copy costs too much: draw the `VideoFrame` on a `<canvas>` positioned over the Qt canvas, the way web frontends are shown, but z-order, clipping and the rounded mask all break that way, so it's a last resort.
5. **Audio**: PCM s16le 48 kHz into the existing `QAudioSink` path, if Qt's wasm `QAudioSink` (Web Audio) behaves. Otherwise a small `AudioWorklet` in the helper. `AudioCushion` should still work; its starting value will need tuning for Web Audio.
6. **Manager**: replace the `Q_OS_MOBILE` guard with `OCTAVE_HAVE_PHONE_MIRROR` (native desktop, or wasm with this transport). The wasm manager has no Wi-Fi, pairing, QR, mDNS, `tcpip`, firewall or adb-server code; split those into the desktop `.cpp` the way `networkmanager_wasm.cpp` / `obdadapterfinder_wasm.cpp` do. New: `canMirror` (WebUSB and WebCodecs H.264 both present) and `pickPhone()` (the chooser, from the tap).

## Edge cases, and what to do about each

### Browser and platform

- **Firefox and Safari have no WebUSB.** `canMirror` is false and the phone button is hidden, as `canScan` hides the Bluetooth pick. Don't show a dead button.
- **Chromium without proprietary codecs** (some Linux distro builds) has no H.264 in WebCodecs. Check with `VideoDecoder.isConfigSupported()` at startup. The server only offers h264 / h265 / av1, and few phones encode AV1, so there's no good fallback codec. Say "this browser can't play the phone's video" instead of starting a session that shows black. (A wasm software H.264 decoder is possible but heavy and slow; not worth it.)
- **Hardware-decode limits:** some GPUs or drivers refuse high-level H.264 (the phone's 1080p60 high profile). If `configure` fails, restart the session at 720p / 30 fps.
- **Chrome on Android** has WebUSB. Mirroring one phone onto another over OTG is a curiosity; allow it but don't test it.
- **Not in a secure context** (a local build served over plain http on a LAN address) means no `navigator.usb` and no `VideoDecoder`. `localhost` is fine. `serve.py` already serves localhost.
- **Permissions-Policy:** the Qt canvas is top-level in `index.html` / `playground.html`, so `usb` is allowed. The frontend iframes are sandboxed without `allow-same-origin` and get no WebUSB, which is what we want. If the core is ever put in an iframe, it needs `allow="usb"`.

### USB access (the most likely support problem)

- **Another adb holds the interface.** Only one program can claim the adb interface. With an `adb server` running (Android Studio, a native OCTAVE, scrcpy), `claimInterface` fails. Report it as "another program is using the phone (adb). Close it, or run `adb kill-server`" and don't retry in a loop. Same for a second tab of the page.
- **Linux:** the user needs a udev rule for the phone (the `android-udev` package or a `51-android.rules`), or Chrome lists the phone and the open fails with a permission error. Say so in that error.
- **Windows:** Chrome can only claim interfaces bound to WinUSB. With Samsung's (or another OEM's) USB driver installed, the adb interface belongs to that driver and the open fails. Point to Google's USB driver / Zadig in the error and the wiki. Test on the Win11 VM with USB passthrough.
- **macOS:** works once no adb server is running.
- **USB debugging off:** the phone exposes no adb interface, so the chooser is empty or doesn't list it. The empty-chooser case can't be detected (the chooser is the browser's), so the button's help text must say "turn on USB debugging first", with the same steps the native onboarding gives.
- **USB mode switches** (charging → file transfer, or the phone's own USB prompt) re-enumerate the device. That's a `disconnect` followed by a `connect` for the same device; treat it as a cable pull (below), not a new phone.
- **The chooser needs a user gesture.** `requestDevice` must be called inside the tap's event handler, as `WebSerialLink::choose()` is from "Pick USB Adapter". On later visits, `navigator.usb.getDevices()` returns phones the user has already allowed, so reconnect without the chooser.
- **Several phones:** the chooser picks one. The phone list (`devices`) is the allowed devices from `getDevices()`, not an adb probe.

### adb authentication

- **RSA key:** adbd signs nothing itself; it sends a 20-byte token and wants `RSA_sign(NID_sha1)` over it, meaning PKCS#1 v1.5 with the SHA-1 DigestInfo prefix over the token *as if it were the digest*. WebCrypto always hashes its input, so it can't do this. Use a raw modpow, either JS `BigInt` in the helper or a small bignum in `src/adb/`. Pick one in the spike. The Android-host plan has OpenSSL, so a C++ interface with two backends is fine. Generate a 2048-bit key once (WebCrypto `generateKey` + export to JWK for n / d is fine) and store it in IndexedDB.
- **Public key format:** Android's `RSAPublicKey` struct (len, n0inv, n, rr, e), base64, plus a name. The name shows on the phone. Use a fixed `octave@browser`: no hostname, user name or email (privacy rule).
- **First connection:** the phone shows "Allow USB debugging?" and the user has to unlock it and tap Allow. Wait up to ~60 s with a clear "unlock your phone and tap Allow" message, not a spinner. Treat a timeout as unauthorized, not failed.
- **Storage cleared, private window or blocked storage:** no key, so a new one is generated and the phone asks again. Wrap every IndexedDB access in try/catch and keep the key in memory for the visit.
- **"Revoke USB debugging authorizations" on the phone:** AUTH comes back for an old key. Send the public key again and the prompt reappears.
- **Guest pages:** the key is a credential for the visitor's phone. Under `guestPage()` (`spotifymanager.cpp:68`) neither read the stored key nor write one, the same rule as the Spotify session.

### Session and protocol

- **Flow control:** classic adb allows one outstanding WRTE per stream until its OKAY. Video at 8 Mbps over a 1 MB max payload is fine (Tango shows it). Newer adbd's delayed-ACK feature (`A_VERSION 0x01000001`) can come later if throughput is short.
- **Max payload negotiation:** use the CNXN reply's value. Old phones say 4 KB; newer ones up to 1 MB.
- **Server not listening yet:** OPEN of `localabstract:scrcpy_<scid>` gets CLSE. Retry until the deadline `connectUntilReady` already uses (20 s, 4 s on attach). The server sends meta only after every socket is open, so open video, audio and control in that order, as now.
- **The forward-mode dummy byte:** with `tunnel_forward=true` the server writes one byte on the first socket. Same as now; keep it.
- **Stale forwards:** there are none (there's no `forward`), so drop the `forward --list` cleanup on this transport. Stale *servers* still exist: the `pkill` of `kServerProcessPattern` before a new start stays.
- **Reattach (`attachScid`)** works the same way: the server was started with `nohup` and outlives its shell (`:653`), so after a replug the client opens the same `scid` streams and follows the log with `logcat` over `shell:`.
- **Shell text:** only fixed commands built in C++ go to `shell:`. Never let a service call, a URL parameter or a setting put text there (the same rule as custom PIDs and the wire).

### Cable pull, tab life, page close

- **Cable pull:** `transferIn` rejects and `navigator.usb` fires `disconnect`. Go into the reconnect grace the native code uses (the server holds the audio capture 10 s and the display 60 s). On `connect` for the same device (match the serial from the USB descriptor), reconnect with no chooser and reattach. Keep the phone's track through it, as the manager already does.
- **Tab hidden:** Chrome stops `requestAnimationFrame` and throttles timers, but WebCodecs and audio go on. Stop decoding to frames nobody sees (drop until visible, then `requestKeyframe()`), keep the audio, and keep the keeper's `stayAwake` timer going. A tab playing audio isn't frozen. A silent hidden tab may be frozen after 5 minutes (Chrome) or put to sleep (Edge). On resume, treat it as a link drop.
- **Reload or close:** there's no reliable chance to send `OCTAVE_END`. Try it on `pagehide` (a `transferOut` that may not complete). If it doesn't arrive, the server waits its 10 s, then releases the audio and pauses the music. That's acceptable; say so in the wiki. The browser releases the device on unload.
- **Wasm memory:** a 1080p NV12 frame is 3 MB. Keep a small pool of frame buffers instead of allocating per frame, or memory grows until the page dies. Two or three in flight is enough. Drop frames rather than queue them.
- **Main thread:** USB, WebCodecs and the copies all run on the page's main thread, which also renders Qt. Measure frame times at 1080p60. If the page stutters, cap the browser build at 720p / 30 and let `AdaptiveBitRate` work from there. Never block the main thread waiting on the worker (the `webport.h` freeze, which happened once already).

### What the page can't do

- **Wi-Fi mirroring.** A page can't open raw TCP or do adb's TLS pairing. Chrome's Direct Sockets API is only for Isolated Web Apps, not a website. A relay (a small program on the computer bridging WebSocket ↔ adb) would work, but at that point the native app is the better answer. Out of scope; the UI shouldn't offer Wi-Fi in the browser.
- **Auto-Wi-Fi, QR pairing, the adb firewall rule, the adb-server notice:** desktop only. Hide them in the browser build's settings.

### Service and frontends

- The page's frontends hold the service at full scope. Phone touch / key injection and the phone video stream (`ScrcpyClient::setVideoTap`, `streamhub`) would let a guest frontend watch and drive the visitor's phone. Under `guestPage()`, refuse `phoneMirrorManager`'s writing calls and the phone stream over the service, and say so in `docs/api/` for the browser build. The page's own built-in frontends (glass etc.) may show the phone.
- Nothing new on the API surface beyond `canMirror` / `pickPhone()`. Mark them `octave.experimental`, regenerate `docs/api/` and the SDKs, and bump nothing.

### Size

- `src/adb/` and the helper add roughly tens of KB, and the jar is already in the build. No new download for visitors. Keep the JS helper inline as the other `web*` helpers are, no npm dependency. If ya-webadb is borrowed from (MIT), keep its notice in `tools/phone-server/NOTICE` or the license texts (`licensing-review.md`).

## Steps

Shared with `TODO/phone-mirror-android-host.md`: steps 1–2 are that plan's step 2 (spike) and step 3 (`AdbTransport` on desktop). Do them once, for both.

1. **adb protocol core + spike on desktop.** **Effort:** L. **Device:** desktop Linux + S22. `src/adb/` over a plain byte pipe; test it against the phone through Linux usbfs (or libusb) in a test program. Prove CNXN + AUTH (prompt, then the stored key), push the jar, `shell:` the server, open three `localabstract` streams, and get a picture with the existing decoder. Add a QtTest with a fake adbd (recorded exchanges) to `tests/cpp/`.
2. **`AdbTransport` in `ScrcpyClient`.** **Effort:** M. **Device:** desktop + S22, then the Pi for a soak. The session reads `QIODevice`s; the default transport is still QProcess adb + TCP. `tst_scrcpyclient` / `tst_phonemirror` keep passing. Regression-check Wi-Fi reconnect and the cable pull on the Pi.
3. **WebUSB link + manager un-stub in the browser build.** **Effort:** M. **Device:** desktop Chrome + S22 on a cable. `webusb.{h,cpp}`, the `OCTAVE_HAVE_PHONE_MIRROR` guard, the trimmed wasm manager, `pickPhone()`, the key in IndexedDB. First goal: the server starts and its log shows in the page's log.
4. **WebCodecs decode into `QVideoSink`.** **Effort:** M. **Device:** same. Measure 1080p60 vs 720p30 frame time and pick the browser build's default. Fall back to the canvas overlay only if the copy is too slow.
5. **Audio, keeper, media controls, touch.** **Effort:** M. **Device:** same. Check ducking against weblibrary music and that the visualizer gets the phone's sound.
6. **The edge cases above.** **Effort:** M. **Device:** Chrome on Linux (udev), Windows (Win11 VM with USB passthrough: driver conflicts), a Mac if one is available; Firefox for the hidden button; a Chromium without H.264 for its message. Cable pull, replug, mode switch, hidden tab, reload, second tab, adb server running, revoked authorization.
7. **Guest-page rules and docs.** **Effort:** S. **Device:** desktop. `guestPage()` guards; `wiki/phone-mirror-android-auto.html` (a "In the browser" section: Chrome / Edge, USB only, udev / WinUSB, kill adb), `wiki/building.html` if the wasm build changes, CLAUDE.md's WebAssembly paragraph, `docs/api/` regenerated.

Not doing: Wi-Fi in the browser, a relay helper, Firefox / Safari.

Related: `TODO/phone-mirror-android-host.md` (the same adb core; whichever starts first builds it), `TODO/phone-mirror-robustness.md` (reconnect behaviour the browser transport has to match), `TODO/licensing-review.md` (any borrowed WebUSB / adb code).

**Delete this file when** a phone mirrors with sound and touch in the published browser build in Chrome on Linux and Windows, and the edge cases above have each been tried or written up as known limits in the wiki.
