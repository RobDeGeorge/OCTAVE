# Native Android Auto receiver (replace the DHU screen-scrape)

**Goal:** let a phone run Android Auto straight into OCTAVE, first over the adb connection phone mirroring already uses, later over a plain USB accessory link, with no Google Desktop Head Unit and no extra hardware.
**Status:** Phases 0–2 done on branch `android-auto-native` (2026-10-25): OCTAVE runs real Android Auto from a phone over adb, in C++, on desktop Linux. Next: listen, talk and touch in person, then the Orange Pi / Jeep. DHU stays the default backend until then.
**Last updated:** 2026-10-25

## Where it stands

Android Auto used to mean launching Google's Desktop Head Unit (DHU) and screen-scraping its window, which only ever worked on desktop x86_64 and never in the Jeep. Now `androidAutoBackend = "native"` makes OCTAVE the head unit itself: it speaks the Android Auto Protocol to the phone's head unit server through an adb forward (`src/android_auto/`, `src/managers/androidautomanager_native.cpp`, layouts in `docs/ANDROID_AUTO_PROTOCOL.md`). The DHU path is untouched and still the default.

Checked on 2026-10-25 against a Samsung SM-S948U, Android Auto 17.9.664044, wireless adb, desktop Linux build:
- certificate accepted, every channel opens, 1280x720 H.264 shows in `AndroidAutoView.qml` (Maps, Spotify, the app rail)
- taps and drags reach the phone (opened Spotify, scrolled its grid)
- media audio plays on the user's output at OCTAVE's volume
- leaving the page stops the phone's video (video focus) and coming back resumes it; the session stays up
- three connect / stop cycles in a row, and quitting OCTAVE mid-session followed by a relaunch: each ends with a bye-bye and the next session connects without touching the phone
- `tst_androidauto` (15 cases) runs a whole session against a fake phone, no device needed

Not checked yet (nobody there to hear or speak): how the audio sounds, guidance prompts during navigation, the microphone (implemented: captured only while the phone has it open, see `AapConnection::onMicrophone`), multi-touch on a real touchscreen.

**Common prerequisite (done):** the shared H.264 decoder is `src/util/h264decoder.{h,cpp}`; phone mirroring and Android Auto both use it, and `TODO/carlinkit-dongle-port.md` / `TODO/phone-mirror-android-host.md` can too.

### Licensing (resolved)

