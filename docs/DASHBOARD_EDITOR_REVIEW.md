# Dashboard editor review — 2026-10-30

**Goal:** decide what the dashboard creator must become to be a reason people pick OCTAVE, by auditing ours against the editors that large user bases already use.

**Verdict:** the foundation is right and better than most competitors'. The editing experience on top of it is a desktop/mouse design squeezed onto touch, and that is what makes it feel bad. Almost every problem is in the six QML files under `frontend/dashboards/editor/` and the widgets themselves, not in the data model, so it can be fixed without throwing work away.

Sources:
- Code read in full: `frontend/dashboards/editor/*.qml`, `DashboardRenderer.qml`, `widget-catalog.json`, the chooser in `frontend/OBDMenu.qml` (~l. 926-1500), `src/managers/dashboardmanager.*`.
- Earlier captures: `dev/dashboard-audit/editor-final.png`, `palette-final.png` (1280×720), and [DASHBOARD_ASSET_AUDIT.md](DASHBOARD_ASSET_AUDIT.md) for the widgets.
- Competitor studies, every claim with a source link: [automotive apps](research/dashboard-editor-2026-10-30/automotive-apps.md) (RealDash, Torque Pro, Car Scanner, OBD Fusion/OBDLink, DashCommand, RaceChrono, MoTeC/Haltech/Holley/AiM/Ecumaster) and [touch layout editors](research/dashboard-editor-2026-10-30/touch-layout-editors.md) (iOS widgets and Watch faces, Pixel/Nova, KWGT, Home Assistant Sections, Garmin, Grafana, Figma/FigJam, Stream Deck/Touch Portal, mobile game HUD editors, Apple/Material/NN/g guidance).

Not done: nothing was tried on a real touchscreen for this review. The app was not launched because it picks up the bundled adb while the phone is reachable. Pixel figures below are computed from the QML geometry at scale 1.0.

---

## 1. What we have that competitors don't (keep all of it)

| OCTAVE today | Competitor reality |
| --- | --- |
| Widgets are single semantic units from one catalog (`widget-catalog.json`) | RealDash gauges are stacks of arc/text/value/unit parts selected one at a time; its own developer says touch editing is "too clumsy", and a third party built an AI editor to cope |
| Resolution-independent grid spec | RealDash dashboards are locked to the aspect ratio they were saved at; Torque layouts depend on DPI; DashCommand keeps one skin per aspect |
| Data-only, sanitized import; ids always regenerated; import never overwrites | Car Scanner and OBD Fusion imports can overwrite every dashboard |
| Vehicle-aware PID picker (supported first, others greyed) | Matches Car Scanner / OBDLink best practice |
| Demand-driven polling of exactly the PIDs on screen | Fusion users see refresh drop past four gauges with no explanation |
| Simulator + demo data so you can build without a car | Car Scanner is praised precisely for this |
| Draft rescue when the editor is destroyed | Lost work is a top complaint across Torque, RealDash, Garmin |
| The whole manager is on the service API | Enables the "edit from your phone/laptop" pattern (Touch Portal, CarPlay, Garmin Connect) that nobody in the OBD space has |

## 2. What is wrong, by severity

### Critical — makes the editor unusable or untrustworthy on touch

1. **Targets are far below any touch minimum.** New dashboards are 12×6 with 16 dp gutters and every free cell is its own `+` button (`EditorCanvas.qml:115`).
   - 390×844 phone: canvas ≈ 358 px wide → cell ≈ **12 px** wide.
   - 800×480 head unit: cells ≈ **46×28 px**; the 12×8 presets give ≈ **46×17 px** rows. The gutters alone take 112 of ~288 px of height.
   - Guidance: Apple 44 pt, Material 48 dp + 8 dp spacing, NN/g 1 cm, in-vehicle research 19-22 mm.
