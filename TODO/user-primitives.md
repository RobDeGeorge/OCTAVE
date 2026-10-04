# User-Created, Shareable Dashboard Primitives

**Status:** parked (design agreed in principle, not started)
**Last updated:** 2026-10-04

---

## What the user asked for

"Allowing people to create their own primitives to share would be dope." Today a
dashboard can only use the 11 widgets built into `frontend/gauges/` and
`frontend/dashboards/widgets/`, listed in `frontend/dashboards/WidgetCatalog.qml`.
Dashboards themselves are already shareable (export / copy as text / import, landed
2026-10-04, see `DashboardManager::importDashboard*`). This item adds the same for the
building blocks.

## Why it's parked

Sharing a *dashboard* is safe because a dashboard is pure data: the importer
(`sanitizeImportedSpec` in `src/managers/dashboardmanager.cpp` and
`backend/dashboard_manager.py`) keeps only known keys and scalar props, and nothing in
the file runs.

A primitive is different. The obvious implementation — drop a `.qml` file into a
user `primitives/` folder and add it to the catalog — means **running someone
else's code inside the head unit**. A QML file loaded into the main engine can:

- call every context property: `obdManager.clear_dtc()`, `settingsManager.save_setting(...)`,
  `downloadManager`, `phoneMirrorManager`, the ESP32 / I²C managers;
- `import` any app file by absolute path, reach singletons, and walk the object tree;
- use `XMLHttpRequest` / `Qt.openUrlExternally` for network access or to open URLs.

Shared via a forum or a chat, that becomes a malware vector in a car. It needs a
deliberate design, not a quick folder scan, so it was split out of the 2026-10
dashboard pass.

## Recommended design: two tiers

### Tier 1 — data-only "widget presets" and composites (safe, do this first)

Most of what people will want to share is *a look*, not new drawing code:

1. **Widget presets**: a named bundle of props for an existing primitive, e.g.
   "Redline Tach" = `CircularGauge` + `{sweepAngle: 240, majorTickCount: 9,
   redlineStart: 6500, fillColor: "#E74C3C", showNeedle: true}`. The palette shows
   presets next to the base widgets; placing one copies its props into the cell.
2. **Composites**: a small grid of existing primitives that places and moves as one
   unit (e.g. "Trim Pair" = two `DigitalReadout`s stacked, "Boost cluster" = Arc +
   Sparkline + WarningLight). Stored as a mini dashboard spec (same cell schema)
   with relative coordinates; the renderer expands it into its sub-cells.

Both are pure JSON, so they reuse the existing sanitizer and share pipeline
(`exportDashboard` / `importDashboardFromText` → add `"kind": "preset" | "composite"`
or a sibling `exportPreset` / `importPreset` pair). No code runs, and both
backends only need JSON I/O (parity is cheap).

Work items:
- Schema: `{"schema": 1, "kind": "widgetPreset", "label", "baseType", "props"}` and
  `{"schema": 1, "kind": "composite", "label", "gridColumns", "gridRows", "cells"}`.
- Storage: `<app data>/widgets/*.json` scanned by `DashboardManager` (or a new
  `WidgetLibraryManager` in both backends), hot-reloaded like dashboards.
- `WidgetCatalog`: merge user presets into `widgets` at runtime (catalog entries
  gain `"source": "builtin" | "user"`, `"baseType"`).
- Editor: "Save as preset" on the properties panel; the palette gets a "Mine"
  section; the PID picker / panel treat a preset as its base type.
- Composites: renderer + editor canvas expand them; moving and resizing act on the group.

### Tier 2 — code primitives (QML), opt-in only

For genuinely new visuals (e.g. a bar-graph tach, a boost "staircase"). Only consider
this after Tier 1 ships, and only with all of these:

- **Off by default**, behind a "Developer: allow custom code widgets" setting, desktop
  first. On mobile, store review rules (Play / App Store, see `OCTAVE_ENABLE_DOWNLOADS`)
  probably forbid downloaded executable code anyway.
- **Never installed by a dashboard import.** A shared dashboard that references an
  unknown type keeps rendering it as "missing widget" (the renderer already skips
  unknown types). Code widgets are installed separately, with an explicit trust
  screen showing the author, the file hash and a "this can control your car's
  systems" warning.
- **Isolation**: load through a dedicated `QQmlContext` whose context properties
  shadow every manager with `null`, plus a narrow bridge object (`widgetApi`) that
  exposes read-only `value(paramId)`, `paramInfo(paramId)` and theme colors. Block
  file and network access: a `QQmlNetworkAccessManagerFactory` that refuses all
  requests, and keep `QML_XHR_ALLOW_FILE_READ` unset. Reject files whose `import`
  lines reference anything other than `QtQuick`, `QtQuick.Shapes` and the `widgetApi`
  module (this is a static check before loading).
- Treat it as best effort, not a sandbox. QML has no real security boundary, so the
  trust prompt is the actual control. Document that plainly.

## Prerequisites / cross-references

- Builds on the 2026-10-04 dashboard pass: `WidgetCatalog` capability fields
  (`labelProps`, `rangeProps`, `colorProps`, typed `editableProps`) are what a preset
  or code widget must declare. See `docs/GAUGE_AUTHORING.md` §7.
- `TODO/dashboards-roadmap.md`: the remaining on-device checks there should land first.
- `TODO/god-object-splits.md`: if `OBDMenu.qml`'s chooser is split out, do that before
  adding a "Mine" palette section and a widget-library UI.

## Recommended order

1. Tier 1 widget presets (schema, storage in both backends, "Save as preset", palette
   section, share/import).
2. Tier 1 composites (renderer + canvas grouping).
3. Re-ask the user whether Tier 2 is still wanted once presets exist. It may not be.
4. Tier 2 only with the isolation list above, and desktop first.

Delete this file when Tier 1 ships and Tier 2 is either built or explicitly dropped.
