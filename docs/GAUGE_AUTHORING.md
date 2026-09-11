# Authoring OBD Gauges & Dashboards

This is the authoritative guide for adding new gauge widgets and dashboards
to OCTAVE. It is written to be picked up cold by a future session (human or
AI) without any additional context. If anything here conflicts with the
actual code, **the code wins** — update this file.

---

## 1. Mental model

An OBD value in OCTAVE flows like this:

```
backend/obd_manager.py  ── Signal ──▶  OBDParameterModel.qml  ── paramValues[id] ──▶  your gauge
       (polls PID)        (emits)      (singleton aggregator)    (reactive QML binding)
```

You do **not** wire signals yourself. The `OBDParameterModel` singleton
already listens to every `*Changed` signal on `obdManager` and mirrors each
value into a plain object keyed by a short `paramId` (e.g. `"RPM"`,
`"SPEED"`, `"COOLANT_TEMP"`). Any QML that binds
`App.OBDParameterModel.paramValues[paramId]` gets live, animated updates
for free.

Every gauge primitive takes a `paramId` and handles the rest — title, unit,
min, max, and current value all come from the model. You can override any
of them per-gauge if needed.

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
| `minorTicksPerMajor`  | int   | `5`     | Minor ticks inserted between each pair of majors.                    |
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
| `lineColor`, `fillColor`, `labelColor`, `valueColor`, `backgroundColor` | color | theme | Manual overrides. |

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
the app-wide source rule (`settingsManager.mediaSource == "spotify"` AND
Spotify connected → Spotify, else local media) and re-resolves on source /
connection / track changes. Display-only.

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

As of Phase 2, dashboards are **JSON specs**, not hand-written QML. `DashboardRenderer.qml` reads a spec and instantiates widgets into a grid. `OBDMenu.qml` hosts the renderer and a modal chooser popup; the active dashboard id persists under the `activeDashboard` setting. The default choice (`"grid"`) shows the built-in parameter-card view in `OBDMenu.qml` itself.

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
drops bounce back), tap a widget to select it and edit its PID / position /
span / curated props in the side panel. Saving writes the JSON below through
`dashboardManager.saveDashboard()` and the chooser refreshes immediately.
Editor sources live in `frontend/dashboards/editor/` (`DashboardEditor`,
`EditorCanvas`, `PalettePopup`, `PIDPicker`, `PropertiesPanel`); the widget
set, palette metadata, and curated editable props all come from the
`WidgetCatalog` singleton (`frontend/dashboards/WidgetCatalog.qml`).

**Manual path.** Drop a JSON file with the same schema under the OS config dir:

- Linux: `~/.config/OCTAVE/dashboards/my-board.json`
- macOS: `~/Library/Application Support/OCTAVE/dashboards/my-board.json`
- Windows: `%APPDATA%/OCTAVE/dashboards/my-board.json`

Both backends watch the user dashboards directory, so a hand-dropped file
appears in the chooser within about a second (see §5.4 Hot reload); no restart
is needed. `dashboardManager.refresh()` forces an immediate rescan.

### 5.4 Registry rules

- Built-in preset ids must be unique. User ids colliding with a built-in are rejected at load time (logged).
- Built-ins are read-only (`deleteDashboard` returns false). "Duplicate" via `dashboardManager.duplicateDashboard(sourceId, newLabel)` creates an editable user copy with an auto-generated unique id.
- **Validation.** `DashboardRenderer` checks every cell against `WidgetCatalog.isKnownType()` and `OBDParameterModel.hasParameter()`; failures are `console.warn`ed with the dashboard id and never crash the view. The editor applies the same checks when loading a spec (`_sanitizeCells`) — unknown types are dropped, unknown PIDs cleared so the panel shows "Choose PID…".
- **Hot reload.** Both `DashboardManager` backends watch the user dashboards directory (`QFileSystemWatcher`, 300 ms debounce, rescans only when the file-name/mtime/size signature changed). Drop a JSON in from a terminal or sync client and it appears in the chooser without restarting; delete one and it disappears. If the deleted file was the active dashboard, `OBDMenu` falls back to Parameter Cards.

### Legacy recipe (pure-QML dashboard)

The pre-Phase-2 path — create `frontend/dashboards/MyDashboard.qml` and register it in the `dashboardRegistry` array — is retired. All built-ins now ship as JSON presets. If you need something the JSON schema can't express, extend the renderer's `_widgetRegistry` with a new primitive under `gauges/` instead of a whole hand-written dashboard.