2. **Drag starts on first touch.** The widget's `MouseArea` has `drag.target` set and selects on `onPressed` (`EditorCanvas.qml:267-298`): any finger that lands on a widget and wiggles moves it. This is the exact complaint that has Grafana withdrawing phone editing on 2026-11-05 ("near impossible to scroll ... without moving a panel"). No lift state, no haptic, no offset; the finger covers what it moves.
3. **No undo.** Delete is one tap with no confirm and no undo (`PropertiesPanel.qml:631`). Every editor studied either has undo or users complain that it doesn't. Apple HIG: "Prefer letting people undo".
4. **Rejected drops bounce back silently** (`DashboardEditor.qml:214-228`). There is no push-aside or swap, so rearranging a full dashboard means shrinking or deleting something first. A red highlight shows during the drag but no reason is given.
5. **The settings panel covers the work.** It is a fixed 300 dp panel floating over the canvas (`DashboardEditor.qml:503`). At 1280×720 it hides a third of the canvas, including part of the selected row (see `editor-final.png`); on a phone it covers essentially everything. The comment explains why it floats (a docked panel rescaled the dashboard mid-drag), which is a real problem that needs a different answer, not an overlay.

### High — the experience feels clumsy and unfinished

6. **Blank-canvas start.** "+ New" always opens an empty 12×6 spreadsheet of `+` cells. Built-ins can't be edited, only Copied, then found, then edited. Every successful consumer flow (Car Scanner, Garmin, Apple Watch, AiM, Haltech, RaceChrono) starts from a layout template with slots.
7. **Every edit rebuilds every widget.** All mutations reassign `cells`, so the `Repeater` destroys and synchronously reloads every widget (`DashboardEditor.qml:11`, `EditorCanvas.qml:204`). Typing a title resets every sparkline's history and replays every needle animation. It also costs a lot on a Pi.
8. **Settings are badly prioritized.** The panel order is type → PID → **Column/Row/Width/Height steppers** → options → label and range → color → delete. The things people change most (data, title, color, warning) are below four layout steppers. Raw text fields are used for numbers, enums show raw values (`vertical`), and nothing explains a setting.
9. **The widget palette doesn't show what you get.** It uses glyph icons (`○`, `◠`, `▮`); Bar and Segmented share an icon; "Clock" shows as `12`. There are no descriptions, no live previews, no sizes, and no grouping.
10. **The PID picker fights touch.** It calls `forceActiveFocus()` on search when opened (`PIDPicker.qml:44`), so the on-screen keyboard covers half the list. Groups are by unit kind ("Percentage", "Temperature") rather than by what drivers think in (engine, fuel, electrical, motion). It has no recents and no live values.
11. **Resize is one bottom-right grip only.** A widget at the right or bottom edge can only grow left or up by moving it first. The grip's hit area is 44 dp, but its hit area is inside the next cell when cells are this small.
12. **Navigating away loses context.** The persistent bottom bar stays live in the editor; one tap on it destroys the editor (work survives only as a "▲ Draft" button). Cancel's two-tap "Discard?" is nonstandard and times out after 3 s.

### Medium — missing what good editors and pro tools have

13. **One layout for every screen.** A dashboard built on a 1280×720 desktop renders squeezed on a 390×844 phone. The asset audit shows the presets failing in portrait. There is no portrait variant and no preview at another size. This is the #1 structural complaint in the automotive study.
14. **Range and warning are tangled.** There is one `redlineStart` / `warnAbove` per widget and no warning-vs-critical bands. There is no way to test a value (MoTeC, Ecumaster, DashXL and SimHub all have a test-value slider); "Demo data" is a global sweep you can't aim.
15. **No polling-cost feedback.** A 17-PID dashboard on an ELM327 updates each value at ~1 Hz, and the editor gives no hint of that.
16. **No reuse.** There is no copy/paste style, no "apply to all gauges", and no duplicate-widget action ("Doing this manually is taking forever!" — an OBD Fusion user).
17. **The grid is fixed per file with no UI.** New dashboards are 12×6, but every built-in preset is 12×8 (and TJ is 12×12), and nothing in the editor changes it.
18. **Widget quality undercuts everything.** From the asset audit: missing data renders as 0 (and lights a low-fuel warning), BarGauge's vertical layout breaks, and readouts clip in small cells. An editor whose widgets look broken at the sizes it lets you pick will feel bad however good the interactions are.

