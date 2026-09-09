# Native phone mirror: OCTAVE as a scrcpy-protocol client

**Status:** active (approved 2026-09-09)
**Owner:** OCTAVE dev agent + Orange Pi hardware agent
**Goal:** phone mirroring with **no user-installed dependencies**. No scrcpy binary, no
v4l2loopback, no ffmpeg on PATH, no `adb shell input` for touch. OCTAVE talks to the
phone itself; the only external pieces are two small Apache-2.0 files we ship inside
OCTAVE (`adb` and the scrcpy server jar).

## Phase 0 results (Orange Pi 5 Plus, scrcpy server 3.3.4, Galaxy S22 Ultra / Android 16)

- **Decoder decision: Option B (PyAV / libavcodec).** Qt's FFmpeg backend refuses a raw
  Annex-B `QIODevice` (`FormatError "Could not open file"` after 1.5 MB fed). Option A is dead.
- **First paint:** 0.41–1.26 s from server launch to first decoded frame, phone idle or not
  (v4l2 path: 4–58 s). The encoder emits a key frame on connect; the seed/nudge machinery goes away.
- **Decode cost:** software h264 at 1280x800 = 12–15 % of one RK3588 core (PyAV 16.1 bundled ffmpeg).
  PyAV has `h264` and `h264_v4l2m2m` but **not** `h264_rkmpp`; system ffmpeg has both. Try v4l2m2m later.
- **Control socket:** 32-byte touch message opens an app; change visible in the stream at +0.17 s
  (adb `input` path: ~0.13 s just to spawn, plus no multitouch).
- **Byte layouts below verified against the v3.3.4 source** (`server.h`, `demuxer.c`, `control_msg.c`): no discrepancies.
- **Gotchas:** `cleanup=true` deletes the jar on exit, so push it every session; with `tunnel_forward`
  the local TCP connect succeeds before the server listens and then EOFs — retry until the dummy
  byte arrives; the first decoded frame can precede the launcher drawing its icons.
- Bundled server: `tools/scrcpy-server/scrcpy-server-v3.3.4`, sha256
  `8588238c9a5a00aa542906b6ec7e6d5541d9ffb9b5d0f6e1bc0e365e2303079e`, Apache-2.0 (LICENSE alongside).

## Why

The v4l2 path that landed in `b0f5095` works, but every link in it is an install step
or a failure mode on someone else's box:

| Today | Problem |
|---|---|
| scrcpy desktop binary | distro versions are years old (Ubuntu 22.04 ships 1.21, which cannot mirror Android 14+); building from source is not something a user will do |
| v4l2loopback kernel module | must be built (DKMS) or shipped by the distro, must be loaded at boot with the right params, Linux-only |
| ffmpeg subprocess reading the loopback node | extra process, 4 MB/frame copies, and the loopback starves on a static display so first paint is 4–60 s |
| `adb shell input tap/swipe` for touch | ~130 ms per event, no multitouch, no long-press, no drag, no pinch |
| Windows: window screen-grab | fragile, GDI-only, keeps a hidden scrcpy window alive |

The scrcpy **server** (the jar running on the phone with shell privileges) is the part
that is hard to replace and does not need replacing: it creates the virtual display,
captures without a consent dialog, encodes on the phone's hardware encoder, and injects
input. The scrcpy **desktop client** is what we replace, with code inside OCTAVE.

## What we keep, what goes

**Keep (bundled, Apache-2.0, no user action):**
- `scrcpy-server` jar (~70 KB) from the scrcpy release matching the protocol we implement. Pinned version; updated deliberately.
- `adb` from Android platform-tools, per platform (Linux x86_64 / aarch64, Windows, macOS). Only used for `devices`, `push`, `forward`, `shell app_process`. Bundled under `tools/platform-tools/<platform>/`; fall back to a system `adb` if the bundled one is missing.

**Remove once the native path is validated:**
- `PhoneMirrorManager`'s scrcpy binary discovery, version gate, `captureMode`, `videoDevice`, and the whole v4l2/ffmpeg reader in `ScrcpyCapture`.
- The Windows window-grab path (`findWindowByPid`, GDI capture, off-screen window).
- `scrcpyPath`, `scrcpyVideoDevice` settings. `scrcpyDisplaySize` and `scrcpyAudioEnabled` stay.
- wiki text describing the v4l2 chain (keep the "why not QtMultimedia" knowledge; it still applies to camera enumeration).

## The scrcpy protocol (server 3.x)

Everything below is what the scrcpy desktop client does; source of truth is
`app/src/server.c`, `app/src/demuxer.c`, `app/src/control_msg.c` in the scrcpy tree
at the pinned tag. Verify each detail against that tag before coding.

