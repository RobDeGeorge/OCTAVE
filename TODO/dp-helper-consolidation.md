# Consolidate the duplicated dp() / dpMin() QML helper

**Status:** deferred — blocked on an Android on-device verification
**Last updated:** 2026-05-24

## Why it's parked

**59 QML files** each define a private copy of:

```qml
// Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }
```

…instead of calling the canonical helper. `frontend/Spacing.qml` (a
`pragma Singleton`) **already exposes** `dp()` and `dpMin()` (L165–170), so the
duplication exists purely to dodge a historical **Qt bug where JS functions on a
QML singleton failed/returned undefined on Android**.

It's a real maintainability win to collapse 59 copies into one call site — but the
bug is **Android-specific and can't be verified from a desktop checkout**, and OCTAVE
ships an Android APK. If the bug still bites and we consolidate, **every screen's
pixel scaling breaks on Android**. So this must not be done blind.

## Evidence the bug may already be fixed (Qt 6.7.3)

`frontend/gauges/LinearGauge.qml` already calls `App.Spacing.dp(...)` directly
(many times) rather than using a local copy — and ships. If LinearGauge scales
correctly on an Android device, that's strong evidence the singleton-function bug
is fixed in the Qt version OCTAVE builds against.

## Verification prerequisite (do this first)

1. Build + install the Android APK on a real device.
2. Open a screen that calls `App.Spacing.dp()` directly — the OBD dashboard
   renders `LinearGauge`, which already does. Confirm gauges/text scale correctly.
3. Pay special attention to function calls inside **property bindings evaluated at
   component init** (the classic trigger for the old bug), not just post-load calls.

If scaling is correct → the bug is fixed → safe to consolidate.

## Work (once verified)

- Identify the files: `rg -l "work around Qt Android singleton-function bug|function dp\(s\)|function dp\(size\)" frontend` (~59).
- Remove each local `dp()` / `dpMin()` definition.
- Replace local `dp(x)` / `dpMin(x,f)` calls with `App.Spacing.dp(x)` / `App.Spacing.dpMin(x,f)`
  (or add a terse module-level alias if the verbosity is unwanted).
- Re-run `qmllint` and do a desktop + Android smoke pass.

## Cross-references

- Same 2026-05-24 cohesion pass as `TODO/wiki-purge-removed-python-android-docs.md` (the settings-icon conversion from that pass shipped 2026-08-08).
- Touches the same god-files as `TODO/god-object-splits.md`; either can go first.

Delete this file when all local copies are gone.