---

## 6. All 92 parameter IDs (18 core + 66 extended OBD + 8 IMU)

Copy `id` verbatim into `paramId`. `min`/`max` listed are what the primitive
will default to. **Source of truth:** `frontend/OBDParameterModel.qml` —
update this table when that file changes.

### Core 18 (default-enabled)

| id                        | title             | unit    | min  | max  |
| ------------------------- | ----------------- | ------- | ---: | ---: |
| `SPEED`                   | Speed             | MPH     |    0 |  160 |
| `RPM`                     | Engine RPM        | RPM     |    0 | 8000 |
| `COOLANT_TEMP`            | Coolant Temp      | °C      |    0 |  120 |
| `OIL_TEMP`                | Oil Temp          | °C      |    0 |  150 |
| `COMMANDED_EQUIV_RATIO`   | Air-Fuel Ratio    | :1      |   10 |   18 |
| `ENGINE_LOAD`             | Engine Load       | %       |    0 |  100 |
| `THROTTLE_POS`            | Throttle          | %       |    0 |  100 |
| `FUEL_LEVEL`              | Fuel Level        | %       |    0 |  100 |
| `SHORT_FUEL_TRIM_1`       | Short Fuel Trim 1 | %       |  -25 |   25 |
| `LONG_FUEL_TRIM_1`        | Long Fuel Trim 1  | %       |  -25 |   25 |
| `INTAKE_TEMP`             | Intake Temp       | °C      |    0 |   80 |
| `INTAKE_PRESSURE`         | Intake Pressure   | kPa     |    0 |  255 |
| `MAF`                     | Mass Air Flow     | g/s     |    0 |  100 |
| `TIMING_ADVANCE`          | Timing Advance    | °       |  -35 |   35 |
| `CONTROL_MODULE_VOLTAGE`  | System Voltage    | V       |   10 |   15 |
| `O2_B1S1`                 | O2 Bank 1 Sensor 1| V       |    0 |  1.0 |
| `FUEL_PRESSURE`           | Fuel Pressure     | kPa     |    0 |  765 |
| `IGNITION_TIMING`         | Ignition Timing   | °       |  -10 |   60 |

### Extended (66 more)

Time & distance: `RUN_TIME`, `RUN_TIME_MIL`, `DISTANCE_W_MIL`,
`DISTANCE_SINCE_DTC_CLEAR`, `WARMUPS_SINCE_DTC_CLEAR`,
`TIME_SINCE_DTC_CLEARED`.

Pressures: `FUEL_RAIL_PRESSURE_VAC`, `FUEL_RAIL_PRESSURE_DIRECT`,
`FUEL_RAIL_PRESSURE_ABS`, `BAROMETRIC_PRESSURE`, `EVAP_VAPOR_PRESSURE`,
`EVAP_VAPOR_PRESSURE_ABS`, `EVAP_VAPOR_PRESSURE_ALT`.

Temps: `AMBIANT_AIR_TEMP`, `CATALYST_TEMP_B1S1`, `CATALYST_TEMP_B1S2`,
`CATALYST_TEMP_B2S1`, `CATALYST_TEMP_B2S2`.

Throttle / accel: `RELATIVE_THROTTLE_POS`, `THROTTLE_POS_B`,
`THROTTLE_POS_C`, `THROTTLE_ACTUATOR`, `ACCELERATOR_POS_D`,
`ACCELERATOR_POS_E`, `ACCELERATOR_POS_F`, `RELATIVE_ACCEL_POS`.

Fuel: `SHORT_FUEL_TRIM_2`, `LONG_FUEL_TRIM_2`, `FUEL_INJECT_TIMING`,
`FUEL_RATE`, `FUEL_TYPE`, `ETHANOL_PERCENT`, `EVAPORATIVE_PURGE`.

O2 sensors: `O2_B1S2`, `O2_B1S3`, `O2_B1S4`, `O2_B2S1`, `O2_B2S2`,
`O2_B2S3`, `O2_B2S4`, `O2_S1_WR_VOLTAGE` .. `O2_S8_WR_VOLTAGE`,
`O2_S1_WR_CURRENT` .. `O2_S8_WR_CURRENT`, `SHORT_O2_TRIM_B1`,
`SHORT_O2_TRIM_B2`, `LONG_O2_TRIM_B1`, `LONG_O2_TRIM_B2`.

