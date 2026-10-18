# Check the 3D View first-tap freeze is gone on the phone

**Status:** code done 2026-10-18; needs a phone check
**Last updated:** 2026-10-18

**Goal:** confirm that the first tap on "3D View" from Sensor Home no longer freezes for about 1.2 s on the Galaxy S22.

What changed: SensorHome, on its first real visit, calls `mainWindow.prewarmPageSoon("CarMenu.qml")`, which adds CarMenu to the background pre-build queue (`pagePrewarmTimer.enqueue()` in `frontend/Main.qml`; same pacing and warm render as the startup pages). On desktop with `lastSensorPage` empty, CarMenu is no longer built at startup, and is built in the background a moment after Sensor Home appears (`[NAV] Pre-built CarMenu.qml in …` in the log).

## Check

1. Fresh install on the S22 (`adb uninstall org.octave.app`, then install), so `lastSensorPage` is empty.
2. Open Sensor Home, wait about 3 s, tap 3D View. It should open without the 1.2 s freeze (watch the frame gaps, or the `[NAV]` lines in `octave-cpp.log`).
3. On the Orange Pi under the roxy session: idle CPU with a hidden pre-built CarMenu (about +1 point was measured before, which is fine).

**Effort:** S. **Needs:** Galaxy S22, Orange Pi.

Delete this file when the check passes.
