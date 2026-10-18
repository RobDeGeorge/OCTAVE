# Pre-build the 3D Jeep page from Sensor Home

**Status:** deferred (found in 0.9.4 phone testing, not a regression)
**Last updated:** 2026-10-18

**Goal:** Pre-build the 3D Jeep page (CarMenu) once Sensor Home is on screen, so the first tap on "3D View" doesn't freeze for about 1.2 s.

## Problem

On a fresh install `lastSensorPage` is empty, so the startup pre-build queue (`frontend/Main.qml:650-668`, `pagePrewarmTimer`) builds SensorHome but not CarMenu. The "3D View" tile (`frontend/SensorHome.qml:183`, `pushSubpage()` at `:43`) then builds CarMenu cold: on the Galaxy S22, a 1.2 s freeze plus a 0.37 s gap. Later visits and later launches (when CarMenu has become `lastSensorPage`) are smooth. Pre-building CarMenu unconditionally at startup would cost time and memory for people who never open it.

## Steps

1. In `SensorHome.qml`, on its first `StackView.onActivated` (or the first time it becomes visible), when Quick3D is available (the existing probe `Qt.createComponent("CarMenu.qml", Component.PreferSynchronous)` at `:56`), call `stackView.prewarmPage("CarMenu.qml", {}, cb)`. Use the `prewarmPage()` helper from `Main.qml:526`, then `pageWarmRenderer.warm(page)`. Delay it about 500 ms and skip it while a finger is down (same `inputActivity` check as the queue at `Main.qml:645`).
2. Make sure `pushSubpage("CarMenu.qml")` then reuses the pre-built page. A tap while the build is in progress should finish that build rather than start a second one (see the "Pressed while prewarmPage() is still building it" path, `Main.qml:468`).
3. Guard it so it runs once per session.

**Effort:** S–M. **Needs:** the Galaxy S22 (fresh install, then the first tap on 3D View with the frame gaps measured) and the Orange Pi under the roxy session (idle CPU with a hidden CarMenu; about +1 point was measured before, which is fine).

Delete this file when done.
