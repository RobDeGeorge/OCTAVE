# Sensor-driven UI cost: remaining idle and on-screen overhead

**Status:** deferred (measured in 0.9.4 pre-release testing; all regressions fixed, these are what's left)
**Last updated:** 2026-09-27

## Background

0.9.4 pre-builds pages at startup and keeps them alive hidden. Testing on the Orange Pi (BerryIMU) and a Galaxy S22 (phone motion sensors) found hidden pages redrawing on every sensor reading. Those were fixed: the OBD grid follows live values only while it's on screen, CarMenu follows the IMU only while visible, MediaRoom ignores position ticks while hidden, and the Android sensors are throttled to 5 Hz in software while no page uses them. Idle is now 10% of a core on the Pi (Home, paused, 4-5 fps), the same as without pre-building. On the phone it went from ~80% / 119 fps down to ~23% / 10 fps. Three things remain.

## 1. Android: sensors keep running while nothing uses them

`BerryIMUManager` (mobile branch, `src/managers/berryimumanager.cpp`, after `#else // Q_OS_MOBILE`) now drops accel/compass readings to 5 Hz while inactive. But Android still delivers ~25 readings/s to `QtSensorsLooper` (~7% of a core), and two 5 Hz streams still cause ~10 redraws/s. When `setActive(false)` is called and no page shows sensor values, stop `m_accel` / `m_compass` (and `m_pressure`), then restart them in `setActive(true)`. Check what still needs readings while inactive: the Settings status, and the Home IMU card when it's configured (`Main.qml` `updateSensorConsumers()` / `_homeShowsImuParam()`).

## 2. OBD Parameter Cards grid on screen: ~106% CPU on the phone at full vsync

With the grid visible, `OBDParameterModel.paramValues` is replaced up to ~30x/s, so every card re-evaluates and restarts its 50/100 ms `Behavior` animations (`frontend/OBDMenu.qml`, the `parametersGrid` Repeater). That happens even though only 7 sensor values change slowly. Options: bind each card to its own value instead of the whole `paramValues` object (a per-parameter signal or model role), skip the Behavior when the change is below display precision, or cap the fold rate in `OBDParameterModel._flushTimer` for sensor-derived params.

## 3. Full Grid: "Object or context destroyed during incubation" on a real first visit

Visiting OBD with the Full Grid dashboard logs about 8 `CircularGauge.qml: Object or context destroyed during incubation` warnings. That happens with or without pre-building (it's cosmetic, and the gauges appear within ~330 ms). `DashboardRenderer`'s async widget Loaders are torn down mid-incubation, probably because `spec` re-evaluates (a new object from `dashboardManager.loadDashboard()`) during activation. Find which dependency changes, and keep `spec` stable across the Activating→Active transition.

## Order

Do 1 first (biggest battery win on phones), then 2, then 3. Re-measure on the Pi and phone the same way as in 0.9.4: pidstat 20 s, qt.scenegraph "frame rendered" per second, screenrecord frame count on Android.

Delete this file when done.
