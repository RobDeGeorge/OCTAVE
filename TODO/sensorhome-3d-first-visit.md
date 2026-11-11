# Check the 3D View first-tap freeze is gone on the phone

**Status:** code done 2026-10-18; needs a phone check
**Last updated:** 2026-11-10

**Goal:** confirm that the first tap on "3D View" from Sensor Home no longer freezes for about 1.2 s on the Galaxy S22.

What changed: SensorHome, on its first real visit, calls `mainWindow.prewarmPageSoon("CarMenu.qml")`, which adds CarMenu to the background pre-build queue (`pagePrewarmTimer.enqueue()` in `frontend/Main.qml`; same pacing and warm render as the startup pages). On desktop with `lastSensorPage` empty, CarMenu is no longer built at startup, and is built in the background a moment after Sensor Home appears (`[NAV] Pre-built CarMenu.qml in …` in the log).

Since 0.9.6-rc3 the request is honoured only when a GPU is known to draw the window (`mainWindow.gpuRendered`, set by `GraphicsRenderer::report()` in `src/util/graphicsrenderer.cpp`): on software rendering (llvmpipe VMs, the emulated Pi image) the hidden warm render of CarMenu stalled the GUI thread for 13-19 s and tripped the UI watchdog. The phone (OpenGL ES, Adreno / Mali / Xclipse) counts as a GPU; the log says `Window drawn by OpenGL: <name> (GPU)` at startup, or `[NAV] Not pre-building CarMenu.qml: no GPU known to draw it (...)` if it does not.

## Check

1. Fresh install on the S22 (`adb uninstall org.octave.app`, then install), so `lastSensorPage` is empty.
2. Open Sensor Home, wait about 3 s, tap 3D View. It should open without the 1.2 s freeze (watch the frame gaps, or the `[NAV]` lines in `octave-cpp.log`).
3. On the Orange Pi under the roxy session: idle CPU with a hidden pre-built CarMenu (about +1 point was measured before, which is fine).

**Effort:** S. **Needs:** Galaxy S22, Orange Pi.

## Follow-up: Windows

4. Direct3D 11 (Qt's default on Windows) and Vulkan do not name their adapter through `GraphicsRenderer::describe()`, so Windows builds CarMenu on the tap as 0.9.5 did. To pre-build there too, read the adapter from `QRhi::driverInfo()` (`QQuickWindow::rhi()`, Qt 6.6+; `deviceType` tells an integrated / discrete GPU from the CPU "Microsoft Basic Render Driver") and extend `isGpu()` and `tst_graphicsrenderer`. **Effort:** S. **Needs:** a Windows machine with a GPU and the GPU-less win11 VM.

Delete this file when the check passes and step 4 is done or dropped.