1. **Push the server:** `adb -s SERIAL push scrcpy-server /data/local/tmp/scrcpy-server.jar`.
2. **Tunnel:** `adb -s SERIAL forward tcp:PORT localabstract:scrcpy_SCID` where SCID is a
   random 31-bit hex id. (Forward, not reverse: simpler, works everywhere.)
3. **Start:** `adb -s SERIAL shell CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server VERSION key=value ...`
   with at least: `scid=SCID tunnel_forward=true video=true audio=false control=true
   video_codec=h264 max_size=0 video_bit_rate=8000000 max_fps=60 stay_awake=true
   cleanup=true send_device_meta=true send_frame_meta=true send_codec_meta=true
   send_dummy_byte=true log_level=info` and `new_display=WxH` when a virtual display is
   wanted (Android 11+). VERSION must equal the jar's version string exactly.
   Keep the `app_process` running as a child; its stderr is the server log.
4. **Connect:** open TCP `localhost:PORT` repeatedly (100 ms) until it accepts. With
   `tunnel_forward` the first connection is the **video socket**, the second the
   **control socket** (audio would sit between them if enabled). The server writes one
   dummy byte on the first socket (`send_dummy_byte`); read and discard it.
5. **Device meta** (first socket): 64 bytes, NUL-padded device name.
6. **Codec meta** (video socket): 4 bytes codec id (`h264` FourCC), 4 bytes width, 4 bytes
   height, big-endian.
7. **Frames** (video socket, `send_frame_meta`): repeated `[8 bytes PTS+flags][4 bytes size][size bytes]`,
   big-endian. Bit 63 of the first field = config packet (SPS/PPS, PTS invalid),
   bit 62 = key frame. Data is Annex-B H.264. Feed config packets to the decoder before the first key frame.
8. **Control messages** (client → control socket), first byte = type:
   - `2` INJECT_TOUCH_EVENT: action u8 (0 down, 1 up, 2 move), pointer id u64
     (`-1` = mouse / `-2` = finger; use distinct small ids per finger for multitouch),
     x i32, y i32 in **video frame pixels**, screen width u16, screen height u16,
     pressure u16 fixed-point (0xFFFF = 1.0), action_button i32, buttons i32. 32 bytes total.
   - `0` INJECT_KEYCODE: action u8, keycode i32, repeat i32, metastate i32.
   - `1` INJECT_TEXT, `9` SET_CLIPBOARD, `10` SET_DISPLAY_POWER (screen off while mirroring), `3` BACK_OR_SCREEN_ON.
   Coordinates are relative to the mirrored display, so the virtual display needs no
   `input -d`: the server routes injection to the display it mirrors.
9. **Device messages** (control socket, server → client): clipboard changes; safe to read and drop initially.
10. **Teardown:** close both sockets and kill the `app_process` child; the server exits and
    destroys the virtual display (`cleanup=true`). Keep the `adb shell pkill -f com.genymobile.scrcpy` safety net and `adb forward --remove`.

## Video decode: two options, decide by spike

**Option A (zero new dependencies): QMediaPlayer over a QIODevice.**
Write the Annex-B stream into a `QIODevice` (a pipe-like `QBuffer`/custom device) and
call `QMediaPlayer::setSourceDevice()`; render through `QVideoSink` → QML `VideoOutput`.
Qt's ffmpeg backend can demux raw H.264. Unknown: startup probing delay and internal
buffering may add hundreds of ms of latency, and hardware decode on the RK3588 is not
available through Qt's bundled ffmpeg. Same code shape in PySide6.

**Option B (one build-time dependency): libavcodec directly.**
C++: link `libavcodec`/`libavutil` (pkg-config on Linux, vcpkg or the gyan.dev shared
build on Windows, brew on macOS); decode on a worker thread with `h264_rkmpp` /
`h264_v4l2m2m` / `h264` chosen at runtime; upload frames as a `QVideoFrame` to a
`QVideoSink` (`VideoOutput` in QML), which avoids the per-frame QImage copy of the
image-provider path. Python: **PyAV is already in `requirements.txt`** (`av>=12`) and
wheels bundle ffmpeg, so the Python backend gets Option B for free.

Decision (Phase 0): **B in both backends.** Option A cannot open the stream at all. The QML
side is `VideoOutput` bound to a `QVideoSink` the manager exposes; decoded YUV420P planes go
into a `QVideoFrame` without an RGB conversion.

## Touch model

`PhoneMirrorView.qml` replaces the `MouseArea` with a `MultiPointTouchArea` (mouse
falls through as one point) and forwards down/move/up per touchpoint to
`phoneMirrorManager.injectTouch(id, action, x, y)`, mapping from `VideoOutput.contentRect`
to frame pixels. That gives tap, long-press, drag, scroll, and pinch with socket
latency. Add `injectKey(keycode)` for BACK / HOME / APP_SWITCH buttons in the overlay.