## 3. What the market teaches (condensed)

The patterns that showed up across both studies, with where they come from:

1. **Template first, slots second.** Garmin (pick a layout of N fields, then tap a field to choose data), the Apple Watch face editor, Car Scanner (layout, then tick values), AiM page styles. Most users never need free layout.
2. **An explicit edit mode, and select before you manipulate.** In FigJam and PUBG/CoD Mobile HUD editors (hundreds of millions of touch users), a tap selects and only the selected item drags. OBDLink uses explicit Move and Resize modes. Nothing moves in view mode.
3. **Size presets, not free resizing.** iOS 18 widgets snap to declared sizes; Home Assistant uses min/max via `getGridOptions`; Android declares resize ranges. Handles are a secondary path.
4. **Neighbors get out of the way.** NN/g: animate neighbors in ~100 ms when the dragged item's centre crosses their edge. Pixel Launcher pushes into free cells; Garmin uses tap-tap to swap. Counter-lesson: Home Assistant replaced Masonry because global auto-packing moved things the user never touched. Push or swap locally, never re-pack the whole dashboard.
5. **Undo plus a session Save/Cancel.** Show "Undo" in a snackbar after delete instead of a confirm dialog.
6. **Few settings up front, the rest folded away, and an escape hatch.** Home Assistant has a form, a live preview and a YAML editor; Apple's widget HIG favours minimal configuration. KWGT's "everything at once" panel is the cautionary example.
7. **Data-first add.** Home Assistant's "by entity": pick Coolant Temp and the app suggests a suitable card. We already have `supportedKinds` and `kind` to do this well.
8. **Layouts per orientation or screen class, derived once and then owned by the user.** Home Assistant keeps a fixed column count; react-grid-layout has per-breakpoint layouts; DashCommand has a view per aspect.
9. **Pros separate display range from alarm bands** (Normal / Warning / Critical with colour, flash and tone; "only when engine running"; acknowledge), test with a simulated value, and switch pages on conditions.
10. **Heavy editing happens on the big screen; the car screen does pick/swap/size.** RealDash, MoTeC, AiM and DashXL are PC-first; Touch Portal and CarPlay edit from the phone with a live result on the target. We can do both, because the editor is QML that runs on desktop and WASM and the manager is on the service API.
11. **Don't edit while moving.** NHTSA's 2 s per glance / 12 s per task limits.

## 4. Proposed design

### 4.1 Flow

```
Dashboards chooser
  └─ + New ──► Template gallery (live previews with demo data)
                 Hero + 4 · 2 up · 2×2 · 3×2 · Six-field strip · Track · Economy
                 Towing temps · Media + gauges · Blank
                 └─ slots pre-filled with PIDs this car supports, or empty "+" slots
  └─ Edit (user dash) / Customize (built-in: forks a copy silently, named "Sport (mine)")
                 └─ Editor
```

### 4.2 Editor screen

- **Full screen.** The bottom nav bar is hidden while editing; leaving goes through Done or Close only.
- **Top bar:** `✕ Close` · `↶ Undo` `↷ Redo` · name (tap to rename) · screen preview switcher `▭ Landscape / ▯ Portrait / 📱 Phone` · `Done`. Done saves; Close with changes asks once, with Save / Discard / Keep editing.
- **Canvas:** a faint dot grid. The default density is chosen so a cell is at least ~12 mm on the current screen (see §5, decision 1). There are no `+` buttons in every cell. A tap on empty space shows one ghost at the default size with **Add here**, and a floating **+ Add** button is always present.
- **Select, then manipulate:**
  - Tap selects. A selected widget lifts (scale 1.03 + shadow) and shows a floating toolbar beside it, never under the finger: **Data · Style · Size (S/M/L/Wide/Tall chips) · Duplicate · Delete**.
  - Only a selected widget drags, after ~8 dp of movement. The widget keeps the finger's grab offset, and a ghost shows the drop cell.
  - Crossing a same-size neighbour **swaps** it; otherwise neighbours are **pushed** into the nearest free cells (100 ms ease). If nothing fits, the ghost turns red with a reason: "No room — make it smaller or move something".
  - **Tap-tap:** with a widget selected, tapping another widget offers Swap, and tapping an empty cell offers Move here. This needs no drag, is precise, and is accessible.
  - Resize uses the size chips first. Corner grips (48 dp hit area, drawn outside the widget) snap only to the widget's declared sizes, with a "4 × 2" readout away from the finger.
