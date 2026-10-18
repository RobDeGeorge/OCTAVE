# Dashboards: on-device checks

**Goal:** prove the dashboard editor and demand-driven polling work on a real touchscreen and a real car.
**Status:** active. Primitives, JSON dashboards, the in-app editor, renderer validation and user-folder hot-reload are all done and verified on desktop (2026-09-08 and 2026-10-04 passes).
**Last updated:** 2026-10-18

Done 2026-10-18: the Primitives Gallery is gone, and a dashboard with a newer `schema` shows "needs a newer OCTAVE" instead of its cells (`tst_vehiclegate`).

Dropped: the `"source": "Custom.qml"` QML fallback. It would load arbitrary code from a dashboard file, which is the risk `TODO/user-primitives.md` exists to design around.

## On-device checks

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

Delete this file when every check above is ticked.
