# aarch64 AppImage: no GPL FFmpeg for Qt's media plugin

**Status:** deferred
**Last updated:** 2026-10-21

**Goal:** the Linux aarch64 AppImage (the Jeep build) ships no GPL FFmpeg or codec libraries, like the x86_64 and macOS packages.

## Where things stand

Release builds link OCTAVE's own phone-mirror decoder statically: `packaging/ffmpeg/build-h264-decoder.sh` (FFmpeg 9.0.2, H.264 decoder only, LGPL) plus `-DOCTAVE_FFMPEG_ROOT`. On x86_64 and macOS that leaves only Qt's own LGPL FFmpeg in the package, and CI fails the build if a GPL codec library shows up.

aarch64 is the exception. The arm64 Qt 6.7.3 package from aqtinstall has no FFmpeg of its own; its `plugins/multimedia/libffmpegmediaplugin.so` links Ubuntu 22.04's FFmpeg 4.4 (`libavcodec.so.58`, `libavformat.so.58`, `libavutil.so.56`, `libswresample.so.3`, `libswscale.so.5`). linuxdeploy bundles those, and with them Ubuntu's `libx264`, `libx265` and `libxvidcore`, because Ubuntu's FFmpeg is a GPL build. Checked in the v0.9.5 aarch64 AppImage. That plugin plays local music, so it can't simply be dropped. The GStreamer plugin is already dropped there (`OCTAVE_DROP_QT_PLUGINS` in the arm64 job) because jammy has no `libgstphotography`.

## Why it's parked

OCTAVE is GPL-3.0-or-later, so shipping a GPL FFmpeg is allowed. `NOTICE` says which build it is and where its source lives. This is cleanup, not a compliance hole. Both fixes touch music playback on the head unit and need a check in the Jeep, so they didn't fit into the decoder change.

## Options

1. **Build an LGPL FFmpeg 4.4 for the plugin.** **Effort:** M. **Needs:** an arm64 CI run, then the Pi.
   Build FFmpeg 4.4.x as **shared** libraries with the same sonames (`.so.58` and so on), with avformat, swscale and swresample and all native decoders, but no `--enable-gpl` and no external codec libraries. Put the build on `LD_LIBRARY_PATH` ahead of the system copy when linuxdeploy runs, and drop `libavcodec-dev` and the other FFmpeg dev packages from the arm64 job's apt line. This could become a second mode of `build-h264-decoder.sh` or a sibling script. Risk: the plugin was built against Ubuntu's 4.4.2. Same major version means the same ABI, but check that every format the music library uses still plays (mp3, m4a/AAC, flac, opus, ogg).
2. **Move to a Qt whose arm64 package carries its own FFmpeg.** **Effort:** L. **Needs:** every platform.
   That's a Qt upgrade (6.8+), which also lifts the Android targetSdk cap and the macOS AGL workaround noted in `build.yml`. Check first that the arm64 Linux package really ships FFmpeg.

## Recommended order

Option 1 if it comes up before a Qt upgrade; otherwise fold it into the Qt upgrade. Either way, extend the x86_64 job's "Check the AppImage carries no GPL FFmpeg" step to the arm64 job, and update `NOTICE`, the About page entry (`frontend/settings/AboutPage.qml`, "FFmpeg (inside Qt Multimedia)"), `BUILD.md` and `wiki/building.html`, which all mention the aarch64 exception.

Delete this file and its row in `TODO/README.md` when done.