- **Delete** shows a snackbar: "Gauge removed · Undo".
- **Settings sheet.** A bottom sheet in portrait, a side sheet in landscape. The canvas **zooms to fit the remaining area at a fixed scale while the sheet is open**, and the selected widget is kept in view. This replaces the overlay without the rescale-during-drag problem, because dragging is disabled while the sheet is open.
  - **Data:** PID button (live value shown).
  - **Look:** title, unit, colour swatches (theme default), style options as labelled segmented controls with icons.
  - **Range & alerts:** min/max (Auto or a value), Warning above/below, Critical above/below, flash. A **Test value** slider drives this widget only, so needles, bands and text overflow can be checked.
  - **Advanced:** everything else from `editableProps`, plus **Edit as JSON** for power users.
  - Position and size steppers move to Advanced; they stay as the precise, accessible alternative to dragging.
- **Status line:** "12 values · ~1.5 updates/s each on ELM327" with a warning colour past a threshold.
- **Driving lock:** above ~5 km/h the editor goes read-only with a banner (setting to override for passengers).

### 4.3 Pickers

- **Widget gallery** (bottom sheet): search; categories (Gauges / Readouts / Graphs / Warnings / Motion / Media / Time); each card is a **live miniature** of the real widget with demo data and its size options; a one-line description. "Suggested for your car" row.
- **Data-first add:** "What do you want to see?" opens the PID list; picking one proposes the best widget for its `kind` (temperature → Arc or Bar, RPM → Circular with redline, boolean-like → Warning) and places it.
- **PID picker:** grouped by system (Engine · Fuel & air · Temperatures · Electrical · Speed & motion · Emissions · Sensors), with Recent at the top. When connected, rows show live values. No automatic keyboard: the search field focuses only when tapped.

### 4.4 Screens and sharing

- **Spec addition:** an optional `layouts: { "portrait": { gridColumns, gridRows, cells } }` variant (a `schema: 2`, read alongside 1). When it is missing, it is derived once in reading order the first time the user previews or opens portrait, then saved and owned by the user. The renderer never re-packs at runtime.
- **Remote editing:** the same editor runs in the browser build and against the service API (pair with a code, edit on the laptop or phone, live result on the car screen). This is the "design on the big screen" path every pro tool uses, and we can do it without a separate app.
- **Sharing:** a thumbnail embedded in the export, a QR/share-sheet export, and an import report ("2 values your car doesn't report: shown as placeholders").

## 5. Decisions (taken 2026-10-30: the recommendations)

1. **Grid density.** Options:
   - (a) Keep 12 columns but zoom/pan the canvas so cells meet the touch minimum.
   - (b) Coarse / Normal / Fine density per dashboard (e.g. 4×3 / 6×4 / 12×8), with spans rescaled on change.
   - (c) Density chosen from the screen's physical size.

   **Recommendation: (b), with the default picked from screen size (c).** Fine density is meant for desktop and the browser editor.
2. **Free-form placement:** stay on the grid (recommended; every pro-grade consumer editor that works on touch does) or add a free x/y "expert" mode like RealDash's (its main source of complaints).
3. **Driving lock:** on by default with a passenger override (recommended), or off.
4. **Built-ins:** "Customize" silently forks a copy (recommended), or keep Copy-then-Edit.

## 6. Plan

Ordered so each phase is shippable and the next one builds on it. Effort: S < 1 day, M 1-3 days, L ~1 week, XL more.

