# Consolidate the duplicated dp() / dpMin() QML helper

**Status:** deferred (blocked only on a ~10-minute Android check)
**Last updated:** 2026-10-18

**Goal:** Replace the 63 private copies of `dp()` / `dpMin()` in QML files with the single shared one in `Spacing.qml`.

## Why it's parked

63 QML files under `frontend/` define their own `function dp(size)` / `dpMin(size, floor)` (43 of them carry the comment "work around Qt Android singleton-function bug"). The `pragma Singleton` `frontend/Spacing.qml` already has both (`:165-171`). The copies exist only to dodge an old Qt bug where JS functions on a QML singleton returned undefined on Android. If that bug still bit, removing them would break scaling on every Android screen.

The bug looks fixed: 17 files already call `App.Spacing.dp(...)` directly, including in bindings evaluated at component init. They have shipped in every APK since Android started working (0.9.3): all gauges in `frontend/gauges/`, `dashboards/DashboardRenderer.qml`, `dashboards/widgets/{NowPlaying,MediaControls}Widget.qml`, and `settings/widgets/{About,Device,OBD}Widget.qml`, `WidgetPill.qml`, `WidgetChip.qml`.

## Steps

1. **Check (about 10 min, Galaxy S22):** install the current APK and open an OBD dashboard (CircularGauge / LinearGauge) and the Settings widgets page. Confirm the gauges, text and pills are scaled, not tiny or zero-sized. If they look right, the blocker is cleared.
2. List the files: `rg -l "function dp\((size|s)\)" frontend` (63).
3. Remove each local `dp()` / `dpMin()` and change the calls to `App.Spacing.dp(x)` / `App.Spacing.dpMin(x, f)`. Check that each file imports the `App` qualifier, and add `import "." as App` (or the right relative path) where it's missing.
4. Run `qmllint` (the CI pass), then a desktop smoke pass (`./build/octave --dev` and the screenshot tooling over every page) and an Android smoke pass on the S22.

**Effort:** M (mechanical, 63 files). **Needs:** the Galaxy S22 for steps 1 and 4.

Touches the same large files as `TODO/god-object-splits.md`; either can go first. Delete this file when all local copies are gone.
