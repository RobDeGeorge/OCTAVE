# Trip history: health trends across drives

**Goal:** show long-term engine health (coolant, voltage, fuel trims, oil temp) across weeks of recorded drives on top of the black box that now records every drive.

**Status:** parked until after v1.0. The recorder itself shipped on 2026-10-21: `TripRecorder` + `TripStore` (`src/managers/triprecorder.{h,cpp}`, `tripstore.{h,cpp}`) record every OBD reading of every drive into `<config>/history/blackbox.sqlite` and write one CSV + JSON per trip to `history/trips/`; `frontend/TripHistory.qml` lists drives, shows per-drive stats and a per-parameter chart, exports and deletes; settings `blackBoxEnabled` / `blackBoxSweepAll` / `blackBoxMaxMB`; `tst_triprecorder` covers it. What is left is the cross-drive view.

**Last updated:** 2026-10-21

---

## Why it's parked

v1.0 first. The per-drive data is on the box now, so this is a query + a page, with no new storage to get right.

## Plan

### 1. Health aggregates (C++)

**Effort:** M — desktop (sim) for the query, Jeep for the numbers

1. `TripStore::healthTrend(paramId, days)`: one row per trip (`trips.start_ms`, min / max / avg of the parameter, from `samples` or from the stored summary JSON) for the last N days; add a `TripRecorder::requestHealthTrend(paramId, days)` → `healthTrendReady(paramId, rows)` pair like `requestSeries`.
2. Derived numbers worth a trend: warm-up time (seconds from trip start until `COOLANT_TEMP` ≥ 80 °C), idle voltage (`CONTROL_MODULE_VOLTAGE` while `RPM` < 1000), long-term fuel trim average. Compute per trip at `endTrip()` and keep them in the summary JSON so the trend query is a JSON read, not a full scan.
3. Test in `tst_triprecorder`: three fake trips, the trend rows come back in date order with the right peaks.

### 2. Trends view (QML)

**Effort:** M — desktop (sim)

A "Trends" tab or section on `TripHistory.qml`: parameter chips, a bar/line per trip over the chosen window (7 / 30 / 90 days), the derived numbers above as small sparklines. Reuse the page's `Canvas` chart.

### 3. Docs

**Effort:** S

`wiki/obd-manager.html#black-box`, `pages.html#triphistory`, `signals-slots-reference.html#tripRecorder`; rebuild the search index.

## Later (not planned yet)

Shareable car cards and trip summaries exported as images, a gallery of shared dashboards (reusing `DashboardManager`'s export/import), and maybe a self-hostable, federated (ActivityPub) "OCTAVE hub" for car profiles. Anything online must be off by default, opt-in per category, stats-only, with privacy zones around home, never VIN/plates/speed traces publicly, and real deletion. It depends on `TODO/open-backend.md` (gateway) and a GPS source (`TODO/octave-companion-app.md`). Write it up as its own plan when step 2 above ships.

Delete this file when steps 1–3 have landed.
