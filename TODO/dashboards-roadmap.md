# Dashboards Roadmap — "Tony Hawk Create-A-Park for OBD Dashboards"

**Status:** Phases 1–3 complete and **verified on desktop 2026-09-08** — `dev/tests/drive_dashboard_editor.py` runs ~45 assertions against the live app (place / bind / drag / resize / options / save / edit / duplicate / delete / draft rescue / hot reload), all passing with a clean QML log. The Phase 2 fast-follow (renderer validation + user-dir hot reload) also landed 2026-09-08. Bugs fixed during verification: deleting the active dashboard left it selected (delegate context died mid-handler → moved into a page-level function + registry-level fallback), StackView anchors warning on the editor page, clipped option labels. **Only remaining item: a ten-minute on-device pass on the Android tablet** (touch drag feel, palette/picker sizing) — delete this file once that's done.
**Last updated:** 2026-09-08

---

## Vision

End goal: users build their own OBD dashboards from within the app, Tony-Hawk-create-a-park style. Drag gauge primitives onto a grid, wire them to PIDs, tweak thresholds and colors, save with a name. No code edits required.

Today: dashboards are **JSON specs** under `frontend/dashboards/presets/`, rendered by `frontend/dashboards/DashboardRenderer.qml` and enumerated by `DashboardManager` (C++ `src/managers/dashboardmanager.{h,cpp}` + Python `backend/dashboard_manager.py`), which populates `OBDMenu.qml`'s `dashboardRegistry`. Four built-ins ship: Sport, Minimal, Full Grid, Performance.

---

## Why phased

The editor (Phase 3) is only tractable on top of a data-driven dashboard format (Phase 2). Without that, every "save my custom dashboard" click would have to generate QML source, which is a mess. The hinge is turning dashboards into JSON. Phase 1 is independent groundwork that's low-risk and immediately useful on its own.

---

## Phase 1 — More primitives *(complete)*

Add gauge primitives to `frontend/gauges/` to widen what can be composed into a dashboard. Still hand-writing dashboard QML files; no new systems.

### Current primitives
- `CircularGauge` — arc + ticks + optional needle + readout
- `BarGauge` — horizontal/vertical filled bar with warn threshold
- `LinearGauge` — horizontal scale with bidirectional fill (for values that straddle zero)
- `DigitalReadout` — big number, no chrome

### Planned additions (this phase)
- [x] **`ArcGauge`** — half-circle (180°) meter, opens downward. Wide-aspect alternative to CircularGauge for compact speedometer/RPM layouts. *(delivered)*
- [x] **`SparklineGauge`** — rolling history line showing recent values. Great for boost, load, trim trends. Timer-sampled to keep the x-axis scale stable. *(delivered)*
- [x] **`WarningLight`** — binary indicator bound to a threshold condition (`triggerAbove` / `triggerBelow`). Small, composable, overlays nicely on other gauges. *(delivered)*

### Candidates deferred within Phase 1
Pick these up if a dashboard needs them — low priority until a concrete use case lands:

- **`SegmentedBar`** — staircase/boost-gauge style bar with discrete cells. Aesthetic; could be achieved with BarGauge + visual styling for now.
- **`MultiReadout`** — stacked mini digits for related PIDs (e.g. short/long fuel trim side-by-side). Niche; compose from multiple DigitalReadouts first.
- ~~**`CompassGauge`** — rotation-based dial. Only useful once we have heading data; BerryIMU publishes magnetometer but there's no `HEADING` PID yet.~~ *(delivered 2026-06-10: `HEADING` + 7 more BerryIMU parameters now flow through `OBDParameterModel`, and `CompassGauge`/`GForceGauge` plus music widgets (`NowPlayingWidget`, `MediaControlsWidget` in `frontend/dashboards/widgets/`) shipped as self-binding editor widgets — `supportedKinds: []` hides the PID picker for them.)*