OCTAVE moved to GPL-3.0-or-later on 2026-10-12 (commit `3352b92`). That makes the GPLv3 references usable directly: **aasdk** (C++, the library behind OpenAuto / Crankshaft) and its `.proto` definitions can be linked or copied. **Open Headunit** (`github.com/andreknieriem/headunit`, a revival of Mike Reid's `mikereidis/headunit`) is **AGPL-3.0**: read it, don't copy from it, unless OCTAVE goes AGPL too.

## Learned on the way (2026-10-25)

- The head unit server can't be started over adb on Android Auto 17.9 (`startHeadunitServer()` in `androidautomanager.cpp`, used by the DHU path, fails both ways). The user turns on developer mode and taps *⋮ > Start head unit server* once; it then serves session after session and survives USB unplug. The page and the wiki walk through it.
- A session dropped without a bye-bye can leave the server accepting TCP but never answering. OCTAVE times out after 10 s and says how to restart the server, and always ends sessions with a bye-bye (also at quit).
- Android Auto refuses a head unit without an audio sink and a microphone source.

## Two transports, one session

Why not push a dex the way phone mirroring does? The mirroring jar runs under `app_process` as the adb shell user, which may capture the screen and inject input, so the phone has nothing to opt into. Android Auto is different: the Android Auto app only draws its car UI after a complete AAP session with a head unit, including TLS with a Google-chained client certificate. A shell-user dex can't fake that without root, and mirroring Maps onto a virtual display is phone mirroring, not Android Auto. So nothing new runs on the phone; the phone-side half is the Android Auto app itself.

What carries over from mirroring is the **transport**. Android Auto's developer mode has a *head unit server* (`com.google.android.projection.gearhead/.companion.DeveloperHeadUnitNetworkService`) that listens on phone port 5277. The DHU is just an AAP client on that socket, reached through an adb forward. OCTAVE speaks AAP on that socket itself: `androidautomanager_native.cpp` picks the phone as phone mirroring does, checks the server listens (`/proc/net/tcp6`, port `:149D` in state `0A`), forwards a free local port and hands it to `AapConnection`.

| | **adb transport** (Phases 0–3) | **USB accessory (AOA) transport** (Phase 5) |
|---|---|---|
| Link | `adb forward tcp:5277` → `QTcpSocket` | libusb bulk endpoints after the AOA switch |
| Phone setup | USB debugging + Android Auto developer mode + head unit server (started by hand once) | Android Auto "Unknown sources" only |
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

## What was built

- `src/android_auto/`: `aapproto` (hand-encoded protobuf, no libprotobuf), `aapframer` (frames, per-frame decryption order, per-channel reassembly), `aaptls` (OpenSSL TLS 1.2 client over a memory BIO pair), `aapsession` (the state machine, no I/O), `aapconnection` (socket + session + decoder on a worker thread; frames, audio and mic on the GUI thread). Built when OpenSSL and FFmpeg are found (`OCTAVE_HAVE_NATIVE_AA`, desktop only; CI's Linux job has both, so `tst_androidauto` runs there).
- `androidAutoManager` native mode (experimental API members): `backend`, `nativeState` / `nativeStatus`, `videoSink`, `startNative` / `stopNative`, `injectTouch`, `setViewVisible`; retries while the page wants Android Auto; volume, mute, output device and night mode wired in `AppCore`.
- Settings (generic keys): `androidAutoBackend` (`"dhu"` | `"native"`), `androidAutoCertPath` (empty = `<config>/android-auto/headunit.{crt,key}`), `androidAutoResolution`, `androidAutoFps`, `androidAutoDpi`. UI in Settings > Accessories > Phone Dock > Android Auto; the nav-bar button follows `androidAutoEnabled`.

## Risks

- **Google's head-unit certificate.** The phone only accepts a TLS client certificate chained to Google. Every open-source receiver ships one extracted from Google's own or an OEM's software. It's a legal gray area, and Google can revoke it, which would break every user at once. Decision: **OCTAVE never ships one** and the docs don't say where to get one; the user supplies a PEM certificate + key. The DHU 2.0 binary holds its certificate in a form that isn't plain PEM or DER, so "extract it from a local DHU at runtime" is not a thing OCTAVE can do. For testing, the developer's copy lives in the gitignored `dev/aa-cert/`.
- **Head unit server lifetime.** It survives USB unplug and many sessions; unknown across a phone reboot, an Android Auto update or the app being killed. Starting it over adb doesn't work on 17.9 (see above), so a reboot probably means a tap on the phone before driving. Worth checking whether it can be left running for weeks.
- **Wireless AA is getting harder.** Android Auto 17.4+ blocks the older ways of starting a wireless session (Open Headunit's README warns about it). Real wireless AA needs a Bluetooth RFCOMM handshake that hands Wi-Fi credentials to the phone, then TCP. Wireless adb sidesteps that for the developer path; for end users the Carlinkit dongle is the dependable wireless answer.
- **Protocol churn.** Google changes the phone side without notice. Pin a known-good Android Auto version in the docs and log the version the phone reports, so breakage is diagnosable from the Diagnostics export.
- **USB permissions (AOA only).** Linux needs a udev rule for the phone before and after the AOA switch (both VIDs). Ship it in `packaging/linux/` and the AppImage docs. Windows may need WinUSB bound to the accessory interface (Zadig-style). macOS should just work with libusb. The adb transport needs none of this.
- **Head-unit performance.** 1080p60 decode on the Orange Pi 5 Plus needs hardware decode (RK3588 `h264_rkmpp` / v4l2m2m, also wanted by `TODO/phone-mirror-robustness.md` item 6). Start at 800x480/1280x720 at 30 fps, then measure against the phone-mirror numbers.

## Recommended order

1. **In person on the desktop.** **Effort:** S. **Device:** desktop Linux + phone. Listen to media and navigation prompts, hold the Assistant button and talk (mic), two-finger zoom on a touchscreen if one is around. Fix what turns up.
2. **Media integration.** Moved to [media-source-arbiter.md](media-source-arbiter.md): Android Auto in `nowPlaying`, focus decided by OCTAVE, ducking, resume.
3. **Orange Pi 5 Plus + Jeep.** **Effort:** M–L. **Device:** Orange Pi 5 Plus in the Jeep + phone. Copy the certificate to `<config>/android-auto/`, check the RK3588 decodes 1280x720 at 30 fps in software or add hardware decode (`h264_rkmpp` / v4l2m2m, also wanted by `TODO/phone-mirror-robustness.md`), test in the car over the adb link mirroring uses. Driving status could come from OBD speed (it is always "unrestricted" now). Then flip the `androidAutoBackend` default to `"native"`.
4. **Onboarding.** **Effort:** S. **Device:** desktop + phone. Remove the dead `startHeadunitServer()` attempts from the DHU path or make them report that they can't work; a setup check in Settings that tells whether developer mode / the server / the certificate are in place.
5. **Windows/macOS.** **Effort:** S–M. **Device:** Windows and macOS machines + phone. OpenSSL in the release builds (vcpkg / Homebrew `openssl@3`, CMake `OPENSSL_ROOT_DIR`), then a smoke run; the adb transport needs no USB setup.
6. **AOA transport.** **Effort:** L. **Device:** desktop Linux + phone, then the Android tablet. `aoatransport` (libusb, udev rule, WinUSB docs) feeding the same `AapSession`, then `UsbManager` on the Android build (`TODO/android-cpp-port.md`). This is what makes it work for users without developer mode. Wireless AA only if it's still feasible by then.

Update `wiki/phone-mirror-android-auto.html`, `wiki/signals-slots-reference.html`, `wiki/hardware-setup.html` and `README.md` as each phase lands.

**Delete this file when** the native receiver is the default on desktop and the Orange Pi, DHU is either removed or explicitly kept as a documented fallback, and any leftover work (AOA transport, wireless, Android build) has its own smaller TODO.
