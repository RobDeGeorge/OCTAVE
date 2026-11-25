# Home as a dashboard: the car check, then the classic Home goes

**Goal:** Home is one dashboard among the others (drawn by `DashboardRenderer`, edited in the dashboard editor), with nothing of the hand-built Home left in the tree.

**Status:** active. The code is done on desktop (2026-11-24); what is left waits on the Jeep's head unit and a phone.
**Last updated:** 2026-11-24

**In-app editing removed (2026-11-24):** Rob builds dashboards with Claude as JSON files and loads them (`<config>/dashboards/`, picked in Settings > Display > Home Screen). The touch editor, its tiled rework of that day, the OBD page's chooser and the built-in dashboards other than `home` were deleted from the tree (the tiled editor is kept as `dev/patches/2026-11-24-tiled-editor.patch`, against the commit before). Left to decide: whether a tap on a Home widget opens its page again (`HomeDashboard.tapsOpenPages`, false).

**Hand-built screens (2026-11-24):** six Home screens outside the grid were added in `frontend/home/` (Cockpit, Glass, Trail, then Retro, Rings, Telemetry; Settings > Display > Home Screen), as a first round of styles for Rob to react to. Seen on desktop only (simulated car and IMU, 1280x720 and 720x1180); not yet on the Pi, where Glass's blurred backdrop is the one to watch for cost.

**Picker (2026-11-24):** Settings > Display > Home Screen is a grid of cards with a picture of each Home (`homeScreenCards`; pictures in `frontend/assets/home/`); the dashboard / classic switch is gone and Classic is one of the cards. When a screen's look changes, re-shoot its picture.

## Why it is parked here

Home is on screen most of the drive, and the Orange Pi is the machine it has to be cheap on. The dashboard Home was built and driven on desktop only (`--sim` under Xvfb at 1280x800 and 800x1280), so the hand-built Home was kept one switch away (Settings > Display > Layout > Home Screen, the `homeStyle` setting) until the checks below are done. Until then the tree carries both, and the settings that only the classic Home reads.

What exists: `frontend/HomeDashboard.qml` (loaded by `frontend/MainMenu.qml`), `dashboardManager.homeDashboardId` / `setHomeDashboard`, the built-in `frontend/dashboards/presets/home.json`, `DashboardManager::homeSpec` / `migrateClassicHome` (`src/managers/dashboardmanager.cpp`), the renderer's taps (`interactive`, `cellTapped`, `pressAndHold`), `VehicleWidget`, `ShiftLightWidget`, the readout's `card` style and the cell `frame`. Reference: `docs/GAUGE_AUTHORING.md` §5.5, `wiki/pages.html#home-dashboard`.

## What is left

1. **The Pi check.** **Effort:** S. **Needs:** Pi, in the Jeep.
   - Home idle and while driving, dashboard against classic (switch in Settings): CPU of the `octave` process and frame time (`--profile`, or `top`). The cells are async `Loader`s with `clip`, the compass is a `Canvas`, the blurred art is a `MultiEffect` layer.
   - The migration on the Jeep's real config: the first start should write "My Home" from the card list in use there (no shift light: Home no longer gets one by itself).
   - A press held on the touchscreen opens the editor; a tap on each widget opens its page (off since 2026-11-24, `HomeDashboard.tapsOpenPages`: decide whether it comes back); the progress bar seeks without opening the player.
   - With the adapter unplugged: "No vehicle connected" over the readings and its Connect button.
2. **The phone check.** **Effort:** S. **Needs:** S22.
   - Portrait Home on the phone's size; the music prompt and the Connect button reachable; long-press does not fight scrolling titles.
3. **Delete the classic Home.** **Effort:** M. **Needs:** desktop, after 1 and 2.
   - `classicHomeComponent` in `frontend/MainMenu.qml` (and what only it uses there: the title scrollers, the slider), `frontend/HomeOBDView.qml`, `frontend/HomeVehicleCard.qml`, `frontend/HomeSensorPanel.qml`, the `homeStyle` switch (`frontend/settings/DisplaySettingsPage.qml`), the card list in `frontend/settings/OBDSettingsPage.qml` and "Show on Home" in `frontend/settings/DeviceSettingsPage.qml`.
   - `settingsManager.homeOBDParameters` and its calls, `maxHomeObdParameters`, `homeVehicleCard` (deprecated now) are a breaking change to stable members: remove them with the next protocol bump (`TODO/fix-up-leftovers.md` has the protocol 4 list), and move the glass and console example frontends off them first (`examples/frontends/glass/src/ui/HomeParams.svelte`, `examples/frontends/console/octave_console/panes.py`, `examples/frontends/PARITY.md`).
   - Wiki: the "Classic layout" half of `wiki/pages.html#mainmenu`, `components.html` (HomeOBDView, HomeVehicleCard, HomeSensorPanel), `settings-reference.html`.
4. **Editor options that are spec-only today.** **Effort:** S. **Needs:** desktop.
   - `tapAction` is honoured from a spec but not offered in the editor (it would do nothing in the OBD menu, where dashboards are display only). Offer it once a second interactive host exists, or only when editing Home.
5. **The other fixed dashboards.** **Effort:** L. **Needs:** desktop, then Pi.
   - The Sensors page (`frontend/SensorMenu.qml`, `SensorCard.qml`, `GForceDisplay.qml`) and the OBD menu's Parameter Cards are the same job done by hand. Same move: a built-in dashboard each, the page a renderer. Parameter Cards lists every PID the car reports, so it needs a generated spec, not a preset.

## Related

- `TODO/dashboards-roadmap.md`: the editor's own touchscreen and in-car checks; do them in the same visit as step 1.
- `TODO/user-primitives.md`: composites would let a "reading card with a shift light" be one widget.
- `TODO/sensor-driven-ui-cost.md`: Home now asks for the full IMU rate only when its dashboard shows the motion sensor.

## Order

1, 2 in one car visit; then 3; 4 and 5 whenever. Delete this file (and its row in `TODO/README.md`) when 3 is done and 4, 5 have moved to their own files or been dropped.
