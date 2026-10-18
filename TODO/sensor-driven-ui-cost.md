# Sensor-driven UI cost: remaining idle and on-screen overhead

**Status:** code done for all three (2026-10-18); what is left is measuring on the phone and the Pi
**Last updated:** 2026-10-18

**Goal:** Cut the remaining CPU and battery cost of sensor and OBD values: stop phone sensors nobody is using, and stop the OBD card grid redrawing every card on every value.

0.9.4 fixed hidden pages redrawing on sensor readings (idle is about 10% of a core on the Pi, and about 23% / 10 fps on the S22).

## 1. Android: sensors stopped while nothing uses them (verify on the S22)

Done in code 2026-10-18, untested on a phone: the mobile `BerryIMUManager::setActive(false)` (`src/managers/berryimumanager.cpp`) now stops `m_accel`, `m_compass` and `m_pressure`; `setActive(true)` restarts them when the IMU is enabled; `setEnabled(true)` probes with `start()` and stops again if no page is active; `connected` stays true while paused. The consumers that keep it active are unchanged (`updateSensorConsumers()` in `frontend/Main.qml`); the Settings status card reads only the connection status. Compiles for Android (`build-android-rhea`).

Left, on the S22 (sideload APK):
1. Sensor Home, the 3D view, OBD dashboards and a home screen with an IMU card still update live; leaving them stops the readings (`phone sensors paused` / `running` in `octave-cpp.log`).
2. Settings > Accessories still shows the IMU as connected while paused.
3. Re-measure idle on the home screen: pidstat for 20 s (`QtSensorsLooper` should be near 0) and a screenrecord frame count, against the ~7% of a core and ~10 redraws/s from 0.9.4 testing.
4. Tare (`calibrateTare()`, CarMenu) reads `m_accel->reading()`, which is stale while the sensors are paused. It's only reachable from an active page today, so it's fine; anything that adds a tare elsewhere (e.g. a Settings button) must start the sensors first.

**Effort:** S. **Needs:** the Galaxy S22.

## 2. OBD Parameter Cards grid on screen: about 106% CPU on the phone (measure on the S22 and Pi)

Done 2026-10-18 (option 1): each card binds to `OBDParameterModel.valueHolder(id).value`, which changes only when that PID is in a fold, instead of `paramValues`, which every fold replaces. So with the grid on screen, only the cards whose value moved re-evaluate and restart their `Behavior`s (`tst_vehiclegate` checks a fold touches only its own holder). On desktop under Xvfb the CPU difference is within noise (software rendering dominates), so the win has to be measured where the problem was seen.

Left: with the grid on screen and live OBD (or the simulated car via `--dev`), measure on the S22 and the Orange Pi (pidstat 20 s, `qt.scenegraph` frames/s) against the 106% figure. If it's still high, the next steps are skipping the `Behavior` for changes below display precision, and folding sensor-derived params at a lower rate in `_flushTimer`.

**Effort:** S (measuring), M if more work is needed. **Needs:** the Galaxy S22 and the Orange Pi.

## 3. Full Grid: "Object or context destroyed during incubation" warnings (check on the S22)

Seen on the first visit to OBD with the Full Grid dashboard (about 8 `CircularGauge.qml` warnings, cosmetic). Not reproducible on desktop (2026-10-18: zero warnings with Full Grid active, both when OBD was pre-built and when it was opened before the pre-build reached it). As a defensive fix, `OBDMenu.qml` now reads the spec through `_specFor(id)`, a per-id cache cleared on `dashboardsChanged`, so a re-evaluation of the `spec` binding during activation hands the renderer the same object instead of a fresh `loadDashboard()` result.

Left: on the S22, open OBD for the first time with Full Grid active and check the log for the warning. If it's gone, delete this item; if not, find which dependency of the `spec` binding (`frontend/OBDMenu.qml`, `activeDashboardRenderer`) flips during activation on the phone.

**Effort:** S. **Needs:** Galaxy S22.

## Order

1 (biggest battery win on phones), then 2, then 3. Delete this file when all three are done.
