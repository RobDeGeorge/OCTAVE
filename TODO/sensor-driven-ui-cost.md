# Sensor-driven UI cost: remaining idle and on-screen overhead

**Status:** deferred (measured in 0.9.4 testing; the hidden-page regressions are fixed, these three are what's left)
**Last updated:** 2026-10-18

**Goal:** Cut the remaining CPU and battery cost of sensor and OBD values: stop phone sensors nobody is using, and stop the OBD card grid redrawing every card on every value.

0.9.4 fixed hidden pages redrawing on sensor readings (idle is about 10% of a core on the Pi, and about 23% / 10 fps on the S22).

## 1. Android: stop the sensors while nothing uses them

Mobile `BerryIMUManager::setActive(false)` (`src/managers/berryimumanager.cpp:1209-1216`) only lowers the data rate to 5 Hz, and `idleThrottled()` (`:1091`) drops readings in software. Android still delivers about 25 readings/s to `QtSensorsLooper` (about 7% of a core), and two 5 Hz streams still cause about 10 redraws/s.

Steps:
1. In the mobile `setActive(false)`, stop `m_accel`, `m_compass` and `m_pressure` (if `m_hasPressure`). In `setActive(true)`, start them again, but only if `m_enabled`. Keep `m_connected` true, since only the sensor is paused, not disconnected.
2. Check what still needs readings while inactive: the Settings IMU status card, and the Home IMU card when it's configured (`frontend/Main.qml:930-940`, `updateSensorConsumers()` / `_homeShowsImuParam()`). Those must keep `setActive(true)`.
3. Re-measure on the S22: pidstat for 20 s, and screenrecord frame count.

**Effort:** S–M. **Needs:** the Galaxy S22.

## 2. OBD Parameter Cards grid on screen: about 106% CPU on the phone

`OBDParameterModel.paramValues` is replaced up to about 30 times a second (`frontend/OBDParameterModel.qml:233`, `_flushTimer` at `:236-237`, 33 ms). Every card in `frontend/OBDMenu.qml`'s `parametersGrid` (`:508`) reads `paramValues[modelData.id]` (`:531-534, 619, 675`) through the page-level `paramValues` binding (`:271`), so every card re-evaluates and restarts its `Behavior` animations (`:536, 681`) even when only a few values changed.

Steps (pick one, or combine them):
1. Bind each card to its own value: a per-parameter signal (`paramChanged(id, value)`) or a model role, so only the card whose value changed updates.
2. Skip the `Behavior` when the change is below display precision.
3. Fold sensor-derived params at a lower rate in `_flushTimer`.

Then re-measure on the phone and Pi with the grid on screen (pidstat 20 s, `qt.scenegraph` frames/s).

**Effort:** M. **Needs:** the Galaxy S22 and the Orange Pi (live OBD, or the simulated car via `--dev`).

## 3. Full Grid: "Object or context destroyed during incubation" warnings

The first visit to OBD with the Full Grid dashboard logs about 8 `CircularGauge.qml: Object or context destroyed during incubation` warnings. They're cosmetic (the gauges appear within about 330 ms). The cause is that `spec` is bound to `(visible && _pageActive && ...) ? dashboardManager.loadDashboard(...)` (`frontend/OBDMenu.qml:474-475`). It gets a new object as `visible` and `_pageActive` flip during Activating→Active, which tears down `DashboardRenderer`'s async Loaders mid-incubation.

Steps: load the spec once per `activeDashboardId` (or on a dashboard-changed signal) into a plain property, and gate rendering on visibility separately. Confirm the warnings are gone with `./build/octave --dev`.

**Effort:** S–M. **Needs:** desktop only.

## Order

1 (biggest battery win on phones), then 2, then 3. Delete this file when all three are done.