Each new primitive requires:
1. Implement in `frontend/gauges/` following the shared binding API (see `docs/GAUGE_AUTHORING.md` §3 and §7)
2. Update `docs/GAUGE_AUTHORING.md` in the same commit
3. Update `wiki/gauges-dashboards.html` + run `python wiki/build_search_index.py`

**Exit criteria for Phase 1:** at least one new hand-written dashboard showcasing each of the new primitives in use. Something like a `PerformanceDashboard.qml` with sparklines for load/boost/trim, an ArcGauge for speed, and WarningLights for overtemp/overrev.

---

## Phase 2 — JSON-defined dashboards *(substantially landed)*

**This is the hinge.** Do not attempt Phase 3 without this.

**Landed:** `DashboardRenderer.qml` (spec → grid); four JSON presets in `frontend/dashboards/presets/` (`sport`, `minimal`, `fullgrid`, `performance`) all carrying `"schema": 1`; `DashboardManager` shipped in **both** backends (`src/managers/dashboardmanager.{h,cpp}` and `backend/dashboard_manager.py`) and wired into `OBDMenu.qml` via `dashboardRegistry` / `_rebuildRegistry()`.
**Still to verify / finish:** spec validation against `OBDParameterModel` + the type whitelist (work item 5), the `"source"` QML-fallback field (item 6), user-dashboard dir hot-reload, and a side-by-side visual parity check vs. the old hand-written versions.

### Core idea

Stop hand-writing dashboard QML files. Define dashboards as data:

```json
{
  "id": "sport",
  "label": "Sport",
  "schema": 1,
  "gridColumns": 12,
  "gridRows": 6,
  "cells": [
    {
      "type": "DigitalReadout",
      "paramId": "SPEED",
      "col": 0, "row": 0, "colSpan": 6, "rowSpan": 4,
      "props": { "valueScale": 7.0, "padDigits": 3 }
    },
    {
      "type": "CircularGauge",
      "paramId": "RPM",
      "col": 6, "row": 0, "colSpan": 6, "rowSpan": 4,
      "props": { "showNeedle": true, "redlineStart": 6500 }
    },
    {
      "type": "BarGauge",
      "paramId": "COOLANT_TEMP",
      "col": 0, "row": 4, "colSpan": 3, "rowSpan": 2,
      "props": { "warnAbove": 105 }
    }
  ]
}
```

### Work items

1. **`frontend/dashboards/DashboardRenderer.qml`** — single component that takes a spec (JS object) and instantiates cells into a `GridLayout` via `Qt.createComponent`. Replace all three current dashboards' hand-written QML with this + a spec.
2. **Spec location split:**
   - Built-ins: `frontend/dashboards/presets/*.json` (read-only, shipped in the bundle)
   - User-defined: `~/.config/OCTAVE/dashboards/*.json` on Linux (or platform equivalent via `QStandardPaths::AppConfigLocation`) — writable, survives across installs; parallels the existing `settingsConfigure.json` persistence pattern in `src/managers/settingsmanager.cpp`
3. **Registry refactor in `OBDMenu.qml`:** instead of hardcoding `dashboardRegistry`, populate it from (built-in presets ∪ user dashboards). `src/managers/dashboardmanager.{h,cpp}` (C++ / Qt 6) enumerates both sources and exposes the list as a QML context property, with a parallel `backend/dashboard_manager.py` peer for the Python dev backend (dual-backend parity — see CLAUDE.md). *(shipped)*
4. **Schema versioning:** include `"schema": 1` in every spec so the format can evolve. If `DashboardRenderer` sees a newer schema than it supports, degrade gracefully (render a "this dashboard requires a newer OCTAVE" placeholder).
5. **Validation:** on load, verify every `paramId` exists in `OBDParameterModel.allParameters` and every `type` exists in a small whitelist (the primitives listed above). Skip invalid cells with a log warning rather than crashing the whole dashboard.
6. **Keep QML fallback:** a `"source": "path/to/Custom.qml"` field lets a dashboard bypass the JSON renderer entirely when someone needs to do something the grid can't express. Retains the current hand-written option for power users.