**Phase 0 — make the pieces trustworthy (M-L, desktop)** — done 2026-10-30 except the editor's use of the size data (moved to Phase 1 with the size chips)
- The widget fixes from the asset audit: a missing-data state shown as `--`, BarGauge geometry, measured text fit, minimum readable spans.
- Catalog: `minSpan` / `maxSpan` / `sizes` per widget (drives the size chips and stops unreadable sizes).
- Editor model: an incremental update per cell (keyed `ListModel` or per-delegate props) instead of rebuilding the Repeater; then an **undo/redo stack** of spec snapshots (50 steps).
- Tests: `tst_dashboardeditor` (QtQuickTest or the existing QML fixture style) for undo, swap/push placement rules and size presets; extend `src/util/dashboardgrid.h` with the push/swap solver in C++, unit-tested.

**Phase 1 — touch interaction core (L, desktop then Galaxy S22 and the 7" Pi screen)** — built and tested on the desktop 2026-10-30; the device checks are in `TODO/dashboards-roadmap.md`. The density setting is per dashboard (Coarse 4×3 / Normal 6×4 / Fine 12×8, turned for portrait) in the editor's Grid menu, not a global setting
- Full-screen editor, the new top bar, Done/Close semantics.
- Select-then-drag, drag threshold, lift, ghost, swap/push, red-with-reason.
- Floating toolbar with size chips, duplicate, delete + undo snackbar, tap-tap swap/move.
- Density setting and the new default; dot grid; tap-empty "Add here".
- Settings sheet with zoom-to-fit canvas.

**Phase 2 — creation and settings (L)** — built and tested on the desktop 2026-10-30: ten templates (`frontend/dashboards/templates/`, slots filled by `dashboardManager.fromTemplate`), Customize on built-ins (an unsaved copy), the widget gallery with live miniatures / categories / search / data-first, the PID picker by system with recents and live values, and the sheet in Data / Look / Range & alerts / Advanced / JSON with warning + critical limits (`warnAbove` / `warnBelow` / `critAbove` / `critBelow`, `alertFlash`) on every value gauge and a Test value slider
- Template gallery with live previews and PID auto-fill from the car's supported list; "Customize" on built-ins.
- Widget gallery with live miniatures, categories and descriptions; data-first add.
- PID picker by system, with recents, live values and no auto-keyboard.
- Settings reorganised (Data / Look / Range & alerts / Advanced / JSON); warning + critical bands in the spec; per-widget Test value slider.

**Phase 3 — every screen (L)** — built and tested on the desktop 2026-10-30: schema 2 `layouts.portrait` (positions only, one per cell), `derivePortrait` / `orientedSpec`, the renderer on upright screens, the editor's Portrait switch (derive once, then the user's), portrait layouts in every preset, the polling-cost line, and the driving lock with a passenger override
- The portrait variant in the spec; preview switcher; derive-once; presets get portrait layouts.
- Polling-cost status line.
- Driving lock.

**Phase 4 — ecosystem (L-XL)** — built and tested on the desktop 2026-10-30: the browser editor at `GET /dashboards` with a live preview on the car (`previewDashboard`) and the chooser's **In a browser** sheet (address, QR, pairing code); thumbnails on save, the import report; Style menu (copy / paste, use on every widget of a type); the critical-alert banner and `showWhen` auto-show; widget presets ("My widgets", tier 1 of `TODO/user-primitives.md`). Not done: composites (tier 1 part 2, still in that TODO), an OS share sheet, a QR of the dashboard itself (a busy dashboard is more than a QR holds; the QR is of the editor's address), and the network side of remote editing stays behind `--service-bind`
- Remote editing over the service API / browser editor with a live result on the car.
- Share thumbnails, QR, import report; copy/paste style and "apply to all".
- Dashboard alert overlay and "switch to page when …" rules; then `TODO/user-primitives.md` tier 1 (data-only widget presets).

**How we'll know it's right** (run on the S22 and the 7" Pi touchscreen, a new user given each task cold):
- Build a 4-gauge dashboard from New in **< 90 s** without help.
- Swap two gauges and make one bigger in **< 15 s**.
- **Zero accidental moves** while scrolling and selecting during a 5-minute session.
- Every on-canvas control is **≥ 9 mm** physically on the 7" screen.
- No widget shows clipped text or a fake 0 at any size the editor allows.

The on-device checks in `TODO/dashboards-roadmap.md` fold into Phase 1 and 3 testing.
