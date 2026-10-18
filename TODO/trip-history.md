# Trip and health history on the box (first step toward car profiles)

**Goal:** record each drive and the engine's long-term health numbers on the head unit, and show them on a Trip History page, with no network involved.

**Status:** parked until after v1.0. No work started: OCTAVE has no trip or history recording today (`OBDManager` only exposes live values plus a few counters like `distanceWithMIL`), and no GPS source on the Jeep box.

**Last updated:** 2026-10-18

---

## Why it's parked

v1.0 first. It's also a new persistent data store on a daily-driven box, so it needs care (SD-card writes, size caps, power cut mid-write).

## Plan

### 1. `TripRecorder` manager (C++)

**Effort:** M–L

1. New `src/managers/triprecorder.{h,cpp}`, listed in `CMakeLists.txt`, instantiated in `src/main.cpp`, registered as the `tripRecorder` context property.
2. **Demand:** polling is demand-driven, so the recorder must register its own PIDs with `obdManager.setParameterDemand("tripRecorder", ids)` (`obdmanager.h:234`), or nothing gets polled when no OBD page is open. Keep the list short (speed, RPM, coolant, oil temp, voltage, fuel level, short/long fuel trims, engine load) so it doesn't slow the live gauges; check poll rate on the Orange Pi.
3. **Inputs:** connect to `OBDManager`'s per-parameter signals (`speedMPHChanged`, `rpmChanged`, `coolantTempChanged`, `engineOilTempChanged`, `voltageChanged`, `fuelLevelChanged`, `shortTermFuelTrimChanged`, `longTermFuelTrimChanged`, `engineLoadChanged`, ...) and `connectionStatusChanged`.
4. **Trip detection:** start when connected and RPM > 0; end after N minutes of RPM 0 or a disconnect (a short OBD dropout must not split a trip).
5. **Per trip, summarise:** start/end time, duration, distance (integrate speed), max speed, peak coolant and oil temps, min/max voltage, fuel used (fuel-level delta). Store the summary, plus a downsampled time series (e.g. 1 sample / 5 s).
6. **Storage:** `<config>/history/` (`SettingsManager::getAppDataDir()`), one JSON file per trip plus a daily health-aggregate file. Write with the same atomic-write approach `SettingsManager` uses. Cap total size; downsample, then delete, the oldest trips.
7. **QML API:** `trips` (list of summaries), `loadTrip(id)` (series), `healthTrend(paramId, days)`, `deleteTrip(id)`, `clearHistory()`, `exportHistory()` (to Downloads, like `DiagnosticsManager::exportLogs()`).

### 2. Settings

**Effort:** S

`tripHistoryEnabled` (default on, local only), `tripHistoryRetentionDays`, `tripHistoryMaxMB` in `buildDefaultSettings()` / the settings registry, with an OBD settings card (toggle, retention, "Clear history").

### 3. Tests

**Effort:** M

`tests/cpp/tst_triprecorder.cpp`: drive the recorder with the simulated car (`tst_obdsim` pattern, `src/dev/carsim` scenarios); assert trip start/stop, a dropout not splitting a trip, summary maths, retention pruning, and a truncated file being skipped. Temp `XDG_CONFIG_HOME`.

### 4. Trip History page

**Effort:** M

New page reached from `CarMenu.qml` or `OBDMenu.qml`: trip list (date, distance, duration, peaks), a trip detail view with `SparklineGauge`-style charts, and a health-trends view (coolant, voltage, fuel trims over weeks).

### 5. Docs

**Effort:** S

Wiki: new section on `obd-manager.html` (or its own page), `settings-reference.html`, `settings-manager.html`, `pages.html`, `signals-slots-reference.html`; rebuild the search index.

## Later (not planned yet)

Once local history exists: shareable car cards and trip summaries exported as images, a gallery of shared dashboards (reusing `DashboardManager`'s export/import), and maybe a self-hostable, federated (ActivityPub) "OCTAVE hub" for car profiles. Anything online must be off by default, opt-in per category, stats-only, with privacy zones around home, never VIN/plates/speed traces publicly, and real deletion. It depends on `TODO/backend-service-split.md` (gateway) and a GPS source (`TODO/octave-companion-app.md`). Write it up as its own plan when step 4 above ships.

Delete this file when steps 1–5 have landed.
