# Authoring OBD Gauges & Dashboards

This is the authoritative guide for adding new gauge widgets and dashboards
to OCTAVE. It is written to be picked up cold by a future session (human or
AI) without any additional context. If anything here conflicts with the
actual code, **the code wins** — update this file.

---

## 1. Mental model

An OBD value in OCTAVE flows like this:

```
OBDManager ─ parameterSampled ─▶ paramStore (C++) ─▶ OBDParameterModel.qml ─ liveValue(id) ─▶ your gauge
(polls PIDs)  (every value)      (one key per id,     (singleton: metadata,
berryIMU ─ 8 signals ─────────▶   33 ms fold)          gate, demo mode)
```

`OBDManager` is the C++ manager in `src/managers/obdmanager.cpp`, registered
in `src/main.cpp` as the `obdManager` context property.

You do **not** wire signals yourself. `main.cpp` feeds every decoded value
(and the IMU readings) into the C++ `paramStore`, keyed by a short `paramId`
(e.g. `"RPM"`, `"SPEED"`, `"COOLANT_TEMP"`). Any QML that binds
`App.OBDParameterModel.liveValue(paramId)` gets live, animated updates for
free: it reads `paramStore[paramId]` (or the dashboard editor's demo sweep),
re-evaluates only when that id changes, and is `undefined` until the first
reading (show "--", not 0) and at shutdown.

Every gauge primitive takes a `paramId` and handles the rest — title, unit,
min, max, and current value all come from the model. You can override any
of them per-gauge if needed.

**Units.** Gauges show the units the user picked: temperatures in °C or °F
(`gaugeTempUnit`), SPEED in MPH or km/h (`gaugeSpeedUnit`), ALTITUDE in m or
ft (`sensorAltitudeUnit`); the defaults are each parameter's own unit.
`ParameterCatalog` converts (`displayParameters`, `toDisplay`), and
`OBDParameterModel` serves the result: `liveValue(id)`, `getParamInfo(id)`,
`displayParameters` and `vehicleParameters` are in display units, while
`allParameters` and `nativeUnit(id)` are each parameter's own (what the store,
the black box and Trip History hold). A unit change re-evaluates every
reader at once. A primitive that looks up its PID's unit / min / max reads
`displayParameters`; `toDisplay(id, v)` / `fromDisplay(id, v)` convert one
value, and `rawValue(id)` is the native-unit read for code that needs it
whatever the gauges show (the 3D Jeep's wheel speed wants MPH).

---

## 2. Quick start: one gauge, ten seconds

Create any QML file and drop this in:

```qml
import QtQuick 2.15
import "gauges" as Gauges     // adjust the relative path if you're in a subdir
import "." as App

Item {
    width: 400; height: 400

    Gauges.CircularGauge {
        anchors.centerIn: parent
        width: 320; height: 320
        paramId: "RPM"
        showNeedle: true
        redlineStart: 6500
    }
}
```

That's it. The gauge is live, animated, and themed.

---

## 3. Primitives (frontend/gauges/)

The PID-bound primitives share the same binding API (the self-binding widgets
in §3.8–§3.11 are the exception — they read their data source directly and
take no `paramId`):

| Property       | Type    | Default                     | Notes                                                          |
| -------------- | ------- | --------------------------- | -------------------------------------------------------------- |
| `paramId`      | string  | `""`                        | Any ID from §6. Drives auto-population of title/unit/min/max. |
| `title`        | string  | from paramId                | Override display label.                                        |
| `unit`         | string  | from paramId                | Override unit suffix.                                          |
| `min`, `max`   | real    | from paramId                | Override range used for normalization.                         |
| `value`        | real    | from paramId (live)         | Override manually for preview/testing.                         |
| `decimals`     | int     | `0`                         | Digits after the decimal point.                                |
| `labelColor`   | color   | `App.Style.obdLabelColor`   | Override label tint.                                           |
| `valueColor`   | color   | `App.Style.obdValueColor`   | Override value tint.                                           |

### 3.1 `CircularGauge`

Arc-sweep dial with optional ticks, needle, redline, and center readout.
Flagship primitive for RPM/speed/temp.

| Property              | Type  | Default | Notes                                                                |
| --------------------- | ----- | ------- | -------------------------------------------------------------------- |
| `startAngle`          | real  | `135`   | `PathAngleArc` degrees (0 = 3 o'clock, CW).                          |
| `sweepAngle`          | real  | `270`   | Total arc span. Use `180` for a half-dial.                           |
| `showNeedle`          | bool  | `false` | Draws a rotating needle and center hub.                              |
| `showTicks`           | bool  | `true`  | Major + minor tick marks along the arc.                              |
| `majorTickCount`      | int   | `9`     | Number of major ticks (inclusive of endpoints).                      |
| `minorTicksPerMajor`  | int   | `5`     | Tick steps per major segment; `0` or `1` draws major ticks only.      |
| `redlineStart`        | real  | `NaN`   | If set and `< max`, draws a red arc from this value up, and flips the filled arc to red once `value >= redlineStart`. |
| `showCenterReadout`   | bool  | `true`  | Title + big number + unit in the middle.                             |
| `trackColor`, `fillColor`, `redlineColor`, `needleColor` | color | theme | Manual overrides. |

**Sizing rule:** the gauge fills its parent square. Use `width: Math.min(parent.width, parent.height)` and `height: width` in a wrapper `Item` to keep it round.

### 3.2 `BarGauge`

Horizontal or vertical filled bar. Good for temperatures, fuel, throttle.

| Property       | Type   | Default      | Notes                                                   |
| -------------- | ------ | ------------ | ------------------------------------------------------- |
| `orientation`  | string | `"horizontal"` | or `"vertical"`                                       |
| `warnAbove`    | real   | `NaN`        | If set, bar turns `warnColor` once `value >= warnAbove`.|
| `showLabel`    | bool   | `true`       | Header/title.                                           |
| `showValue`    | bool   | `true`       | Current value text.                                     |
| `trackColor`, `fillColor`, `warnColor` | color | theme | Manual overrides.      |

### 3.3 `LinearGauge`

Horizontal scale with tick marks and a sliding indicator. Designed for
**bidirectional ranges** that straddle zero (fuel trim, timing advance,
EGR error). When `min < 0 && max > 0`, fill grows outward from the zero
mark in both directions.

| Property             | Type | Default | Notes                                                |
| -------------------- | ---- | ------- | ---------------------------------------------------- |
| `majorTickCount`     | int  | `5`     | Inclusive of endpoints.                              |
| `minorTicksPerMajor` | int  | `4`     |                                                      |
| `showTicks`          | bool | `true`  |                                                      |
| `centerOriginAtZero` | bool | auto    | Defaults true when `min < 0 && max > 0`.             |

### 3.4 `DigitalReadout`

Big number, no chrome. Use as the dominant element on a sport dashboard.

| Property      | Type | Default           | Notes                                                  |
| ------------- | ---- | ----------------- | ------------------------------------------------------ |
| `padDigits`   | int  | `0`               | Pad integer part with leading zeros to this width.     |
| `valueScale`  | real | `3.5`             | Multiplier on `App.Spacing.overallText` for the digit. |
| `showTitle`   | bool | `true`            |                                                        |
| `showUnit`    | bool | `true`            |                                                        |
| `alignment`   | int  | `Qt.AlignHCenter` | or `Qt.AlignLeft` / `Qt.AlignRight`                    |

### 3.5 `ArcGauge`

Half-dial ("n" shape — top semicircle with readout below the chord). Compact, wide-aspect alternative to `CircularGauge` for speedometer-style layouts. Same arc/tick/needle/redline engine as `CircularGauge`, but the default angles give a 180° top arc and the readout is positioned beneath it rather than in the center.

| Property              | Type  | Default | Notes                                                                |
| --------------------- | ----- | ------- | -------------------------------------------------------------------- |
| `startAngle`          | real  | `180`   | PathAngleArc degrees (0 = 3 o'clock, CW).                            |
| `sweepAngle`          | real  | `180`   | Total arc span. `180` CW from `startAngle=180` = 9→12→3 (top arc).   |
| `showNeedle`          | bool  | `false` | Rotating needle + center hub, same as CircularGauge.                 |
| `showTicks`           | bool  | `true`  |                                                                      |
| `majorTickCount`      | int   | `5`     |                                                                      |
| `minorTicksPerMajor`  | int   | `4`     |                                                                      |
| `redlineStart`        | real  | `NaN`   | Same semantics as CircularGauge.                                     |
| `showReadout`         | bool  | `true`  | Title + big number + unit stacked below the chord.                   |
| `trackColor`, `fillColor`, `redlineColor`, `needleColor` | color | theme | Manual overrides.  |

**Sizing rule:** prefers a wide-aspect container (e.g. 2:1 width:height). The arc radius is capped by the smaller of `width/2` and `height * 0.6`, so it degrades gracefully in any aspect. Wrap in an `Item` with `Layout.fillWidth/Height` as usual.

### 3.6 `SparklineGauge`

Rolling history line showing recent values. Use when motion matters more than absolute value (boost, load, fuel trim, timing drift). Timer-sampled to keep the x-axis scale stable regardless of OBD poll rate.

| Property             | Type   | Default                                  | Notes                                                          |
| -------------------- | ------ | ---------------------------------------- | -------------------------------------------------------------- |
| `maxSamples`         | int    | `60`                                     | Number of points held in the rolling buffer.                   |
| `sampleIntervalMs`   | int    | `500`                                    | Default: 60 × 500ms = 30s window.                              |
| `fillBelow`          | bool   | `true`                                   | Area fill under the line.                                      |
| `showHeader`         | bool   | `true`                                   | Title + current value strip above the plot.                    |
| `autoScale`          | bool   | `false`                                  | If true, y-axis rescales to window's min/max instead of PID min/max. Use for small-swing values (fuel trim, timing) where full PID range dwarfs real motion. |
| `lineColor`, `fillColor`, `labelColor`, `valueColor`, `backgroundColor` | color | theme | Manual overrides. `fillColor` defaults to `lineColor` at 22 % alpha, so overriding `lineColor` (the editor's Color row) recolors line and area together. |

Sampling only runs while the item is `visible`. Samples are right-aligned — the newest value sits flush with the right edge and older values scroll left as new ones come in.

### 3.7 `WarningLight`

Binary tell-tale indicator bound to a threshold condition. Small (ideally square), composable — drop several into a row for an idiot-light strip, or overlay one atop a larger gauge as a critical alert.

| Property            | Type   | Default                                   | Notes                                                              |
| ------------------- | ------ | ----------------------------------------- | ------------------------------------------------------------------ |
| `triggerAbove`      | real   | `NaN`                                     | Light turns on when `value >= triggerAbove`.                       |
| `triggerBelow`      | real   | `NaN`                                     | Light turns on when `value <= triggerBelow`. Both may be set.      |
| `label`             | string | `""`                                      | Short text shown inside the light (e.g. `"TEMP"`, `"!"`, `"FUEL"`).|
| `activeColor`       | color  | theme danger (`Style.statusDanger`, red) | Bright color when lit.                                             |
| `inactiveColor`     | color  | dark tint of `obdBoxBackground`           | Dim color when off.                                                |
| `activeLabelColor`  | color  | `"white"`                                 | Label color when lit.                                              |
| `labelColor`        | color  | `App.Style.obdLabelColor`                 | Label color when off.                                              |
| `pulse`             | bool   | `false`                                   | Optional 1Hz opacity pulse while lit — reserve for critical alerts.|

Keep pulse off by default. Blinking is visually loud and in a car infotainment context should be used sparingly (overtemp, overrev, low oil pressure — not fuel trim drift).

### 3.8 `GForceGauge` *(self-binding)*

2D G-force dot: concentric rings + crosshair, a dot driven by lateral (x) and
longitudinal (y) acceleration, optional fading trail and total-G readout.
Reads `LATERAL_G` / `LONGITUDINAL_G` from `OBDParameterModel` (fed by
`berryIMU`) — **no `paramId`**, `octaveSupportedKinds: []`.

| Property    | Type | Default | Notes                              |
| ----------- | ---- | ------- | ---------------------------------- |
| `maxG`      | real | `1.5`   | g at the outer ring.               |
| `showTrail` | bool | `true`  | 2-second fading dot trail.         |
| `showValue` | bool | `true`  | Total-G readout, bottom center.    |

### 3.9 `CompassGauge` *(self-binding)*

Rotating compass card (ticks + cardinal letters, letters counter-rotated to
stay upright) under a fixed lubber line, with optional center degrees + the
cardinal abbreviation. Reads `HEADING` from `OBDParameterModel` (fed by
`berryIMU`) — **no `paramId`**, `octaveSupportedKinds: []`. This is the
CompassGauge the dashboards roadmap deferred until heading data existed.

| Property      | Type | Default | Notes                       |
| ------------- | ---- | ------- | --------------------------- |
| `showDegrees` | bool | `true`  | Center "247° / WSW" readout.|

### 3.10 `NowPlayingWidget` *(self-binding, lives in `frontend/dashboards/widgets/`)*

Album art + title + artist + progress bar for the active media source. Follows
the app-wide source rule (the phone while `phoneMirrorManager.mediaActive`,
else local media) and re-resolves on source / track changes. Display-only.

| Property       | Type | Default | Notes                  |
| -------------- | ---- | ------- | ---------------------- |
| `showArt`      | bool | `true`  | Album art square.      |
| `showArtist`   | bool | `true`  | Artist line.           |
| `showProgress` | bool | `true`  | Position/duration bar. |

### 3.11 `MediaControlsWidget` *(self-binding, lives in `frontend/dashboards/widgets/`)*

Previous / play-pause / next transport buttons driving the active media source
(same source rule as `NowPlayingWidget`). Interactive in rendered dashboards;
inside the editor canvas the selection/drag layer sits on top, so taps select
the cell instead of skipping tracks. No editable props.

---

## 4. Styling & theme tokens

All colors come from `App.Style` (`frontend/Style.qml`). The ones you care
about for OBD surfaces:

| Token                       | Purpose                                                |
| --------------------------- | ------------------------------------------------------ |
| `App.Style.obdBoxBackground`| Page/card surface behind gauges.                       |
| `App.Style.obdBarColor`     | Primary fill / accent for arcs, bars, needles.         |
| `App.Style.obdLabelColor`   | Labels (title, unit, tick marks).                      |
| `App.Style.obdValueColor`   | The big numeric value.                                 |
| `App.Style.accent`          | Global accent (fallback).                              |
| `App.Style.primaryTextColor`/`secondaryTextColor` | Text outside the OBD region. |
| `App.Style.fontFamily`      | Current active font.                                   |

Spacing tokens come from `App.Spacing`:

| Call                              | Purpose                                                         |
| --------------------------------- | --------------------------------------------------------------- |
| `App.Spacing.dp(px)`              | Density-independent pixel scaling. Use for hardcoded pixel values. |
| `App.Spacing.dpMin(px, floor)`    | Same but with a minimum floor (for radii).                      |
| `App.Spacing.overallText`         | Base font size for the current screen.                          |
| `App.Spacing.overallSpacing`      | Standard gutter.                                                |

**Never hardcode colors or raw pixels.** Use the tokens. New themes will
automatically look correct.

---

## 5. Recipe: add a new dashboard

As of Phase 2, dashboards are **JSON specs**, not hand-written QML. `DashboardRenderer.qml` reads a spec and instantiates widgets into a grid. `OBDMenu.qml` hosts the renderer and a modal chooser popup; the active dashboard id persists under the `activeDashboard` setting. The default choice (`"grid"`) shows the built-in Parameter Cards view in `OBDMenu.qml` itself: one card for every PID the connected vehicle reported in this session's supported-PID scan (empty, with an explanatory message, until a vehicle answers). A horizontal swipe anywhere on the page cycles Parameter Cards and every dashboard (wrapping); page dots under the header show the position. Nothing renders until `obdManager.connected` is true (`obdPage.obdConnected`): while disconnected the renderer is hidden with a `null` spec (widgets unloaded), swipe and the dots are off, and the page shows the "No vehicle connected" prompt with the connect button in place of the active dashboard. The chooser and editor still work offline.

### 5.1 Dashboard JSON schema (v1)

```json
{
  "id": "my-board",
  "label": "My Board",
  "schema": 1,
  "gridColumns": 12,
  "gridRows": 8,
  "margins": 12,
  "cells": [
    {
      "type":    "CircularGauge",
      "paramId": "RPM",
      "col":     6, "row":     0,
      "colSpan": 6, "rowSpan": 7,
      "props":   { "showNeedle": true, "redlineStart": 6500 }
    }
  ]
}
```

Required fields: `id`, `label`, `schema`, `cells`. Optional: `gridColumns` (default 12), `gridRows` (default 6), `margins` (default 20 dp, outer padding), `spacing` (default 16 dp, gap between cells).

**Per-widget overrides are just `props` keys.** The editor's Label & Range and
Color sections write ordinary props — a cell that renames, rescales and
recolors its gauge looks like this:

```json
{
  "type": "CircularGauge", "paramId": "RPM",
  "col": 0, "row": 0, "colSpan": 6, "rowSpan": 5,
  "props": {
    "title": "Revs", "unit": "rpm", "min": 0, "max": 6000,
    "fillColor": "#2ECC71", "needleColor": "#2ECC71",
    "showNeedle": true, "redlineStart": 5500
  }
}
```

Omit a key to follow the bound PID's metadata (`title`, `unit`, `min`, `max`)
or the live theme (colors). Which keys a widget accepts is declared in its
`WidgetCatalog` entry (`labelProps`, `rangeProps`, `colorProps`,
`editableProps` — see §7).

Per cell: `type` matches a widget registered in `WidgetCatalog` — the gauge primitives in `frontend/gauges/` (`CircularGauge`, `ArcGauge`, `BarGauge`, `LinearGauge`, `DigitalReadout`, `SparklineGauge`, `WarningLight`, and the self-binding `GForceGauge` and `CompassGauge`, which ignore `paramId`) plus the media widgets in `frontend/dashboards/widgets/` (`NowPlayingWidget`, `MediaControlsWidget`, no `paramId`). `paramId` must be one of §6 — an unknown id is logged by `DashboardRenderer` and the widget is left unbound; an unknown `type` is logged and the cell is skipped. `props` keys must be exposed `Q_PROPERTY` on the widget — unknown keys are silently skipped. `"NaN"` (string) decodes to JS `NaN` since JSON can't encode it natively.

### 5.2 Add a built-in preset

1. Write `frontend/dashboards/presets/my-board.json` using the schema above.
2. Restart the app. `DashboardManager` enumerates `presets/` on startup and the chooser picks it up. No code changes.

### 5.3 Add a user dashboard

**Primary path — the in-app editor (Phase 3, shipped).** In the dashboard
chooser (square Dashboards button at the top-right of `OBDMenu.qml`), tap
**+ New** — or **Edit** on a user dashboard, or **Copy** to fork a built-in.
The editor is hybrid tap-and-drag: tap an empty grid cell to place a widget
from the palette, drag a placed widget to move it (snaps to the grid, invalid
drops bounce back), drag the selected widget's corner grip to resize it
(snaps to the grid; a red preview means overlap / out of bounds and the size
is refused), tap a widget to select it and edit it in the side panel. The
panel runs, top to bottom: **PID** → **Position & Size** steppers →
**Options** (the widget's `editableProps`) → **Label & Range** (title / unit /
min / max overrides; empty = follow the PID; warns when min ≥ max) → **Color**
(swatches; "T" = theme/unset; one pick writes every `colorProps` key) →
**Remove**. Saving writes the JSON below through
`dashboardManager.saveDashboard()` and the chooser refreshes immediately.
Editor sources live in `frontend/dashboards/editor/` (`DashboardEditor`,
`EditorCanvas`, `PalettePopup`, `PIDPicker`, `PropertiesPanel`); the widget
set, palette metadata, and curated editable props all come from the
`WidgetCatalog` singleton (`frontend/dashboards/WidgetCatalog.qml`).

**PID picker and the vehicle gate.** With no vehicle scanned this session the
picker lists every PID. Once a connected vehicle has answered the
supported-PID scan, its PIDs sort first in each group and the rest are greyed
("Not supported by this vehicle") but stay assignable — a dashboard may be
built for another car or shared. The panel warns when the bound PID is
unsupported, and at render time `DashboardRenderer` swaps such a cell for a
"Not supported by this vehicle" placeholder (see §5.5).

**Sharing path.** In the chooser, **Share** on any JSON dashboard card offers
*Save to Downloads* (`<Downloads>/OCTAVE-dashboards/<id>.json`) or *Copy as
text*; **Import** offers *From file…* or *Paste from clipboard*. Imports are
validated and always become a new user dashboard (§5.4).

**Manual path.** Drop a JSON file with the same schema under the OS config dir:

- Linux: `~/.config/OCTAVE/dashboards/my-board.json`
- macOS: `~/Library/Application Support/OCTAVE/dashboards/my-board.json`
- Windows: `%APPDATA%/OCTAVE/dashboards/my-board.json`

`DashboardManager` watches the user dashboards directory, so a hand-dropped file
appears in the chooser within about a second (see §5.4 Hot reload); no restart
is needed. `dashboardManager.refresh()` forces an immediate rescan.

### 5.4 Registry rules

- Built-in preset ids must be unique. User ids colliding with a built-in are rejected at load time (logged).
- Built-ins are read-only: `deleteDashboard` on one hides it (ids listed in `<userDir>/.hidden-builtins`, id still reserved) and `restoreBuiltInDashboards()` brings them back; `hiddenBuiltInCount` drives the chooser's "Restore defaults" button. Parameter Cards (`grid`) can't be deleted. "Duplicate" via `dashboardManager.duplicateDashboard(sourceId, newLabel)` creates an editable user copy with an auto-generated unique id. New ids come from `dashboardManager.uniqueIdForLabel(label)` (editor, Duplicate and Import alike); it never returns a built-in id (hidden ones included) or `grid`, and `saveDashboard` refuses both.
- **Newer schema.** A spec whose `schema` is above `dashboardManager.supportedSchema` (`kSupportedSchema`, 1) renders no cells, demands no PIDs, and shows one centred "This dashboard needs a newer version of OCTAVE" message. Import refuses such files; this covers one dropped into the folder by hand.
- **One set of spec rules** (`DashboardManager::normalizeSpec`, applied by `saveDashboard`, `loadDashboard` and every import, so the editor, Duplicate, Import and the renderer all see the same thing):
  - Kept keys: `schema`, `id`, `label`, `gridColumns`, `gridRows`, `margins`, `spacing`, `cells` and per cell `type` / `paramId` / `col` / `row` / `colSpan` / `rowSpan` / `props`; anything else is dropped. `props` keeps scalars only (bool, number, string ≤ 200 chars; `"NaN"` survives).
  - Numbers must be JSON numbers (a string or bool gets the default). Grid size: not a positive number = 12 × 6, else capped at 48. `margins` / `spacing`: 0..200, absent or `null` = the renderer's 20 / 16. Caps: 256 cells (refused on save and import; a file on disk shows its first 256), label 80 chars (whitespace collapsed).
  - A cell off the grid keeps its size and moves back in (as a drag does); only a span wider than the grid shrinks.
  - Cells never overlap. In file order the earlier cell wins; a later one shrinks toward 1×1 (wider side first, as a newly placed widget does) and is dropped, logged, only if its corner is taken.
  - Unknown widget types and PIDs are **kept**, so a newer build's dashboard survives an older build's save: the renderer skips an unknown type (its grid area stays empty) and leaves an unknown PID unbound; the editor shows an unknown type inert ("needs a newer OCTAVE", Delete works) and doesn't bind an unknown PID.
  - Schema: missing = 1. A newer schema is refused by save and import, and `loadDashboard` passes such a file through untouched so the renderer can show its placeholder; the editor won't open it.
  - `saveDashboard` needs a label and a file-safe id (`[A-Za-z0-9_-]`, ≤ 64; new ids are slugs from `uniqueIdForLabel`). A file with no `cells` loads as an empty dashboard.
  - Grid placement (`src/util/dashboardgrid.{h,cpp}`) is the same code in the editor: `dashboardManager.canPlace(cells, cols, rows, col, row, colSpan, rowSpan, exceptIndex)` for drag / resize, `dashboardManager.fitAt(cells, cols, rows, col, row, colSpan, rowSpan)` for the span a new widget shrinks to. The rescued editor draft goes through `dashboardManager.normalizedSpec(spec)`.
  - Every shipped preset already satisfies these rules (`presetsAreAlreadyNormal` in `tests/cpp/tst_dashboardmanager.cpp`).
- **Validation in QML.** `DashboardRenderer` still checks every cell against `WidgetCatalog.isKnownType()` and `OBDParameterModel.hasParameter()` (the catalog is QML), `console.warn`s failures with the dashboard id and never crashes the view.
- **Import.** `importDashboard` / `importDashboardFromClipboard` /
  `importDashboardFromText` apply the rules above and also refuse a spec
  with a bad `schema` or no `cells` list, and input over 512 KB. The id is
  always regenerated from the label (`label` defaults to "Imported
  dashboard"), so an import never overwrites an existing dashboard.
  Failures set `dashboardManager.lastShareError`.
- **Hot reload.** `DashboardManager` watches the user dashboards directory (`QFileSystemWatcher`, 300 ms debounce, rescans only when the file-name/mtime/size signature changed). Drop a JSON in from a terminal or sync client and it appears in the chooser without restarting; delete one and it disappears. If the deleted file was the active dashboard, `OBDMenu` falls back to Parameter Cards.

### 5.5 Vehicle gate and polling demand

- **Range props are in the PID's own unit.** A spec's `min`, `max` (for
  widgets with `rangeProps`) and the thresholds a widget's `editableProps`
  mark `"paramUnit": true` (`redlineStart`, `warnAbove`, `triggerAbove`,
  `triggerBelow`) are written in the parameter's catalog unit (°C, MPH, m),
  whatever the user's display units. `WidgetCatalog.paramUnitProps(type)`
  lists them; `DashboardRenderer` and the editor canvas bind them through
  `OBDParameterModel.toDisplay`, and the properties panel shows and stores
  them converted, so `sport.json`'s `warnAbove: 105` on coolant means 221 °F
  for a °F user. A new threshold prop in parameter units needs the flag.
- **Gate.** `OBDParameterModel` mirrors `obdManager.vehicleSupportedParameters`
  / `vehicleScanComplete` — this session's scan only, cleared on disconnect —
  as `vehicleKnown`, `vehicleSupportedParameters`, `vehicleParameters` (the
  supported OBD params in `allParameters` order, display units) and `isSupported(id)`.
  BerryIMU ids (`sensorParameters`, `isSensorParameter(id)`) are never gated.
  When `vehicleKnown` is true, `DashboardRenderer` hides a cell bound to an
  unsupported PID behind a placeholder (`objectName: dashUnsupported_<i>`).
- **Polling is demand-driven.** The backend polls only what QML asks for via
  `obdManager.setParameterDemand(consumer, ids)`. A dashboard needs no extra
  wiring: `DashboardRenderer.paramIds` lists its distinct PIDs and
  `OBDMenu` registers them as the `"dashboard"` consumer while the page is
  active. Other consumers: `"cards"` (Parameter Cards), `"editor"`
  (`DashboardEditor` canvas), `"home"` (`HomeOBDView`), `"carMenu"` (RPM/SPEED
  for the 3D Jeep while `CarMenu` is visible), `"settings"`
  (`OBDSettingsPage`). Backend side: `wiki/obd-manager.html`.

### Legacy recipe (pure-QML dashboard)

The pre-Phase-2 path — create `frontend/dashboards/MyDashboard.qml` and register it in the `dashboardRegistry` array — is retired. All built-ins now ship as JSON presets. If you need something the JSON schema can't express, extend the renderer's `_widgetRegistry` with a new primitive under `gauges/` instead of a whole hand-written dashboard.

---

## 6. All 129 parameter IDs (120 OBD + 1 alias + 8 IMU)

Copy `id` verbatim into `paramId`. `min`/`max` are what the primitive will
default to; `Short` is the home-card label; `Decimals` is what plain-text
readouts use. **Source of truth:** `src/managers/parametercatalog.cpp` (the
C++ `ParameterCatalog`, served to QML as `parameterCatalog.parameters` and
unchanged through `OBDParameterModel.allParameters`). The tables below are
generated from it: after changing the catalog run
`python scripts/gen_gauge_param_docs.py` (`tst_parametercatalog` fails while
an id is missing here).

**Multi-value PIDs.** Newer PIDs (0x61 and up) pack several sensors into one
reply, so they do not get a signal each: `OBDManager` emits
`obdManager.obdParameterChanged(paramId, value)` and `parameterSampled`,
which puts it in `paramStore` like any other id. Gauges bind them by `paramId`
exactly the same. All are off by default; the vehicle scan enables the ones
the car reports. Decoders live in `ELM327Protocol::extendedPidTable()` in
`src/managers/elm327protocol.cpp`; `everyExtendedParameterIsWired` in
`tests/cpp/tst_elm327protocol.cpp` checks that every id has a settings
default and a catalog entry. Boost pressure is absolute (subtract
`BAROMETRIC_PRESSURE` for gauge boost).

**Sensor parameters** are fed into `paramStore` by the
`berryIMU` manager instead of `obdManager`, but are otherwise identical: any
gauge can bind them by `paramId`, the PID picker lists them, and the
editor's demo mode sweeps them. The vehicle gate never applies to them.

**Alias.** `IGNITION_TIMING` is `TIMING_ADVANCE` under its old name: it is
polled as PID 0x0E and counts as supported whenever timing advance does.

<!-- BEGIN generated by scripts/gen_gauge_param_docs.py -->

### Core (default-enabled) (18)

| `paramId` | Title | Short | Unit | Min | Max | Kind | Decimals |
| --- | --- | --- | --- | ---: | ---: | --- | ---: |
| `SPEED` | Speed | SPD | MPH | 0 | 160 | numeric | 0 |
| `RPM` | Engine RPM | RPM | RPM | 0 | 8000 | numeric | 0 |
| `COOLANT_TEMP` | Coolant Temp | CLT | °C | 0 | 120 | temperature | 1 |
| `OIL_TEMP` | Oil Temp | OIL | °C | 0 | 150 | temperature | 1 |
| `COMMANDED_EQUIV_RATIO` | Air-Fuel Ratio | AFR | :1 | 10 | 18 | numeric | 1 |
| `ENGINE_LOAD` | Engine Load | LOAD | % | 0 | 100 | percentage | 1 |
| `THROTTLE_POS` | Throttle | TPS | % | 0 | 100 | percentage | 1 |
| `FUEL_LEVEL` | Fuel Level | FUEL | % | 0 | 100 | percentage | 1 |
| `SHORT_FUEL_TRIM_1` | Short Fuel Trim 1 | STFT1 | % | -25 | 25 | bidirectional | 1 |
| `LONG_FUEL_TRIM_1` | Long Fuel Trim 1 | LTFT1 | % | -25 | 25 | bidirectional | 1 |
| `INTAKE_TEMP` | Intake Temp | IAT | °C | 0 | 80 | temperature | 1 |
| `INTAKE_PRESSURE` | Intake Pressure | MAP | kPa | 0 | 255 | pressure | 1 |
| `MAF` | Mass Air Flow | MAF | g/s | 0 | 100 | numeric | 1 |
| `TIMING_ADVANCE` | Timing Advance | TIM | ° | -35 | 35 | bidirectional | 1 |
| `CONTROL_MODULE_VOLTAGE` | System Voltage | VOLT | V | 10 | 15 | voltage | 1 |
| `O2_B1S1` | O2 Bank 1 Sensor 1 | O2 | V | 0 | 1 | voltage | 1 |
| `FUEL_PRESSURE` | Fuel Pressure | FP | kPa | 0 | 765 | pressure | 1 |
| `IGNITION_TIMING` | Ignition Timing (alias of `TIMING_ADVANCE`) | IGN | ° | -10 | 60 | bidirectional | 1 |

### More single-value OBD PIDs (66)

| `paramId` | Title | Short | Unit | Min | Max | Kind | Decimals |
| --- | --- | --- | --- | ---: | ---: | --- | ---: |
| `RUN_TIME` | Run Time |  | sec | 0 | 65535 | numeric | 0 |
| `DISTANCE_W_MIL` | Distance w/ MIL |  | km | 0 | 65535 | numeric | 0 |
| `FUEL_RAIL_PRESSURE_VAC` | Fuel Rail Pressure |  | kPa | 0 | 5177 | pressure | 1 |
| `FUEL_RAIL_PRESSURE_DIRECT` | Fuel Rail Direct |  | kPa | 0 | 655350 | pressure | 1 |
| `BAROMETRIC_PRESSURE` | Barometric |  | kPa | 0 | 255 | pressure | 1 |
| `AMBIANT_AIR_TEMP` | Ambient Air |  | °C | -40 | 215 | temperature | 1 |
| `RELATIVE_THROTTLE_POS` | Rel. Throttle |  | % | 0 | 100 | percentage | 1 |
| `THROTTLE_POS_B` | Throttle Pos B |  | % | 0 | 100 | percentage | 1 |
| `ACCELERATOR_POS_D` | Accel. Pedal |  | % | 0 | 100 | percentage | 1 |
| `CATALYST_TEMP_B1S1` | Cat Temp B1S1 |  | °C | 0 | 6513 | temperature | 1 |
| `CATALYST_TEMP_B1S2` | Cat Temp B1S2 |  | °C | 0 | 6513 | temperature | 1 |
| `EVAP_VAPOR_PRESSURE` | EVAP Pressure |  | Pa | -8192 | 8191 | bidirectional | 1 |
| `SHORT_FUEL_TRIM_2` | Short Fuel Trim 2 |  | % | -25 | 25 | bidirectional | 1 |
| `LONG_FUEL_TRIM_2` | Long Fuel Trim 2 |  | % | -25 | 25 | bidirectional | 1 |
| `O2_B1S2` | O2 Bank 1 Sensor 2 |  | V | 0 | 1 | voltage | 1 |
| `O2_B2S1` | O2 Bank 2 Sensor 1 |  | V | 0 | 1 | voltage | 1 |
| `O2_B2S2` | O2 Bank 2 Sensor 2 |  | V | 0 | 1 | voltage | 1 |
| `DISTANCE_SINCE_DTC_CLEAR` | Dist Since Clear |  | km | 0 | 65535 | numeric | 0 |
| `WARMUPS_SINCE_DTC_CLEAR` | Warmups Since Clear |  |  | 0 | 255 | numeric | 0 |
| `ABSOLUTE_LOAD` | Absolute Load |  | % | 0 | 25700 | numeric | 1 |
| `COMMANDED_EGR` | Commanded EGR |  | % | 0 | 100 | percentage | 1 |
| `EGR_ERROR` | EGR Error |  | % | -100 | 100 | bidirectional | 1 |
| `ETHANOL_PERCENT` | Ethanol % |  | % | 0 | 100 | percentage | 1 |
| `O2_B1S3` | O2 Bank 1 Sensor 3 |  | V | 0 | 1 | voltage | 1 |
| `O2_B1S4` | O2 Bank 1 Sensor 4 |  | V | 0 | 1 | voltage | 1 |
| `O2_B2S3` | O2 Bank 2 Sensor 3 |  | V | 0 | 1 | voltage | 1 |
| `O2_B2S4` | O2 Bank 2 Sensor 4 |  | V | 0 | 1 | voltage | 1 |
| `O2_S1_WR_VOLTAGE` | O2 S1 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S2_WR_VOLTAGE` | O2 S2 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S3_WR_VOLTAGE` | O2 S3 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S4_WR_VOLTAGE` | O2 S4 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S5_WR_VOLTAGE` | O2 S5 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S6_WR_VOLTAGE` | O2 S6 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S7_WR_VOLTAGE` | O2 S7 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S8_WR_VOLTAGE` | O2 S8 WR Voltage |  | V | 0 | 5 | voltage | 1 |
| `O2_S1_WR_CURRENT` | O2 S1 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S2_WR_CURRENT` | O2 S2 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S3_WR_CURRENT` | O2 S3 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S4_WR_CURRENT` | O2 S4 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S5_WR_CURRENT` | O2 S5 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S6_WR_CURRENT` | O2 S6 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S7_WR_CURRENT` | O2 S7 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `O2_S8_WR_CURRENT` | O2 S8 WR Current |  | mA | -128 | 128 | bidirectional | 1 |
| `CATALYST_TEMP_B2S1` | Cat Temp B2S1 |  | °C | 0 | 6513 | temperature | 1 |
| `CATALYST_TEMP_B2S2` | Cat Temp B2S2 |  | °C | 0 | 6513 | temperature | 1 |
| `THROTTLE_POS_C` | Throttle Pos C |  | % | 0 | 100 | percentage | 1 |
| `ACCELERATOR_POS_E` | Accel. Pos E |  | % | 0 | 100 | percentage | 1 |
| `ACCELERATOR_POS_F` | Accel. Pos F |  | % | 0 | 100 | percentage | 1 |
| `THROTTLE_ACTUATOR` | Throttle Actuator |  | % | 0 | 100 | percentage | 1 |
| `EVAPORATIVE_PURGE` | EVAP Purge |  | % | 0 | 100 | percentage | 1 |
| `FUEL_RAIL_PRESSURE_ABS` | Fuel Rail Abs |  | kPa | 0 | 655350 | pressure | 1 |
| `FUEL_INJECT_TIMING` | Fuel Inject Timing |  | ° | -210 | 302 | bidirectional | 1 |
| `FUEL_RATE` | Fuel Rate |  | L/h | 0 | 3212 | numeric | 1 |
| `RUN_TIME_MIL` | Run Time w/ MIL |  | min | 0 | 65535 | numeric | 0 |
| `TIME_SINCE_DTC_CLEARED` | Time Since Clear |  | min | 0 | 65535 | numeric | 0 |
| `MAX_MAF` | Max MAF |  | g/s | 0 | 2550 | numeric | 1 |
| `FUEL_TYPE` | Fuel Type |  |  | 0 | 23 | numeric | 1 |
| `EVAP_VAPOR_PRESSURE_ABS` | EVAP Pressure Abs |  | kPa | 0 | 327 | pressure | 1 |
| `EVAP_VAPOR_PRESSURE_ALT` | EVAP Pressure Alt |  | Pa | -32768 | 32767 | bidirectional | 1 |
| `SHORT_O2_TRIM_B1` | Short O2 Trim B1 |  | % | -100 | 100 | bidirectional | 1 |
| `LONG_O2_TRIM_B1` | Long O2 Trim B1 |  | % | -100 | 100 | bidirectional | 1 |
| `SHORT_O2_TRIM_B2` | Short O2 Trim B2 |  | % | -100 | 100 | bidirectional | 1 |
| `LONG_O2_TRIM_B2` | Long O2 Trim B2 |  | % | -100 | 100 | bidirectional | 1 |
| `RELATIVE_ACCEL_POS` | Rel. Accel Pos |  | % | 0 | 100 | percentage | 1 |
| `HYBRID_BATTERY_REMAINING` | Hybrid Battery |  | % | 0 | 100 | percentage | 1 |
| `ELM_VOLTAGE` | ELM Voltage |  | V | 0 | 65 | voltage | 1 |

### Multi-value OBD PIDs (0x61 and up, via obdParameterChanged) (37)

| `paramId` | Title | Short | Unit | Min | Max | Kind | Decimals |
| --- | --- | --- | --- | ---: | ---: | --- | ---: |
| `DEMAND_ENGINE_TORQUE` | Demand Torque |  | % | -125 | 130 | bidirectional | 1 |
| `ACTUAL_ENGINE_TORQUE` | Actual Torque |  | % | -125 | 130 | bidirectional | 1 |
| `ENGINE_FRICTION_TORQUE` | Friction Torque |  | % | -125 | 130 | bidirectional | 1 |
| `REFERENCE_TORQUE` | Reference Torque |  | Nm | 0 | 2000 | numeric | 1 |
| `BOOST_PRESSURE_A` | Boost Pressure (abs) |  | kPa | 0 | 300 | pressure | 1 |
| `BOOST_PRESSURE_A_COMMANDED` | Boost Commanded (abs) |  | kPa | 0 | 300 | pressure | 1 |
| `WASTEGATE_A` | Wastegate Position |  | % | 0 | 100 | percentage | 1 |
| `WASTEGATE_A_COMMANDED` | Wastegate Commanded |  | % | 0 | 100 | percentage | 1 |
| `MAF_SENSOR_A` | MAF Sensor A |  | g/s | 0 | 655 | numeric | 1 |
| `MAF_SENSOR_B` | MAF Sensor B |  | g/s | 0 | 655 | numeric | 1 |
| `INTAKE_TEMP_B1S1` | Intake Temp B1S1 |  | °C | -20 | 100 | temperature | 1 |
| `INTAKE_TEMP_B1S2` | Intake Temp B1S2 |  | °C | -20 | 100 | temperature | 1 |
| `INTAKE_TEMP_B2S1` | Intake Temp B2S1 |  | °C | -20 | 100 | temperature | 1 |
| `MANIFOLD_SURFACE_TEMP` | Manifold Surface Temp |  | °C | -40 | 215 | temperature | 1 |
| `COOLANT_TEMP_SENSOR_1` | Coolant Temp 1 |  | °C | 0 | 130 | temperature | 1 |
| `COOLANT_TEMP_SENSOR_2` | Coolant Temp 2 |  | °C | 0 | 130 | temperature | 1 |
| `THROTTLE_POS_G` | Throttle Pos G |  | % | 0 | 100 | percentage | 1 |
| `THROTTLE_ACTUATOR_A_COMMANDED` | Throttle A Commanded |  | % | 0 | 100 | percentage | 1 |
| `RELATIVE_THROTTLE_A` | Rel. Throttle A |  | % | 0 | 100 | percentage | 1 |
| `EGR_A_COMMANDED` | EGR A Commanded |  | % | 0 | 100 | percentage | 1 |
| `EGR_A_ACTUAL` | EGR A Actual |  | % | 0 | 100 | percentage | 1 |
| `EGR_A_ERROR` | EGR A Error |  | % | -100 | 100 | bidirectional | 1 |
| `FUEL_RAIL_PRESSURE_A` | Fuel Rail Pressure A |  | kPa | 0 | 250000 | pressure | 1 |
| `FUEL_RAIL_PRESSURE_A_COMMANDED` | Fuel Rail A Commanded |  | kPa | 0 | 250000 | pressure | 1 |
| `FUEL_RAIL_TEMP_A` | Fuel Rail Temp A |  | °C | -40 | 150 | temperature | 1 |
| `ENGINE_FUEL_RATE_GS` | Engine Fuel Rate |  | g/s | 0 | 100 | numeric | 1 |
| `VEHICLE_FUEL_RATE_GS` | Vehicle Fuel Rate |  | g/s | 0 | 100 | numeric | 1 |
| `CYLINDER_FUEL_RATE` | Cylinder Fuel Rate |  | mg/stroke | 0 | 200 | numeric | 1 |
| `EXHAUST_FLOW_RATE` | Exhaust Flow |  | kg/h | 0 | 2000 | numeric | 1 |
| `TRANSMISSION_GEAR_RATIO` | Gear Ratio |  | :1 | 0 | 6 | numeric | 1 |
| `RECOMMENDED_GEAR` | Recommended Gear |  |  | 0 | 10 | numeric | 1 |
| `ODOMETER` | Odometer |  | km | 0 | 1000000 | numeric | 1 |
| `ENGINE_RUN_TIME_TOTAL` | Total Engine Run Time |  | sec | 0 | 360000000 | numeric | 0 |
| `ENGINE_IDLE_TIME_TOTAL` | Total Idle Time |  | sec | 0 | 360000000 | numeric | 1 |
| `HYBRID_BATTERY_VOLTAGE` | Hybrid Battery Voltage |  | V | 0 | 900 | voltage | 1 |
| `HYBRID_BATTERY_CURRENT` | Hybrid Battery Current |  | A | -500 | 500 | bidirectional | 1 |
| `EV_BATTERY_HEALTH` | EV Battery Health |  | % | 0 | 100 | percentage | 1 |

### Sensor parameters (BerryIMU, not OBD) (8)

| `paramId` | Title | Short | Unit | Min | Max | Kind | Decimals |
| --- | --- | --- | --- | ---: | ---: | --- | ---: |
| `PITCH` | Pitch |  | ° | -90 | 90 | bidirectional | 1 |
| `ROLL` | Roll |  | ° | -180 | 180 | bidirectional | 1 |
| `HEADING` | Heading |  | ° | 0 | 360 | numeric | 1 |
| `ALTITUDE` | Altitude |  | m | 0 | 3000 | numeric | 1 |
| `ACCEL_MAG` | G Magnitude |  | g | 0 | 3 | numeric | 1 |
| `LATERAL_G` | Lateral G |  | g | -2 | 2 | bidirectional | 1 |
| `LONGITUDINAL_G` | Longitudinal G |  | g | -2 | 2 | bidirectional | 1 |
| `BARO_TEMP` | Cabin Baro Temp |  | °C | -20 | 60 | temperature | 1 |

<!-- END generated -->

---

## 7. Recipe: add a new gauge primitive

1. Create `frontend/gauges/MyGauge.qml`.
2. Implement the **shared binding API** (§3 table top rows). Copy the first
   ~35 lines of any existing primitive — the `paramId` → `_pInfo` lookup is
   boilerplate you should paste verbatim so behavior is identical.
3. Animate `_animNorm` with a `Behavior on _animNorm { NumberAnimation
   { duration: 100–150; easing.type: Easing.OutCubic } }`. Don't animate
   the raw `value` — that's upstream.
4. Use `Shape` + `ShapePath` + `PathAngleArc` for curves (GPU-accelerated).
   Use `Rectangle` + `transformOrigin` + `rotation` for small marks.
5. Declare `octaveSupportedKinds` (array of PID `kind` strings, or `["*"]`)
   near the top — the editor's PID picker filters on it.
6. Register the widget in `frontend/dashboards/WidgetCatalog.qml`. The
   renderer, palette, and properties panel all read from the catalog — a
   widget missing there won't render or be placeable. Each entry declares:
   - `type`, `url`, `displayName`, `glyph`, `defaultColSpan` / `defaultRowSpan`
   - `supportedKinds` — must mirror step 5 (`[]` = self-binding, no PID picker)
   - `labelProps` — `true` if the widget has `title` + `unit` string props
     that default to the PID's metadata (enables the Title / Unit overrides)
   - `rangeProps` — `true` if it has `min` + `max` real props (Min / Max)
   - `colorProps` — the color props the single Color swatch row writes, all
     set to the same value (e.g. `["fillColor", "needleColor"]`); `[]` hides
     the row. Swatches come from the catalog's `colorSwatches`.
   - `editableProps` — `{ key, label, kind, def[, min, max][, options] }` with
     `kind` ∈ `bool` (switch) · `int` / `real` (number field, clamped to
     `min`/`max`) · `string` (text field) · `enum` (segmented buttons over
     `options`). `def` is the gauge's real default (`null` for NaN-when-unset
     thresholds). Keep it in step with the gauge's own `octaveEditableProps`.
7. Add an entry to this document under §3.

### Needle/angle math cheat sheet

`PathAngleArc` measures in degrees from 3 o'clock, clockwise. A QML `Item`
with `rotation: 0` points straight up.

```
itemRotation = pathAngleArcAngle + 90
```

So an arc from `startAngle=135, sweepAngle=270`:
- min value → needle at PathAngleArc 135° → item rotation 225° (down-left)
- max value → needle at PathAngleArc 45° (after wrap) → item rotation 135° (down-right)

---

## 8. Pitfalls

- **`value` jitter at connect time:** the OBD manager reports `0` for any
  PID it hasn't polled yet. Either show a "disconnected" affordance or
  just let the gauge sit at min — our primitives do the latter.
- **Animated `x` + `Layout.fillWidth`:** don't mix them. Let the layout
  place the container; animate inside it.
- **Redlines that equal `max`:** don't set `redlineStart == max` — the
  redline arc has zero width and looks like a missing end. Use
  `redlineStart < max`.
- **Circular gauge in a non-square container:** the gauge self-centers but
  you still see wasted space. Wrap it in an `Item` and force a square
  inside, anchored center.
- **Never touch `obdManager.*` signals directly from a gauge.** Everything
  goes through `OBDParameterModel.liveValue(paramId)`. A new PID needs a
  decoder in `elm327protocol.cpp` and a row in `parametercatalog.cpp`
  (`tst_parametercatalog` fails if either is missing); its values reach
  the C++ `paramStore` through `obdManager.parameterSampled` with no
  further wiring.

---

## 9. Files at a glance

```
frontend/
├── gauges/
│   ├── CircularGauge.qml      arc + ticks + optional needle + readout
│   ├── BarGauge.qml           horizontal/vertical filled bar
│   ├── LinearGauge.qml        horizontal scale with bidirectional fill
│   ├── DigitalReadout.qml     big number + label + unit
│   ├── ArcGauge.qml           half-dial (top arc) with readout below
│   ├── SparklineGauge.qml     rolling history line (timer-sampled)
│   ├── WarningLight.qml       binary tell-tale indicator bound to a threshold
│   ├── GForceGauge.qml        2D G-dot + trail (self-binding: LATERAL_G/LONGITUDINAL_G)
│   └── CompassGauge.qml       rotating compass card (self-binding: HEADING)
├── dashboards/
│   ├── widgets/               self-binding non-gauge dashboard widgets
│   │   ├── NowPlayingWidget.qml    art + title + artist + progress (local/phone)
│   │   └── MediaControlsWidget.qml prev / play-pause / next transport
│   ├── DashboardRenderer.qml  JSON spec → grid of widget instances
│   ├── WidgetCatalog.qml      singleton: type → url + palette metadata +
│   │                          supportedKinds + label/range/color flags +
│   │                          editableProps + colorSwatches
│   ├── editor/                Phase 3 in-app editor ("create-a-park")
│   │   ├── DashboardEditor.qml   full-screen page; owns the working spec
│   │   ├── EditorCanvas.qml      tap-to-place targets, drag-to-move, corner resize grip
│   │   ├── PalettePopup.qml      widget palette (reads WidgetCatalog)
│   │   ├── PIDPicker.qml         searchable PID list, kind-filtered, vehicle-gated
│   │   └── PropertiesPanel.qml   PID / position & size / options / label & range / color
│   └── presets/               built-in dashboard JSON specs (ship with bundle)
│       ├── sport.json         big speed + RPM dial + vitals strip
│       ├── minimal.json       three round gauges: speed, RPM, fuel
│       ├── fullgrid.json      4×2 grid of compact round gauges
│       ├── performance.json   arc speed + sparklines + warning lights
│       └── tj-wrangler.json   "TJ Wrangler 4.0": 17-cell board from the Orange Pi rig
├── OBDParameterModel.qml      singleton aggregator: values, gate, demo mode;
│                              metadata from parameterCatalog (src/managers/parametercatalog.cpp)
└── OBDMenu.qml                hosts DashboardRenderer, Parameter Cards, swipe
                               between dashboards, and the chooser popup
                               (Import / Share)

src/managers/
├── dashboardmanager.h         enumerates presets + user dashboards, save/
└── dashboardmanager.cpp       delete/duplicate + export/import slots,
                               emits dashboardsChanged
```

User dashboards (outside the repo) live at:
- Linux: `~/.config/OCTAVE/dashboards/*.json`
- macOS: `~/Library/Application Support/OCTAVE/dashboards/*.json`
- Windows: `%APPDATA%/OCTAVE/dashboards/*.json`

When a dashboard/gauge change is merged, update `wiki/gauges-dashboards.html`
and re-run `python wiki/build_search_index.py`.