### Key design decisions already made
- **Grid over absolute positioning** — every current dashboard is grid-shaped anyway, and grids are dramatically easier to edit on a touchscreen. 12×6 is a reasonable default resolution.
- **`"schema": 1`** baked in from day one so future format changes don't strand existing files.
- **Built-in vs user** — built-ins are read-only; "Duplicate" on a built-in copies it to the user dir, fully editable.

### Dependencies
- Phase 1 gauge primitives stabilized (don't want to rename props mid-migration)
- No backend dependencies

### Exit criteria for Phase 2
- All three built-in dashboards converted to JSON specs
- `DashboardRenderer` renders them identically to the current hand-written versions (side-by-side visual comparison)
- Dropping a new JSON file in the user dashboards dir (no code changes, no restart hook yet) shows up in the chooser after reopening the app

---

## Phase 3 — In-app editor ("create-a-park mode") *(in progress)*

The editor is fundamentally "a GUI for the Phase 2 JSON format." Phase 2 is in
place, so this is unblocked.

### The backend is already done

**Critical correction to the original plan:** `DashboardManager` (C++
`src/managers/dashboardmanager.{h,cpp}` **and** Python
`backend/dashboard_manager.py`, full parity) already ships everything the editor
needs to persist:

- `saveDashboard(QVariantMap spec) -> QString id` — writes user JSON; **requires
  both `id` and `label` in the spec**, rejects collisions with a built-in id,
  returns `""` on failure. (User-id collisions overwrite — the editor must
  generate a unique id itself; see below.)
- `deleteDashboard(QString id) -> bool` — removes user JSON, refuses built-ins
- `duplicateDashboard(QString sourceId, QString newLabel) -> QString id` —
  clones to user dir with an auto-slugified unique id
- `loadDashboard(QString id) -> QVariant`, `isBuiltIn(QString id) -> bool`,
  `refresh()`, and the `dashboardsChanged()` signal (already auto-rebuilds
  `OBDMenu.qml`'s `dashboardRegistry`).

**Consequence: the editor MVP is pure QML.** No `CMakeLists.txt` / `main.py`
churn, no C++↔Python parity work for the MVP. The only thing the backend
doesn't expose is "generate a unique id from a label" — the editor does this in
QML (slugify + uniquify against `dashboardManager.dashboards`), mirroring the
manager's internal `_slugify`/`_uniqueId` logic, so no new slot is needed.

### Interaction model — HYBRID (amended 2026-06-10, shipped)

Originally tap-to-place only (decided 2026-05-31). Amended per user preference:
the drag-vs-scroll worry only ever applied to the chooser's Flickable, and the
editor canvas is a full-screen non-scrolling page, so drag is safe there. The
shipped model:

- **Tap an empty grid cell** ([+] target) → palette popup → pick a primitive →
  it fills that cell at its default span (clamped + shrunk to fit free space),
  and the PID picker opens immediately.
- **Drag a placed cell** → moves it, snapping to the grid on release. A live
  highlight shows the candidate cell (accent = valid, danger = overlap/out of
  bounds); invalid drops bounce back.
- **Tap a placed cell** → selects; the properties panel populates (PID,
  position/size `◄ ►` steppers, curated per-type props, remove).
- **Tap the background** → deselects.

### Chooser affordances — EXPLICIT BUTTONS, not long-press

Carry the same reliability principle into the chooser: each card gets small,
visible **Edit / Duplicate / Delete** action buttons (revealed on the card)
rather than a hidden long-press, plus a permanent **New** button. Long-press is
discoverability-hostile on a glance-and-go in-car UI.

### Architecture

```
frontend/dashboards/
├── WidgetCatalog.qml (singleton)   NEW — single source of truth: type → QML url
│                                   + display name + default span + curated
│                                   editable props. Registered in frontend/qmldir.
├── DashboardRenderer.qml           refactored to read its registry from WidgetCatalog
└── editor/                         NEW subtree
    ├── DashboardEditor.qml         full-screen page (StackView-pushed): top bar
    │                               (name, Save, Cancel) + canvas + panels
    ├── EditorCanvas.qml            renders the working spec; empty cells = [+] tap
    │                               targets; placed cells get selection border
    ├── PalettePopup.qml            the 7 primitives; tap one to fill the chosen cell
    ├── PropertiesPanel.qml         inspector for the selected cell
    └── PIDPicker.qml               searchable PID list, grouped by OBDParameterModel `kind`
```

`WidgetCatalog` is the key consolidation: the type→url map currently lives inline
in `DashboardRenderer.qml`. Lifting it into a singleton lets the renderer,
palette, and properties panel agree on the 7 supported types (and their editable
props) without drift. It also becomes the natural home for the type whitelist the
deferred renderer validation will use.

### Milestones (MVP-first) — ALL LANDED 2026-06-10

**A — Scaffold + prove the save round-trip.** ✓ `WidgetCatalog` singleton;
`DashboardRenderer` refactored onto it; `DashboardEditor.qml` shell (name field,
Save, Cancel, live canvas via `DashboardRenderer`, plus a *temporary* "add sample
gauge" button); a *temporary* "New" button in the chooser that pushes the editor.
Exit: create a (near-empty) dashboard, name it, Save, see it appear in the
chooser and render. De-risks the StackView push + `saveDashboard` loop before any
editing UI exists.

**B — Place / select / edit (hits the Phase 3 exit criterion).** ✓ `EditorCanvas`
with `[+]` empty-cell targets + selection overlay; `PalettePopup`;
`PropertiesPanel` with `PIDPicker` + span steppers + delete-cell. After this you
can tap New, place gauges, bind each to a PID, name it, Save — no files touched.

**C — Chooser affordances.** ✓ Permanent New button; Edit / Duplicate / Delete
action buttons on cards (Edit/Delete user-only; Duplicate on all). Remove the
temporary scaffolding buttons from Milestone A and the placeholder comment at
`OBDMenu.qml:632`.

**D — Polish.** ✓ Curated per-type threshold props in the panel (redline for
Circular/Arc, `warnAbove` for Bar, `triggerAbove/Below` for WarningLight — all
declared in `WidgetCatalog` with honest defaults); reposition shipped as
drag-to-move (supersedes the tap-to-reposition idea); `docs/GAUGE_AUTHORING.md`
§5/§7/§9 + `wiki/gauges-dashboards.html` updated and search index rebuilt.

### Fast-follow (the deferred Phase 2 polish)

Renderer `paramId`/`type` validation (against `OBDParameterModel.allParameters`
and the `WidgetCatalog` type list) and user-dir hot-reload. Not MVP blockers —
the editor only ever emits valid specs — fold in after Milestone B.

### Dependencies
- Phase 2 complete ✓
- No backend dependencies for the MVP (see "backend is already done" above)

### Exit criteria for Phase 3
- A new user can tap "New," place three gauges onto the canvas, bind each to a
  PID, name the dashboard, save, and see it appear in the chooser alongside the
  built-ins — all without touching a file

---

## Cross-references

- `TODO/god-object-splits.md` — if `OBDMenu.qml` is on the split list, coordinate: the chooser popup and dashboard registry logic will move during Phase 2 and would interact with any split plan.

---

## Order of operations

1. ~~Phase 1 — primitives + GAUGE_AUTHORING.md + wiki.~~ ✓
2. ~~Ship a dashboard showcasing the new primitives (Phase 1 exit criteria).~~ ✓
3. ~~Phase 2 JSON spec — migrate dashboards, add `DashboardRenderer`, user dir.~~ ✓
4. **Phase 3 editor (now)** — tap-to-place, MVP-first. Milestones A→B→C→D above.
   Then fold in the deferred Phase 2 validation/hot-reload fast-follow.

---

**Delete this file when Phase 3 ships.** Until then, keep it updated as each phase's state changes.
