# Compile the QML ahead of time (Windows first-launch stall)

**Status:** deferred (found in 0.9.5 RC testing, 2026-10-17; predates 0.9.5)
**Last updated:** 2026-10-18

**Goal:** Ship pre-compiled QML so a fresh install doesn't spend seconds compiling it before the first frame.

## Why it's parked

On Windows, 2 of 5 fresh first launches stalled for 10–20 s before the first frame. A second launch never stalls. 0.9.4 showed about 7 s. adb was measured at ≤1.1 s cold, so it isn't the cause. The fix changes how the frontend ships on every platform (self-update, AppImage, Windows, APK), so it's too risky to slip into a point release.

Evidence (rc3, kora, 5 fresh installs):
- The first run creates `%LOCALAPPDATA%\OCTAVE\OCTAVE\cache\` with `qmlcache` (59 .qmlc files), `qtpipelinecache-*` and `_qt_QGfxShaderBuilder_6.7.3`.
- First log line to first frame took 2.9–6.0 s. The largest gap was while `Main.qml` compiles (up to 4.9 s).
- Watchdog stalls appeared in 0 of the 5 runs, plus one 7.5 s stall in the automated run, which matches 0.9.4.
- Logs are in `_OCTAVE_rc/v0.9.5/results/0.9.5-rc3/win11-firstlaunch/`.
- Defender scanning the new DLLs and the D3D pipeline cache may add to it. Compiling the QML ahead of time only removes the QML part.

Today QML is loaded at runtime from `frontend/` (`CMakeLists.txt:364-367`, "loaded at runtime (not compiled into resources)"). Windows reads it from `<appDir>/frontend`, the AppImage from `usr/frontend`, and Android bundles it via `QT_QML_ROOT_PATH` (`CMakeLists.txt:406-407`).

## Steps

1. Check what depends on loose QML files: the Pi self-updater (`git pull` and restart reads `frontend/` straight from the checkout), dev hot-reload, the `src/dev/qml/` overlay, the user dashboards in `<config>/dashboards/` (JSON, not affected), and the qmllint CI pass.
2. Pick an approach:
   - (a) `qt_add_qml_module` with qmlcachegen / the Qt Quick Compiler, putting QML in `qrc:/`. This means fixing relative imports, `Qt.resolvedUrl` and `Qt.createComponent("X.qml")` paths (e.g. `SensorHome.qml:56`), and `App.*` singleton imports.
   - (b) Smaller: keep loose files but ship the `.qmlc` cache, generated at build time with `qmlcachegen` next to each `.qml`.
3. Wire it in for each package (Windows installer, AppImage, APK, Linux arm64), keeping the source-checkout and dev paths working.
4. Measure the first launch on 5 fresh installs, the same way as rc3.

**Effort:** L. **Needs:** the kora Win11 VM (fresh installs), the Orange Pi (self-update path) and the Galaxy S22 (APK smoke test).

Delete this file when the first launch no longer compiles QML.