## Class layout (both backends, mirrored)

- `PhoneMirrorManager` (existing name, same context property): device state, settings,
  lifecycle. Loses scrcpy-binary discovery; gains `injectTouch`, `injectKey`, and
  `videoSink` (a `QVideoSink*` the QML `VideoOutput` binds to).
- `ScrcpyClient` (new, `src/phone_mirror/scrcpyclient.{h,cpp}` /
  `backend/phone_mirror/scrcpy_client.py`): adb helpers, server push/launch, sockets,
  demuxer, control writer. Runs on its own thread; emits `frameSizeChanged`,
  `connected`, `disconnected(reason)`, `serverLog(line)`.
- `H264Decoder` (new): Option A or B behind one interface: `feed(packet, isConfig)` → `QVideoFrame`.
- `ScrcpyCapture`, `EmbeddedScrcpyItem`, `ScrcpyHostItem`, `scrcpy_container.py`,
  `embedded_widget.py`, the `scrcpyframe` image provider: deleted at the end of Phase 3.

## Phases

**Phase 0 — spike (1–2 days).** Standalone Python script on the Pi: push jar, forward,
launch server, read frames, decode with PyAV, print fps/latency; then the same stream
through `QMediaPlayer.setSourceDevice` to measure Option A. Also confirm on the Pi:
touch injection via the control socket lands on the virtual display; SPS/PPS handling;
`new_display` size rounding. Output: decision A vs B, plus measured first-paint and latency.

**Phase 1 — client + decode, Python first. DONE (commits a3cdd09, 8c04adf):** hardware validation on the Pi in progress.
Original estimate: Python is the fastest to iterate
on the Pi. `scrcpy_client.py` + decoder + `VideoOutput` in QML + multitouch. Old paths
stay behind a `phoneMirrorNative` setting (default off) so the Pi can A/B.

**Phase 2 — C++ port. DONE (same day):** `src/phone_mirror/scrcpyclient.{h,cpp}`, libavcodec via
pkg-config / vcpkg (optional, `OCTAVE_HAVE_FFMPEG`), jar embedded as a Qt resource, CI installs
FFmpeg on all three desktop targets. Verified against the fake server; hardware validation pending.
Original estimate: Same classes in C++, CMake for the decoder choice,
Windows and macOS builds in CI (`cpp-build-*` jobs) proving the bundled adb + jar load.

**Phase 3 — bundle and remove.** adb bundling DONE (`scripts/fetch_platform_tools.py`, pinned
platform-tools 37.0.1 with SHA-256, shipped by all three CI packagers; finders prefer it). The
removals wait for hardware validation of phases 1–2. Original scope: Add `tools/platform-tools/<platform>/adb` and
`tools/scrcpy-server` with their LICENSE files to the repo (or a CI download step with
pinned hashes) and to every packaging target in `BUILD.md`. Delete the v4l2 / window-grab
code, the `scrcpyPath` / `scrcpyVideoDevice` settings, and the dead classes. Flip
`phoneMirrorNative` to the only path and remove the flag. Update
`wiki/phone-mirror-android-auto.html`, `settings-reference.html`, `signals-slots-reference.html`, `building.html`.

**Phase 4 — hardware validation.** The Pi agent re-runs the full matrix from the v4l2
work: idle first paint, sustained scroll, touch accuracy at odd sizes, multitouch pinch,
2-minute idle resume, module-not-loaded (should now be irrelevant), all three exit
paths, cable pull, reboot. Plus Windows against the same phone.

## Risks and answers

- **Protocol drift between scrcpy majors.** We pin one server version and ship it; the
  client version string is checked by the server, so a mismatch fails loudly at start.
  Upgrading is a deliberate task: bump jar, re-verify the message layouts above.
- **Android breaks the server.** Same exposure as today, but the fix is "bump the jar",
  not "make the user build scrcpy".
- **FLAG_SECURE / DRM content** shows black. Same as scrcpy; unchanged.
- **First paint on an idle virtual display** is now bounded by scrcpy server startup
  (~8 s on the Pi) plus the encoder's first key frame; the loopback starvation is gone.
  The server emits a key frame on connect, so the 4–60 s wait should disappear. Verify in Phase 0.
- **adb licensing/bundling.** platform-tools and scrcpy are Apache-2.0; ship LICENSE files
  next to the binaries. App-store builds (`OCTAVE_ENABLE_DOWNLOADS=OFF`) may not want to
  spawn adb at all; keep the feature compiled out on mobile as today.

## Order of work

1. Phase 0 spike on the Pi (script lives in `dev/`, not shipped).
2. Decide A/B, record the numbers at the top of this file.
3. Phase 1 → Pi validation → Phase 2 → CI green on all three desktop targets → Phase 3 → Phase 4.

Delete this file when Phase 4 is done and the wiki carries the final design.
