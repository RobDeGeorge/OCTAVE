# Compile the QML ahead of time

**Status:** parked; measured on 2026-11-11, the gain is small and it is not the Windows first-launch stall
**Last updated:** 2026-11-11

**Goal:** Ship pre-compiled QML so a first launch doesn't compile it, if a slow device turns out to need that.

## Why it's parked

This file used to blame the Windows first-launch stall (2 of 5 fresh installs took 10 to 20 s to the first frame in 0.9.5 RC testing) on QML being compiled. kora measured it on the Win11 VM with the published 0.9.6 (37 launches, 2 vCPUs, no GPU; `_OCTAVE_rc/v0.9.7/results/firstlaunch-split/SUMMARY.txt`), and it is not the cause:

- Loading `Main.qml` ("Scanned dashboards" to "Restoring playback state") takes 0.77 s warm and 1.05 to 1.16 s with only `qmlcache` deleted: about 0.3 s. On Linux (zion, Qt 6.12) the difference is 0.05 s.
- The D3D11 pipeline cache and the shader cache cost nothing measurable.
- A reboot with every cache warm costs more than the QML cache does: 1.6 to 1.9 s (DLLs read for the first time).
- A fresh install loads in 1.4 to 1.6 s, first frame after 1.8 to 2.3 s. The very first launch on a pristine Windows, 30 s after boot: 3.0 s, first frame after 4.1 s, with Defender busy.
- The stalls (7.8, 15.0 and 14.9 s, with watchdog lines) came in two minutes during which Windows Update and the Store were installing app packages, a few minutes after the image's first boot. One of the three was a warm launch. The time before the first log line grew too. The launches right after were normal.

So the stall follows what a new Windows does in its first minutes, and nothing in OCTAVE's packaging removes it. A first-launch check on a fresh VM should wait for Windows to settle, or note what it was installing.

What a cold QML cache does cost is the background pre-building of pages after the first frame: 2.5 s warm, 4.2 s with `qmlcache` deleted, 5 to 5.7 s on a fresh install (`SettingsMenu.qml` alone 0.5 s against 0.9 to 1.2 s). That runs while the home page is already usable.

The work below changes how the frontend ships in every package (Windows, AppImage, APK, the Pi's self-update), which is too much for 0.3 s of load on a PC. It stays here for one case that has not been measured: a Raspberry Pi or the Orange Pi, where compiling the QML (59 `.qmlc` files after a first run) may cost seconds.

## Steps

1. Measure on a Pi first: the same gap and the pre-build time, with `~/.cache/OCTAVE/OCTAVE/qmlcache` deleted and warm, three launches each. Under 1 s of difference: delete this file. **Effort:** S. **Needs:** the Orange Pi or a Raspberry Pi.
2. Check what depends on loose QML files: the Pi self-updater (`git pull` and restart reads `frontend/` straight from the checkout), dev hot-reload, the `src/dev/qml/` overlay, the qmllint CI pass, and the C++ that reads data files from the frontend folder (`themes.json`, fonts, dashboard presets, `widget-catalog.json`, templates; `findFrontendDir()` in `src/main.cpp`). **Effort:** S. **Needs:** nothing.
3. Pick an approach. **Effort:** M. **Needs:** nothing.
   - (a) `qt_add_qml_module` with qmlcachegen / the Qt Quick Compiler, putting QML in `qrc:/`. This means fixing relative imports, `Qt.resolvedUrl` and `Qt.createComponent("X.qml")` paths (e.g. `SensorHome.qml:56`), and `App.*` singleton imports.
   - (b) Smaller: keep loose files and ship a `.qmlc` next to each `.qml`, generated at build time with `qmlcachegen` (Qt looks there before its cache folder). The cache is only used when it matches the Qt build and the source file's time stamp, which squashfs (AppImage, whole seconds) and APK assets (none) may not preserve; try it in a package before believing it.
4. Wire it in for each package (Windows installer, AppImage, APK, Linux arm64), keeping the source-checkout and dev paths working. **Effort:** L. **Needs:** the kora Win11 VM, the Orange Pi (self-update path), the Galaxy S22 (APK smoke test).
5. Measure again on the Pi and on a fresh Windows install, the same way. **Effort:** S. **Needs:** the Pi, the kora Win11 VM.

Order: 1 decides whether 2 to 5 happen at all.

Delete this file when step 1 says it isn't worth it, or when a first launch no longer compiles QML.
