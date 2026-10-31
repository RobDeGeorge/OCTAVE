# Dashboards: on-device checks

**Goal:** prove the dashboard editor and demand-driven polling work on a real touchscreen and a real car.
**Status:** active. Primitives, JSON dashboards, the in-app editor, renderer validation and user-folder hot-reload are all done and verified on desktop (2026-09-08 and 2026-10-04 passes).
**Last updated:** 2026-10-30

Done 2026-10-18: the Primitives Gallery is gone, and a dashboard with a newer `schema` shows "needs a newer OCTAVE" instead of its cells (`tst_vehiclegate`).

Dropped: the `"source": "Custom.qml"` QML fallback. It would load arbitrary code from a dashboard file, which is the risk `TODO/user-primitives.md` exists to design around.

## On-device checks

None of these can be done on the desktop rig.

**Touch (Galaxy S22 and the 7" Pi touchscreen):** effort M for the lot. The editor's touch redesign (`docs/DASHBOARD_EDITOR_REVIEW.md` Phase 1) has only run on the desktop and in `tst_dashboardwidgets`.
- [ ] Select-then-drag feel: the 8 dp threshold, press-and-hold to drag an unselected widget, the lift and the neighbours sliding aside live; zero accidental moves while tapping and swiping for 5 minutes.
- [ ] The floating toolbar never lands under the finger or off screen; every control is at least 9 mm on the 7" screen (`touchSize` uses `Screen.pixelDensity`: check the value it reports on each device).
- [ ] The default grid density a new dashboard gets on each screen, and Coarse / Normal / Fine rescaling an existing dashboard sensibly.
- [ ] Settings sheet: side sheet in landscape, bottom sheet in portrait, the zoomed canvas still readable; the on-screen keyboard doesn't cover the field being typed in.
- [ ] Palette and PID-picker sizing on a phone-sized screen.
- [ ] Portrait (Phase 3): every built-in on the S22 held upright (their `layouts.portrait`, Sport and TJ Wrangler hand-tuned), a user dashboard without one (derived), turning the phone mid-drive, and the editor opening on the portrait layout and switching back.
- [ ] Finger swipe between dashboards: a 20% drag or a flick past 8% commits, a short drag snaps back; taps on Media Keys / Now Playing inside a dashboard still register (the DragHandler sits on the page).
- [ ] Resize grip reachable at the canvas edges.
- [ ] Import "From file…" through the Android picker (content:// URL into `importDashboard`), and Export landing in Downloads/OCTAVE-dashboards.
- [ ] Phase 4 on a touchscreen: the import report popup after a real import, the Style menu and *Save as my widget…* name field with the on-screen keyboard, the gallery's Mine cards (Copy / × reachable without placing the widget), the *Alerts and auto-show…* popup's number fields.
- [ ] The browser editor (`/dashboards`) from the S22's browser against the Pi started with `--service-bind 0.0.0.0`: scan the chooser's **In a browser** QR, pair with the code, drag and resize by finger in the page, and the car's dashboard page following each change ("Preview from another device") and dropping the preview 60 s after the phone goes quiet.

**Real car (Orange Pi 5 Plus in the Jeep, or the Pi with an ECU simulator):** effort M for the lot.
- [ ] Parameter Cards: empty before the supported-PID scan, then the car's full list. Note the refresh rate with every supported PID demanded (ELM327 manages ~10–20 PIDs/s total).
- [ ] Switching pages changes what is polled (watch the OBD log), with no visible stall when the poll list is rebuilt.
- [ ] The RPM / ELM_VOLTAGE heartbeat keeps the data watchdog quiet on an empty demand (e.g. on Settings pages).
- [ ] A dashboard bound to a PID the car lacks shows "Not supported by this vehicle", and the PID picker greys it out but still allows it.
- [ ] The alert banner reads at a glance at its critical colour, and `showWhen` brings a dashboard up (e.g. coolant above a limit set low for the test) and hands back the one before 5 s after it clears.
- [ ] The editor's driving lock: it pauses above ~5 km/h and lifts below ~2.5, the banner reads at a glance, "I'm a passenger" works; and the polling-cost line's estimate (15 requests a second shared) against the refresh rate actually seen.

**Android BLE OBD (Galaxy S22 + BLE ELM327 in the Jeep):** effort S.
- [ ] The poll list rebuilds when demand changes. The `#ifdef Q_OS_ANDROID` path compiles in CI but has never run on a device.

Related: `TODO/user-primitives.md` (next dashboard feature; wants these checks done first), `TODO/android-cpp-port.md` (APK polish).

Delete this file when every check above is ticked.
