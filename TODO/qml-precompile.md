# Compile the QML ahead of time (Windows first-launch stall)

**Status:** deferred; the premise is in doubt, measure before building anything (see "Check first")
**Last updated:** 2026-11-11

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

## Check first (2026-11-11): is it the QML at all?

A Linux measurement says no. A copy of the debug build with an empty cache (`octave --smoke`, Qt 6.12, zion) logs "Main.qml loaded" at 0.87 s and writes 52 `.qmlc` files; with the cache warm it is 0.81 to 0.82 s. Compiling the QML costs about 0.05 s there. A slow VM and Qt 6.7.3 will be slower, but not by the 2 to 5 s seen on Windows.

In the rc3 logs the gap is between "Scanned dashboards" and "Restoring playback state", which is `engine.load(Main.qml)`: 4.9, 2.2, 2.9, 2.2 and 3.0 s on the five fresh installs. That call also creates the window and the first scene, so the D3D11 pipeline and shader caches written by the same run (`qtpipelinecache-*`, `_qt_QGfxShaderBuilder_*`) and DLLs loaded for the first time (Defender) sit in the same gap. The VM draws with "Direct3D 11 (no GPU known)". There is no warm Windows number to compare with.

Asked of kora on 2026-11-11 (results in `_OCTAVE_rc/v0.9.7/results/firstlaunch-split/` when run): the same gap on the published 0.9.6 with nothing deleted, with only `qmlcache` deleted, only `qtpipelinecache-*`, only `_qt_QGfxShaderBuilder_*`, the whole cache dir, after a reboot with the caches intact, and on a fresh install.

- If deleting `qmlcache` alone brings the stall back: carry on with the steps below.
- If it is the pipeline / shader cache: pre-compiling the QML does not help. Look at what the first frame draws (the home page's effects, `MultiEffect` / `ShaderEffect` users) and at shipping the pipeline cache or drawing a plain first frame.
- If it is cold DLLs (the reboot case is as slow as a fresh install): nothing in the frontend helps; a splash drawn before `engine.load` is the honest fix.

Rewrite or delete this file once the numbers are in.

## Steps (only if the QML is the cause)

1. Check what depends on loose QML files: the Pi self-updater (`git pull` and restart reads `frontend/` straight from the checkout), dev hot-reload, the `src/dev/qml/` overlay, the user dashboards in `<config>/dashboards/` (JSON, not affected), and the qmllint CI pass.
2. Pick an approach:
   - (a) `qt_add_qml_module` with qmlcachegen / the Qt Quick Compiler, putting QML in `qrc:/`. This means fixing relative imports, `Qt.resolvedUrl` and `Qt.createComponent("X.qml")` paths (e.g. `SensorHome.qml:56`), and `App.*` singleton imports.
   - (b) Smaller: keep loose files but ship the `.qmlc` cache, generated at build time with `qmlcachegen` next to each `.qml`.
3. Wire it in for each package (Windows installer, AppImage, APK, Linux arm64), keeping the source-checkout and dev paths working.
4. Measure the first launch on 5 fresh installs, the same way as rc3.

**Effort:** L. **Needs:** the kora Win11 VM (fresh installs), the Orange Pi (self-update path) and the Galaxy S22 (APK smoke test).

Delete this file when the first launch no longer compiles QML.