EGR / emissions: `COMMANDED_EGR`, `EGR_ERROR`.

Misc: `ABSOLUTE_LOAD`, `MAX_MAF`, `HYBRID_BATTERY_REMAINING`,
`ELM_VOLTAGE`.

### Sensor parameters (BerryIMU, not OBD)

Fed into `OBDParameterModel.paramValues` by the `berryIMU` manager instead of
`obdManager`, but otherwise identical: any gauge can bind them by `paramId`,
the PID picker lists them, and the editor's demo mode sweeps them.

| `paramId`        | Title           | Unit | Min  | Max  | Kind          |
| ---------------- | --------------- | ---- | ---- | ---- | ------------- |
| `PITCH`          | Pitch           | °    |  -90 |   90 | bidirectional |
| `ROLL`           | Roll            | °    | -180 |  180 | bidirectional |
| `HEADING`        | Heading         | °    |    0 |  360 | numeric       |
| `ALTITUDE`       | Altitude        | m    |    0 | 3000 | numeric       |
| `ACCEL_MAG`      | G Magnitude     | g    |    0 |    3 | numeric       |
| `LATERAL_G`      | Lateral G       | g    |   -2 |    2 | bidirectional |
| `LONGITUDINAL_G` | Longitudinal G  | g    |   -2 |    2 | bidirectional |
| `BARO_TEMP`      | Cabin Baro Temp | °C   |  -20 |   60 | temperature   |

Full metadata (units, min, max) lives in `frontend/OBDParameterModel.qml`
— grep there when in doubt.

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
6. Register the widget in `frontend/dashboards/WidgetCatalog.qml`: type, url,
   display name, palette glyph, default span, `supportedKinds` (must mirror
   step 5), and the curated `editableProps` (with honest `def` values) the
   editor's properties panel exposes. The renderer, palette, and properties
   panel all read from the catalog — a widget missing there won't render or
   be placeable.
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
  goes through `OBDParameterModel.paramValues[paramId]`. If you add a new
  backend signal, register it in `OBDParameterModel.qml` too.

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
│   │   ├── NowPlayingWidget.qml    art + title + artist + progress (media/Spotify)
│   │   └── MediaControlsWidget.qml prev / play-pause / next transport
│   ├── DashboardRenderer.qml  JSON spec → grid of widget instances
│   ├── WidgetCatalog.qml      singleton: type → url + palette metadata +
│   │                          supportedKinds + curated editableProps
│   ├── editor/                Phase 3 in-app editor ("create-a-park")
│   │   ├── DashboardEditor.qml   full-screen page; owns the working spec
│   │   ├── EditorCanvas.qml      tap-to-place targets + drag-to-move grid
│   │   ├── PalettePopup.qml      widget palette (reads WidgetCatalog)
│   │   ├── PIDPicker.qml         searchable PID list, kind-filtered
│   │   └── PropertiesPanel.qml   PID / position / span / props inspector
│   └── presets/               built-in dashboard JSON specs (ship with bundle)
│       ├── sport.json         big speed + RPM dial + vitals strip
│       ├── minimal.json       three round gauges: speed, RPM, fuel
│       ├── fullgrid.json      4×2 grid of compact round gauges
│       ├── performance.json   arc speed + sparklines + warning lights
│       └── tj-wrangler.json   "TJ Wrangler 4.0": 17-cell board from the Orange Pi rig
├── OBDParameterModel.qml      singleton aggregator (SOURCE OF TRUTH for PIDs)
└── OBDMenu.qml                hosts DashboardRenderer, the chooser popup, and
                               a Primitives Gallery dev screen

src/managers/
├── dashboardmanager.h         enumerates presets + user dashboards, save/
└── dashboardmanager.cpp       delete/duplicate slots, emits dashboardsChanged
```

User dashboards (outside the repo) live at:
- Linux: `~/.config/OCTAVE/dashboards/*.json`
- macOS: `~/Library/Application Support/OCTAVE/dashboards/*.json`
- Windows: `%APPDATA%/OCTAVE/dashboards/*.json`

When a dashboard/gauge change is merged, update `wiki/gauges-dashboards.html`
and re-run `python wiki/build_search_index.py`.
