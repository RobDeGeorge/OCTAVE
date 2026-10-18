# Dashboards: last cleanup and on-device checks

**Goal:** remove the leftover dev gallery, handle too-new dashboard files politely, and prove the dashboard editor and demand-driven polling work on a real touchscreen and a real car.
**Status:** active. Primitives, JSON dashboards, the in-app editor, renderer validation and user-folder hot-reload are all done and verified on desktop (2026-09-08 and 2026-10-04 passes).
**Last updated:** 2026-10-18

Dropped: the `"source": "Custom.qml"` QML fallback. It would load arbitrary code from a dashboard file, which is the risk `TODO/user-primitives.md` exists to design around.

## 1. Remove the Primitives Gallery

The dev showcase is still in the app though its own comment says to remove it once the editor shipped (it has).

1. `frontend/OBDMenu.qml`: delete the gallery button (comment at line 1219, `onClicked: primitivesGalleryPopup.open()` at 1277) and the `primitivesGalleryPopup` Popup (about lines 1555–1787). Fix the comment at line 943 that sizes another popup "to match primitivesGalleryPopup".
2. Delete `frontend/GalleryTile.qml` (used only by that popup).
3. Remove gallery mentions from `wiki/gauges-dashboards.html`, `pages.html`, `components.html`, `frontend-overview.html`, `obd-manager.html`, `docs/GAUGE_AUTHORING.md` and the CLAUDE.md "Gauges & Dashboards" paragraph; run `python wiki/build_search_index.py`.
4. Build, `qmllint`, and open the OBD page with `./build/octave --dev` to check the header still lays out.

**Effort:** S. **Needs:** desktop only.

## 2. "Requires a newer OCTAVE" placeholder

Import already rejects `schema > kSupportedSchema` (`src/managers/dashboardmanager.cpp:577-581`), but a newer file dropped straight into `<config>/dashboards/` is rendered as if it were schema 1. In `frontend/dashboards/DashboardRenderer.qml`, if `spec.schema` is greater than the supported version (expose `kSupportedSchema` from `DashboardManager` or mirror it), render one centred "This dashboard needs a newer OCTAVE" message instead of the cells. Add a case to the dashboard QtTest if the manager side changes.

**Effort:** S. **Needs:** desktop only.

## 3. On-device checks

None of these can be done on the desktop rig.

**Touch (Galaxy S22, sideload APK):** effort M for the lot.
- [ ] Drag feel, palette and PID-picker sizing on a phone-sized screen.
- [ ] Finger swipe between dashboards: a 20% drag or a flick past 8% commits, a short drag snaps back; taps on Media Keys / Now Playing inside a dashboard still register (the DragHandler sits on the page).
- [ ] Resize grip (44dp target) reachable at the canvas edges.
- [ ] Import "From file…" through the Android picker (content:// URL into `importDashboard`), and Export landing in Downloads/OCTAVE-dashboards.

**Real car (Orange Pi 5 Plus in the Jeep, or the Pi with an ECU simulator):** effort M for the lot.
- [ ] Parameter Cards: empty before the supported-PID scan, then the car's full list. Note the refresh rate with every supported PID demanded (ELM327 manages ~10–20 PIDs/s total).
- [ ] Switching pages changes what is polled (watch the OBD log), with no visible stall when the poll list is rebuilt.
- [ ] The RPM / ELM_VOLTAGE heartbeat keeps the data watchdog quiet on an empty demand (e.g. on Settings pages).
- [ ] A dashboard bound to a PID the car lacks shows "Not supported by this vehicle", and the PID picker greys it out but still allows it.

**Android BLE OBD (Galaxy S22 + BLE ELM327 in the Jeep):** effort S.
- [ ] The poll list rebuilds when demand changes. The `#ifdef Q_OS_ANDROID` path compiles in CI but has never run on a device.

Related: `TODO/user-primitives.md` (next dashboard feature; wants these checks done first), `TODO/android-cpp-port.md` (APK polish).

Delete this file when sections 1–3 are done.
