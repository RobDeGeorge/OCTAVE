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

`OBDManager` is the C++ manager in `src/managers/obdmanager*.cpp`, built in
`src/appcore.cpp` and exposed as the `obdManager` context property.

You do **not** wire signals yourself. `src/appcore.cpp` feeds every decoded value
(and the IMU readings) into the C++ `paramStore`, keyed by a short `paramId`
(e.g. `"RPM"`, `"SPEED"`, `"COOLANT_TEMP"`). Any QML that binds
`App.OBDParameterModel.liveValue(paramId)` gets live, animated updates for
free: it reads `paramStore[paramId]` (or the model's demo sweep while `simulationActive` is set),
re-evaluates only when that id changes, and is `undefined` until the first
reading and at shutdown.

**No reading is not zero.** Gauges bind
`value: App.OBDParameterModel.sample(paramId)`, which is `NaN` until there is a
finite reading, and expose `readonly property bool hasValue: isFinite(value)`.
Without a value: show `--`, draw no fill / needle / marker, and evaluate no
threshold (a missing fuel level must not light a low-fuel warning). Zero is a
valid reading. Every primitive in §3 follows this; a new one must too
(`tst_dashboardwidgets` `missingIsNotZero`).

Every PID-bound gauge primitive takes a `paramId` and handles the rest — title, unit,
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
in §3.8–§3.11 and §3.13 are the exception — they read their data source directly and
take no `paramId`):

| Property       | Type    | Default                     | Notes                                                          |
| -------------- | ------- | --------------------------- | -------------------------------------------------------------- |
| `paramId`      | string  | `""`                        | Any ID from §6, or one from the user's custom PID file (`docs/CUSTOM_PIDS.md`). Drives auto-population of title/unit/min/max. |
| `title`        | string  | from paramId                | Override display label.                                        |
| `unit`         | string  | from paramId                | Override unit suffix.                                          |
| `min`, `max`   | real    | from paramId                | Override range used for normalization. `DigitalReadout` draws no scale and uses them only for the alert hold (below). |
| `value`        | real    | from paramId (live)         | Override manually for preview/testing.                         |
| `decimals`     | int     | `0`                         | Digits after the decimal point. Not on `WarningLight`, which shows no number. |
| `labelColor`   | color   | `App.Style.obdLabelColor`   | Override label tint.                                           |
| `valueColor`   | color   | `App.Style.obdValueColor`   | Override value tint.                                           |

The seven value gauges (Circular, Arc, Bar, Segmented, Linear,
DigitalReadout, Sparkline) get all of this, and the alert properties below,
from one base component, `frontend/gauges/ValueGaugeBase.qml`: each is
`ValueGaugeBase { id: root ... }` and only draws. `WarningLight` binds the
same way without the alert properties.

**Alerts.** The value gauges share one warning / critical rule,
`frontend/gauges/AlertLevel.js`:

| Property | Type | Default | Notes |
| --- | --- | --- | --- |
| `warnAbove`, `warnBelow` | real | `NaN` | Warning at or past this value (inclusive). |
| `critAbove`, `critBelow` | real | `NaN` | Critical at or past this value; critical wins over warning. |
| `alertFlash` | bool | `false` | Blink the value (400 ms on, 400 ms off) while critical. |
| `warnColor`, `critColor` | color | `statusWarning` (amber), `statusDanger` (red) | |
| `alertHysteresis` | real | `NaN` | The hold, in the display unit: how far back inside a limit the value must come before the level drops. `NaN` = 1 % of `max - min` (0.01 V on a 0 to 1 V sensor; one step of the PID's readout only for a gauge with no usable range); `0` = no hold. In a dashboard spec the number is in the PID's own unit, like the limits, and is converted as a difference (5 °C holds 9 °F). |
| `alertLevel` | int (read-only) | | 0 normal, 1 warning, 2 critical; always 0 without a reading. |

A level is entered at its limit and left only once the value is back inside
that limit by the hold (`AlertLevel.levelHeld`): with `warnAbove: 100` on a
0–150 scale the gauge turns amber at 100 and back at 98.5. Without it a
reading sitting on a limit changed level on every sample, so the colour
flickered, the flash restarted and the dashboard's red banner (§5.1,
`alertBanner`) popped in and out; the banner uses the same hold. Changing a
limit starts from the plain rule, so an edit shows at once. `WarningLight`
holds its lamp the same way (`alertHysteresis`, default 1 % of the range).
The banner computes its hold exactly as the gauge does (same range, same
unit), so the two enter and leave critical at the same reading.

At a warning or critical level the value text and the fill (arc, bar, line,
lit segments) take that level's colour. Circular and Arc also draw each band
as a thin arc on the inner edge of the scale (warning bands stop where a
critical one starts); `redlineStart` stays a separate red zone. All four
limits are `paramUnit` props (stored in the PID's own unit, §5.6).

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
| `maxThickness` | real   | `0`          | Cap on the bar's thickness in dp; `0` fills the cell.   |
| `warnAbove`, `critAbove`, ... | real | `NaN` | The shared alerts (§3): the bar and its value turn amber / red. |
| `showLabel`    | bool   | `true`       | Header/title.                                           |
| `showValue`    | bool   | `true`       | Current value text.                                     |
| `trackColor`, `fillColor`, `warnColor`, `critColor` | color | theme | Manual overrides. |

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
| `card`        | bool | `false`           | The Home screen's reading card: centred in its cell, the reading's own decimals (`OBDParameterModel.decimalsFor`, so 90.0 °C and 14.1 V) and a bar along the bottom for where the value stands in its range (`barColor`). Pair with the cell's `frame` (§5.1) for the box. |

A value that rounds to zero never shows as `-0`.

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
| `lineColor`, `fillColor`, `labelColor`, `valueColor`, `backgroundColor` | color | theme | Manual overrides. `fillColor` defaults to `lineColor` at 22 % alpha, so overriding `lineColor` (the browser editor's Color row) recolors line and area together. |

Sampling only runs while the item is `visible`. Samples are right-aligned — the newest value sits flush with the right edge and older values scroll left as new ones come in.

### 3.7 `WarningLight`

Binary tell-tale indicator bound to a threshold condition. Small (ideally square), composable — drop several into a row for an idiot-light strip, or overlay one atop a larger gauge as a critical alert.

| Property            | Type   | Default                                   | Notes                                                              |
| ------------------- | ------ | ----------------------------------------- | ------------------------------------------------------------------ |
| `triggerAbove`      | real   | `NaN`                                     | Light turns on when `value >= triggerAbove`.                       |
| `triggerBelow`      | real   | `NaN`                                     | Light turns on when `value <= triggerBelow`. Both may be set.      |
| `alertHysteresis`   | real   | `NaN`                                     | Once lit, stays lit until the value is back inside the threshold by this much (§3, Alerts). |
| `label`             | string | `""`                                      | Short text shown inside the light (e.g. `"TEMP"`, `"!"`, `"FUEL"`).|
| `activeColor`       | color  | theme danger (`Style.statusDanger`, red) | Bright color when lit.                                             |
| `inactiveColor`     | color  | dark tint of `obdBoxBackground`           | Dim color when off.                                                |
| `activeLabelColor`  | color  | by luminance of `activeColor`             | Label color when lit: dark (`#141414`) on a light lamp (amber, yellow), white on a dark one. |
| `labelColor`        | color  | `App.Style.obdLabelColor`                 | Label color when off.                                              |
| `pulse`             | bool   | `false`                                   | Optional 1Hz opacity pulse while lit — reserve for critical alerts.|
| `maxSize`           | real   | `0`                                       | Cap on the lamp's size in dp; `0` fills the cell.                  |

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
| `showDegrees` | bool | `true`  | Center "247° / SW" readout (8-point compass label).|

### 3.10 `NowPlayingWidget` *(self-binding, lives in `frontend/dashboards/widgets/`)*

Album art + title + artist + progress bar for the active media source. Follows
the app-wide source rule (the phone while `phoneMirrorManager.mediaActive`,
else local media) and re-resolves on source / track changes. Wider than tall
it is a strip (art on the left); taller than wide a column (art on top, the
text centred, the progress bar at the bottom with the times): Home's music panel.

| Property         | Type | Default | Notes                  |
| ---------------- | ---- | ------- | ---------------------- |
| `showArt`        | bool | `true`  | Album art square.      |
| `showArtist`     | bool | `true`  | Artist line.           |
| `showProgress`   | bool | `true`  | Position/duration bar. |
| `seekable`       | bool | `false` | The progress bar takes a touch and seeks (`nowPlaying.seek`), with a handle and the times beside it. |
| `showBackground` | bool | `false` | The art, blurred and darkened, behind the card (the `backgroundBlurRadius` setting). |

### 3.11 `MediaControlsWidget` *(self-binding, lives in `frontend/dashboards/widgets/`)*

Previous / play-pause / next transport buttons driving the active media source
(same source rule as `NowPlayingWidget`). Interactive in rendered dashboards. No editable props.

---

### 3.12 `SegmentedGauge`

Horizontal segmented scale for RPM, load, fuel, temperature or other numeric
PIDs. Its default span is 6 × 2. The title and value have separate width
budgets; the strip is capped at 30 dp high. A value above the range fills all
segments and one below the range fills none; the numeric readout remains
unclamped. Each segment represents `1 / segmentCount` of the range, rounded
up for a nonzero fraction. This is a discrete level indicator, not a precision
plot or the vehicle's shift-light rules.

| Property | Type | Default | Notes |
| --- | --- | --- | --- |
| `segmentCount` | int | `20` | 2–40 segments. |
| `warnAbove`, `critAbove`, ... | real | `NaN` | The shared alerts (§3). Each lit segment past `warnAbove` / `critAbove` takes that level's colour, like a shift-light strip, and has a dim outline when unlit. |
| `showLabel`, `showValue` | bool | `true` | Header title / numeric value. |
| `decimals` | int | `0` | 0–4 decimal places. |
| `fillColor`, `trackColor`, `warnColor`, `critColor`, `labelColor`, `valueColor` | color | theme | The browser editor's Color row overrides `fillColor`. |
| `hasValue` | readonly bool | finite `value` | Missing and non-finite samples show `--`, fill no segments and never activate the warning. A real zero displays `0`. |

The usual `paramId`, `title`, `unit`, `min`, `max`, and manual `value`
overrides apply. Thresholds and range overrides in JSON use the PID's native
unit and follow the user's unit preference through the renderer.
An invalid range (`max <= min`, or non-finite bounds) fills no segments.

### 3.13 `ClockWidget` *(self-binding)*

A dashboard clock in `frontend/dashboards/widgets/`. Default span 4 × 2;
no PID and no additional OBD polling. It follows the app's 12/24-hour
preference and keeps showing time when the bottom-bar clock is hidden.

| Property | Type | Default | Notes |
| --- | --- | --- | --- |
| `showDate` | bool | `true` | Abbreviated weekday, month and day beneath the time. |
| `showSeconds` | bool | `false` | Per-widget seconds display. |
| `valueColor`, `labelColor` | color | theme | Time / date tint. The browser editor's Color row sets `valueColor`. |

Updates once a second while visible and refreshes immediately on becoming
visible. Time and date fit their allotted width and height.

### 3.14 `VehicleWidget` *(self-binding)*

The vehicle as a picture (`App.VehiclePicture`: `vehicleManager`'s active
vehicle) in `frontend/dashboards/widgets/`. Default span 4 × 3. With no
vehicle chosen it says so; an added model with no picture yet shows its name.

| Property | Type | Default | Notes |
| --- | --- | --- | --- |
| `showFrame` | bool | `true` | The reading cards' box around the picture. |
| `frameColor`, `labelColor`, `valueColor` | color | theme | The browser editor's Color row sets `frameColor`. |

### 3.15 `PhoneMirrorWidget` *(self-binding)*

The phone's screen in a cell, in `frontend/dashboards/widgets/`. Default span
3 × 6 (a phone is tall). While phone mirroring runs it shows the live picture
and passes touches through to the phone. Since 2026-11-25 it is the only place
the phone is shown (no bottom-bar button, nothing opens the full-screen
Phone Mirror page) while the flow is worked out; since 2026-11-26 the Media
Room can show it in its art corner as well. Once a phone has
been mirrored on this install (the `phoneMirrorUsed` setting, set by the core
at the first stream) it starts the session itself when it comes on screen (not
after the user closed mirroring in this run: `mainWindow.phoneMirrorStopped`);
before that nothing runs adb until Connect Phone is pressed or, on Linux, a
phone with USB debugging on is plugged in (the manager sees it on the USB bus
without adb and starts the session), so a fresh install does not start the adb
server (on Windows, a firewall prompt). It keeps the manager's
phone retries running while shown (`setViewActive`), and reports its size
(`setViewSize`) so an "auto" virtual display is drawn to fit it. Not live it
says why: "Connecting to your phone…", "No phone connected" with Connect Phone
(starts the session here), "Allow USB debugging on your phone", Set Up when
mirroring needs setting up (Settings > Phone, where a new phone is paired by
QR code). It runs wherever phone mirroring can (the `phoneMirrorEnabled`
setting is not consulted since 2026-11-25). The manager has one video
sink: the widget takes it while on screen. Every tap
is the widget's own (`octaveConsumesTap`). The bottom bar's Home button, pressed while Home is
already shown and mirroring runs, sends the phone to its own home screen. The built-in Home is built around
it (2026-11-25): the phone as the base, a small Now Playing card under it,
two readings on each side.

| Property | Type | Default | Notes |
| --- | --- | --- | --- |
| `showFrame` | bool | `true` | The reading cards' box around it. |
| `cornerRadius` | real | 8 dp | The frame's corners; the live picture is rounded to match. |
| `drivesDisplaySize` | bool | `true` | Off: a running session is shown as it is (letterboxed) instead of the phone's display being redrawn for this widget's shape; only a first start takes its size. The Media Room's art corner sets it off (since 2026-11-26), so moving between pages never restarts the session. Not in the catalog's `editableProps`. |
| `frameColor`, `labelColor`, `valueColor` | color | theme | The browser editor's Color row sets `frameColor`. |

### 3.16 `ShiftLightWidget` *(self-binding)*

The RPM shift light as a lamp, in `frontend/dashboards/widgets/`: dark until
a shift-light flag is reached, then that flag's colour. Default span 2 × 2.
It only draws: the flags, the flash and the look are `shiftLightManager`'s
(Settings > OBD > Shift Light). No props. Its catalog entry's
`demand: ["RPM"]` keeps RPM polled while a dashboard shows it.

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

Dashboards are **JSON specs**, not hand-written QML, and they are **not made in the app** (since 2026-11-24): you write the file, the app shows it. `DashboardRenderer.qml` reads a spec and instantiates widgets into a grid. **Home shows one dashboard** (§5.5): the built-in `home`, or any file in the user dashboards directory picked in Settings > Display > Home Screen. `OBDMenu.qml` is the Parameter Cards only (one card for every PID the connected vehicle reported in this session's supported-PID scan); it shows no dashboards, except that a dashboard being edited in the browser editor previews there.

### 5.1 Dashboard JSON schema (1 and 2)

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

**Portrait layout (schema 2).** A spec may add where each cell goes on a
portrait screen (taller than wide), one placement per cell, in the same
order as `cells`; data and options stay the cells' own:

```json
{
  "schema": 2,
  "gridColumns": 12, "gridRows": 8,
  "cells": [ ... ],
  "layouts": {
    "portrait": {
      "gridColumns": 8, "gridRows": 12,
      "cells": [ { "col": 0, "row": 0, "colSpan": 8, "rowSpan": 5 }, ... ]
    }
  }
}
```

The placements follow the cells' rules (in the grid, no overlaps; a missing
or colliding one goes to the nearest free spot). Without `layouts`, a
portrait screen gets a layout derived from the landscape one by fixed rules
(`dashboardManager.derivePortrait`: the grid turned, every widget its
on-screen shape, reading order), the same every time. The browser editor's
**Portrait** button derives it once and from then on edits and saves it.
Write `schema: 1` for a spec without `layouts`, so builds before 2 read it;
every built-in has a portrait layout. JSON Schema:
`docs/api/dashboard-spec.schema.json`.

**Optional keys (schema 1 still).**

```json
{
  "alertBanner": false,
  "showWhen": { "paramId": "TRANS_TEMP", "above": 110 },
  "thumbnail": "data:image/jpeg;base64,..."
}
```

- `alertBanner` — the renderer names every widget at its critical limit
  (`critAbove` / `critBelow`) with its value in a red strip along the top,
  so a small gauge can't go red unnoticed (warnings stay amber on the gauge
  itself). `false` turns the strip off; absent = on.
- `showWhen` — `{paramId, above, below}` in the PID's own unit, at least one
  limit. The key is kept in the file (`normalizeSpec`; the browser editor can
  set it), but nothing in the app acts on it.
- `thumbnail` — a picture of the dashboard for sharing, a JPEG or PNG data
  URL of at most 96 KB. `dashboardManager.thumbnailFromImage` makes one from
  a QML image, but nothing in the app calls it. Export carries the key; the
  browser editor drops it on save, as it would show the old layout.

**Per-widget overrides are just `props` keys.** The browser editor's Title / Unit, range and
Color options write ordinary props — a cell that renames, rescales and
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
`editableProps`, `specProps` — see §7).

**Only those keys reach the widget.** The renderer
applies a cell's `props` through `frontend/dashboards/CellProps.js`: a key the
catalog does not name for that type is ignored (with a `console.warn` when
it is some other property of the QML item), so a dashboard file cannot hide
a gauge (`visible`), resize it (`width`) or pin it to a made-up reading
(`value`). Each key is applied on its own: a value of the wrong type is
logged and that option stays at its default, and the rest still apply.

Per cell: `type` matches a widget registered in `WidgetCatalog` — the gauge primitives in `frontend/gauges/` (`CircularGauge`, `ArcGauge`, `BarGauge`, `LinearGauge`, `DigitalReadout`, `SegmentedGauge`, `SparklineGauge`, `WarningLight`, and the self-binding `GForceGauge` and `CompassGauge`, which ignore `paramId`) plus the self-binding widgets in `frontend/dashboards/widgets/` (`NowPlayingWidget`, `MediaControlsWidget`, `ClockWidget`, `VehicleWidget`, `PhoneMirrorWidget`, `ShiftLightWidget`, no `paramId`). `paramId` must be one of §6 — an unknown id is logged by `DashboardRenderer` and the widget is left unbound; an unknown `type` is logged and the cell is skipped. `props` keys must be exposed `Q_PROPERTY` on the widget — unknown keys are silently skipped. `"NaN"` (string) decodes to JS `NaN` since JSON can't encode it natively.

### 5.2 Add a built-in preset

1. Write `frontend/dashboards/presets/my-board.json` using the schema above.
2. Restart the app. `DashboardManager` enumerates `presets/` on startup; it is then one of the dashboards Settings > Display > Home Screen offers. No code changes. (`home.json` is the only one shipped.)

### 5.3 Add a user dashboard

**Write the file.** A user dashboard is one JSON file with the schema above.
There is no editor in the app. The widget set and the
props each type takes come from the widget catalog: the data in
`frontend/dashboards/widget-catalog.json` (served as
`dashboardManager.widgetCatalog`, published as
`docs/api/widget-catalog.json`), which the `WidgetCatalog` singleton
(`frontend/dashboards/WidgetCatalog.qml`) pairs with each type's QML url.

**Show it.** Settings > Display > Home Screen shows every dashboard as a card
(`homeScreenCards`; a picture when there is one, else a plan of its cells);
the one picked is what Home shows
(`dashboardManager.setHomeDashboard(id)`).

**The vehicle gate.** A dashboard may name any PID, so it can be written for
another car or shared. At render time `DashboardRenderer` swaps a cell whose
PID the connected vehicle does not report for a "Not supported by this
vehicle" placeholder (see §5.6).

**In a browser.** With the service on, `http://<car>:9876/dashboards`
(`sdk/web/dashboards.html`) is a dashboard editor for a laptop or phone:
the list (thumbnails, Edit / Customize / Copy / Download / Delete, New from
a template, Import by paste or file with a report of what didn't come
across), and an editor with drag to move and resize (placed by the app's own
`arrange` / `nearestFree` / `rescale`), the widget's data, look, range and
alert options from the catalog, grid density, the portrait layout and undo.
It writes the same files. **Show on the car** sends every change to
`dashboardManager.previewDashboard`: the car's OBD page shows it ("Preview
from another device") until the page ends it or 60 s after its last update.
Off the device the service needs `--service-bind` and a pairing code
(`octave --pair-code`).

**Sharing.** `dashboardManager.exportDashboard(id)` writes
`<Downloads>/OCTAVE-dashboards/<id>.json`; `importDashboard(file)` /
`importDashboardFromText(json)` validate and always make a new user
dashboard (§5.4), with `lastImportReport` saying what didn't come across. No
button in the app calls them now; the browser editor does, and a file copied
into the directory below needs no import.

**Where it goes.** Under the OS config dir:

- Linux: `~/.config/OCTAVE/dashboards/my-board.json`
- macOS: `~/Library/Application Support/OCTAVE/dashboards/my-board.json`
- Windows: `%APPDATA%/OCTAVE/dashboards/my-board.json`

`DashboardManager` watches the user dashboards directory, so a hand-dropped file
is listed within about a second (see §5.4 Hot reload); no restart
is needed. `dashboardManager.refresh()` forces an immediate rescan.

### 5.4 Registry rules

- Built-in preset ids must be unique. User ids colliding with a built-in are rejected at load time (logged).
- Built-ins are read-only: `deleteDashboard` on one hides it (ids listed in `<userDir>/.hidden-builtins`, id still reserved) and `restoreBuiltInDashboards()` brings them back; `hiddenBuiltInCount` says how many are hidden (no button in the app uses either now). `home` is never hidden: `deleteDashboard` refuses it. The id `grid` (`kParameterCardsId`) is reserved. "Duplicate" via `dashboardManager.duplicateDashboard(sourceId, newLabel)` creates an editable user copy with an auto-generated unique id. New ids come from `dashboardManager.uniqueIdForLabel(label)` (the browser editor, Duplicate and Import alike); it never returns a built-in id (hidden ones included) or `grid`, and `saveDashboard` refuses both.
- **Newer schema.** A spec whose `schema` is above `dashboardManager.supportedSchema` (`kSupportedSchema`, 2) renders no cells, demands no PIDs, and shows one centred "This dashboard needs a newer version of OCTAVE" message. Import refuses such files; this covers one dropped into the folder by hand.
- **One set of spec rules** (`DashboardManager::normalizeSpec`, applied by `saveDashboard`, `loadDashboard` and every import, so the browser editor, Duplicate, Import and the renderer all see the same thing):
  - Kept keys: `schema`, `id`, `label`, `gridColumns`, `gridRows`, `margins`, `spacing`, `thumbnail`, `alertBanner`, `showWhen`, `layouts.portrait`, `cells` and per cell `type` / `paramId` / `col` / `row` / `colSpan` / `rowSpan` / `props`; anything else is dropped. `props` keeps scalars only (bool, number, string ≤ 200 chars; `"NaN"` survives).
  - Numbers must be JSON numbers (a string or bool gets the default). Grid size: not a positive number = 12 × 6, else capped at 48. `margins` / `spacing`: 0..200, absent or `null` = the renderer's 20 / 16. Caps: 256 cells (refused on save and import; a file on disk shows its first 256), label 80 chars (whitespace collapsed).
  - A cell off the grid keeps its size and moves back in (as a drag does); only a span wider than the grid shrinks.
  - Cells never overlap. In file order the earlier cell wins; a later one shrinks toward 1×1 (wider side first, as a newly placed widget does) and is dropped, logged, only if its corner is taken.
  - Unknown widget types and PIDs are **kept**, so a newer build's dashboard survives an older build's save: the renderer skips an unknown type (its grid area stays empty) and leaves an unknown PID unbound; the browser editor lists an unknown type by its name with "(unknown)".
  - Schema: missing = 1. A newer schema is refused by save and import, and `loadDashboard` passes such a file through untouched so the renderer can show its placeholder; the browser editor won't open it ("Needs a newer version of OCTAVE to edit").
  - `saveDashboard` needs a label and a file-safe id (`[A-Za-z0-9_-]`, ≤ 64; new ids are slugs from `uniqueIdForLabel`, which `saveDashboard` calls itself when the spec has no id). A file with no `cells` loads as an empty dashboard.
  - Grid placement (`src/util/dashboardgrid.{h,cpp}`) is served to every client: `dashboardManager.canPlace(cells, cols, rows, col, row, colSpan, rowSpan, exceptIndex)` says whether a cell fits, `dashboardManager.fitAt(cells, cols, rows, col, row, colSpan, rowSpan)` gives the span a new widget shrinks to, and the browser editor moves and resizes with `arrange` / `nearestFree` / `rescale`. `dashboardManager.normalizedSpec(spec)` applies these rules to a spec that doesn't come from a file.
  - A known widget's `editableProps` are held to their catalog `kind`: a `bool` must be a JSON bool, an `int` / `real` a number (clamped to `min` / `max`, an `int` rounded), an `enum` one of its `options`, a `string` a string; anything else is dropped, so the widget shows its default. Other props (title, unit, min, max, colours, keys the catalog doesn't curate) are kept in the file as before; the renderer applies only the ones the catalog names for the type (§5.1).
  - Every shipped preset already satisfies these rules (`presetsAreAlreadyNormal` in `tests/cpp/tst_dashboardmanager.cpp`).
- **Validation in QML.** `DashboardRenderer` still checks every cell against `WidgetCatalog.isKnownType()` and `OBDParameterModel.hasParameter()`, `console.warn`s failures with the dashboard id and never crashes the view.
- **Import.** `importDashboard` / `importDashboardFromClipboard` /
  `importDashboardFromText` apply the rules above and also refuse a spec
  with a bad `schema` or no `cells` list, and input over 512 KB. The id is
  always regenerated from the label (`label` defaults to "Imported
  dashboard"), so an import never overwrites an existing dashboard.
  Failures set `dashboardManager.lastShareError`.
- **Hot reload.** `DashboardManager` watches the user dashboards directory (`QFileSystemWatcher`, 300 ms debounce, rescans only when the file-name/mtime/size signature changed). Drop a JSON in from a terminal or sync client and it is listed (Settings > Display > Home Screen, the browser editor) without restarting; delete one and it disappears. If the deleted file was Home's dashboard, Home goes back to the built-in `home`.

### 5.5 Home is a dashboard

The Home screen (`frontend/HomeDashboard.qml`, loaded by `MainMenu.qml`)
shows the dashboard `dashboardManager.homeDashboardId` names in the same
`DashboardRenderer`: the built-in `home` (`presets/home.json`) until
`setHomeDashboard(id)` picks another (Settings > Display > Home Screen; `""`
goes back to the built-in), and again when that dashboard is deleted. `home`
cannot be hidden. The choice is one line in `<config>/dashboards/.home`.

- **The default layout** (`presets/home.json`): the phone's screen
  (`PhoneMirrorWidget`) in the middle, a Now Playing card under it, two
  reading cards on each side (speed, RPM, coolant, voltage); portrait: the
  phone on top, the four readings in a row under it, music along the bottom.
  `tst_dashboardmanager` (`homePresetIsPhoneMusicAndFourReadings`) holds it
  to that.
- **Carried over once**: `migrateClassicHome()` (called from `AppCore`)
  turns the classic Home's settings (`homeOBDParameters`, `homeVehicleCard`)
  into a user dashboard "My Home" when they
  differ from the defaults. Those settings are deprecated. That dashboard is
  built by `DashboardManager::homeSpec(readings, vehicle)`, a different
  layout: music on the left, two columns of reading cards over the vehicle
  in the middle, the phone on the right; portrait: music on top, the vehicle
  beside the readings, the phone along the bottom.
- **Taps.** Only the music card opens a page for now
  (`HomeDashboard.tapActionsThatOpen`, `["media"]` since 2026-11-26; all were
  off from 2026-11-24). Only Home sets `DashboardRenderer.interactive`. A tap on a cell
  emits `cellTapped(action, index)`: the cell's `props.tapAction`, else its
  type's catalog `defaultTapAction`, else `obd` for a PID-bound cell
  (`sensors` for an IMU reading), never for `none`. Home opens the music
  page for `media`, the OBD menu for `obd`, Sensors for `sensors` and
  Settings > Device > Vehicle for `vehicle`. A widget keeps a touch to itself
  with `function octaveConsumesTap(x, y)` (Now Playing's seek bar).
- **Frames.** `"frame": true` in any cell's `props` draws the reading cards'
  box behind the widget (`dashboards/CellFrame.qml`).
- **Prompts** are Home's, not the widgets': "No vehicle connected" + Connect
  over the block of OBD-bound cells (a strip along the bottom when other
  widgets stand in that block), the music prompt over a Now Playing cell
  with no music, "No sensor" on IMU cells. `DashboardRenderer.cellRect(i)`
  gives a cell's place; `usesImu` says whether the spec shows the motion
  sensor (a catalog `usesImu` type or an IMU `paramId`).
- Until it has been checked in the car the hand-built Home stays selectable:
  Classic card of Settings > Display > Layout > Home Screen (`homeScreen` = `classic`).
  See `TODO/home-dashboard.md`.

### 5.5b Hand-built Home screens

Home need not be a grid dashboard. `frontend/home/Home*.qml` are full-screen
QML screens drawn by hand, with no layout rules: `HomeCockpit` (an instrument
cluster), `HomeGlass` (the music over blurred album art, the drive in glass
panes), `HomeTrail` (heading tape, artificial horizon, G-force), `HomeRetro`
(seven-segment figures and a bar-graph tachometer), `HomeRings` (four
readings as concentric rings) and `HomeTelemetry` (live traces and the black
box's state). Each is a
`HomeScreen` (`frontend/home/HomeScreen.qml`):

```qml
HomeScreen {
    paramIds: ["SPEED", "RPM"]     // polled while the screen is on screen
    usesImu: false                 // true: the full IMU rate as well
    Text { text: parent.text("SPEED") + " " + parent.unitOf("SPEED") }   // "--" without a reading
}
```

`value(id)` is NaN without a reading, `norm(id)` is where it stands in its
range (0 to 1), `np` / `art` / `hasTrack` are what is playing, `u` is 1 % of
the short side and `portrait` the shape: size everything from those so the
screen works at any size and either way up. The primitives of §3 can be used
inside one (the Cockpit's dial is a `CircularGauge`). Register a new screen in
`frontend/home/HomeScreens.js` with a `preview` picture (640 x 310, a shot of
the screen with the simulated car, in `frontend/assets/home/`); it then
appears as a card in Settings > Display > Home Screen (the `homeScreen`
setting holds its id). Add it to
`tst_dashboardwidgets`' `homeScreens_data`.

### 5.6 Vehicle gate and polling demand

- **Range props are in the PID's own unit.** A spec's `min`, `max` (for
  widgets with `rangeProps`) and the thresholds a widget's `editableProps`
  mark `"paramUnit": true` (`redlineStart`, `warnAbove` / `warnBelow` /
  `critAbove` / `critBelow`, `triggerAbove`, `triggerBelow`) are written in the parameter's catalog unit (°C, MPH, m),
  whatever the user's display units. `WidgetCatalog.paramUnitProps(type)`
  lists them; `DashboardRenderer` binds them through
  `OBDParameterModel.toDisplay`, so a `critAbove: 105` on coolant means 221 °F
  for a °F user. A new threshold prop in parameter units needs the flag.
  `alertHysteresis` is in the PID's own unit too, but it is a difference, so
  `CellProps` converts it with the scale alone
  (`OBDParameterModel.toDisplayDelta`).
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
  `HomeDashboard` registers them as the `"homeDashboard"` consumer while Home
  is shown (`OBDMenu` as `"dashboard"` for a dashboard previewed from the
  browser editor). Other consumers: `"cards"` (Parameter Cards), `"homeScreen"` (a hand-built Home, §5.5b), `"home"` (`HomeOBDView`, the classic Home), `"carMenu"` (RPM/SPEED
  for the 3D Jeep while `CarMenu` is visible). Backend side: `wiki/obd-manager.html`.

### 5.7 Templates

`frontend/dashboards/templates/*.json` are the starting points **+ New**
offers, in their `order`. A template is a schema-1 spec on the Normal 6 × 4
grid plus `id`, `label`, `description` and `order`, whose cells name a
`slot` instead of (or as well as) a `paramId`:

```json
{ "type": "ArcGauge", "col": 0, "row": 0, "colSpan": 3, "rowSpan": 2,
  "slot": { "prefer": ["COOLANT_TEMP", "OIL_TEMP"], "kinds": ["temperature"] } }
```

`dashboardManager.fromTemplate(id, supported)` fills each slot with the first
`prefer` id the car reports (`supported`; empty = no car known, so the first
preferred), else the first reported id of one of its `kinds`, never one
another slot took; a slot nothing fits stays unbound. IMU ids count as reported. The result has no `id` and
goes through the spec rules. `tst_dashboardmanager` checks every template
loads, fits its grid and names only known PIDs and widgets.

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
gauge can bind them by `paramId`, the browser editor's data picker lists
them, and the demo sweep covers them. The vehicle gate never applies to them.

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
2. Get the **shared binding API** (§3 table top rows) and the alert rule by
   making the root `ValueGaugeBase { id: root ... }`
   (`frontend/gauges/ValueGaugeBase.qml`): `paramId`, `title`, `unit`, `min`,
   `max`, `value`, `hasValue`, `decimals`, the `warn*` / `crit*` limits,
   `alertLevel`, `_alertColor` and `_alertOpacity` are then already there.
   Do not paste the block from another gauge. A widget that is not a value
   gauge looks its PID up with `App.OBDParameterModel.displayInfo(paramId)`
   (one map lookup; never scan `displayParameters`).
3. Animate `_animNorm` with a `Behavior on _animNorm { NumberAnimation
   { duration: 100–150; easing.type: Easing.OutCubic } }`. Don't animate
   the raw `value` — that's upstream.
4. Use `Shape` + `ShapePath` + `PathAngleArc` for curves (GPU-accelerated).
   Use `Rectangle` + `transformOrigin` + `rotation` for small marks.
5. Declare `octaveSupportedKinds` (array of PID `kind` strings, or `["*"]`)
   near the top. Nothing reads it at run time: the catalog entry's
   `supportedKinds` (step 6) is what the browser editor's data picker
   filters on, so keep the two the same.
6. Register the widget in the catalog: its data in
   `frontend/dashboards/widget-catalog.json` (`widgets`, in the order they are listed)
   and its QML url in the `urls` map of `frontend/dashboards/WidgetCatalog.qml`
   (a type needs both). The renderer, `normalizeSpec`, the browser editor
   and every other service client read the catalog — a widget
   missing there won't render or be placeable. Then regenerate
   `docs/api/` (`./build/octave --dump-api docs/api`). Each entry declares:
   - `type`, `displayName`, `glyph`, `defaultColSpan` / `defaultRowSpan`
     (for a 12 × 6 grid)
   - `category` — one of the catalog's `categories` (the groups of the
     browser editor's widget list); `description` — one line about the
     widget; `demo` — `{ paramId, props }` to show it with in a preview
     (`{}` for self-binding widgets)
   - `minSize` — `{ "w", "h" }` in dp: the smallest size the widget stays
     readable at (check it with `tst_dashboardwidgets` `smallCellBounds`);
     `aspect` — its natural width / height. The browser editor's minimum span and
     size choices come from these (`dashboardManager.minSpan`,
     `sizeOptions`).
   - `supportedKinds` — must mirror step 5 (`[]` = self-binding, no PID picker)
   - `labelProps` — `true` if the widget has `title` + `unit` string props
     that default to the PID's metadata (enables the Title / Unit overrides)
   - `rangeProps` — `true` if it has `min` + `max` real props (Min / Max)
   - `colorProps` — the color props the single Color swatch row writes, all
     set to the same value (e.g. `["fillColor", "needleColor"]`); `[]` hides
     the row. Swatches come from the catalog's `colorSwatches`.
   - `specProps` — the widget's other public props a hand-written spec may
     set but the browser editor does not offer (`trackColor`, `labelColor`, `warnColor`,
     `alertHysteresis` ...). A prop in none of these lists is not applied
     (§5.1), so list every prop you document for the widget.
   - `defaultTapAction` — what a tap on the widget asks an interactive
     host (Home) for: `media`, `obd`, `sensors`, `vehicle` or `none`
     (§5.5); `usesImu` — `true` if it reads the motion sensor by itself;
     `demand` — PIDs a self-binding widget needs polled (`["RPM"]` for the
     shift light). All optional.
   - `editableProps` — `{ key, label, kind, def, section[, min, max][, options][, paramUnit] }` with
     `kind` ∈ `bool` (switch) · `int` / `real` (number field, clamped to
     `min`/`max`) · `string` (text field) · `enum` (segmented buttons over
     `options`). `def` is the gauge's real default (`null` for NaN-when-unset
     thresholds). `section` ∈ `look` · `range` · `advanced`: where the
     browser editor's options panel shows it. A value gauge lists the shared alert props
     (§3). The catalog entry is the only list the browser editor reads; a gauge
     declares no manifest of its own.
   - If a kind of parameter suits the new widget best, point the catalog's
     `suggestions.byKind` (or `byId`) at it: the browser editor proposes it for such a value.
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

- **No reading yet:** a PID that has not been polled reads `NaN`, never `0`
  (`OBDParameterModel.sample()`). The primitives show an empty gauge and
  "--" for the value; a new widget must treat `NaN` as "no reading" too
  and not draw it as the minimum.
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
│   ├── CompassGauge.qml       rotating compass card (self-binding: HEADING)
│   ├── SegmentedGauge.qml     row of lit segments (shift-light strip)
│   ├── ValueGaugeBase.qml     what the seven value gauges share: PID binding, metadata, alerts
│   └── AlertLevel.js          the warning / critical rule, its hold (levelHeld) and the bands
├── dashboards/
│   ├── widgets/               self-binding non-gauge dashboard widgets
│   │   ├── NowPlayingWidget.qml    art + title + artist + progress (local/phone)
│   │   ├── MediaControlsWidget.qml prev / play-pause / next transport
│   │   ├── VehicleWidget.qml       the vehicle's picture
│   │   ├── PhoneMirrorWidget.qml   the phone's screen, live while mirroring
│   │   └── ShiftLightWidget.qml    the RPM shift light as a lamp
│   ├── DashboardRenderer.qml  JSON spec → grid of widget instances (taps for Home)
│   ├── CellFrame.qml          the card behind a cell with `frame: true`
│   ├── CellProps.js           how a cell's props reach its widget (catalog-listed keys only)
│   ├── widget-catalog.json    the catalog data: listing metadata (category,
│   │                          description, demo), spans and sizes,
│   │                          supportedKinds, label/range/color flags,
│   │                          editableProps (with section), colorSwatches,
│   │                          categories, suggestions
│   ├── WidgetCatalog.qml      singleton: that data (dashboardManager.
│   │                          widgetCatalog) + each type's url
│   ├── templates/             starting points (fromTemplate; the browser editor's "New from")
│   └── presets/               built-in dashboard JSON specs (ship with bundle)
│       └── home.json          Home's default (the only built-in since 2026-11-24;
│                              sport / minimal are test fixtures, tests/cpp/fixtures/presets)
├── OBDParameterModel.qml      singleton aggregator: values, gate, demo mode;
│                              metadata from parameterCatalog (src/managers/parametercatalog.cpp)
├── HomeDashboard.qml          Home: the dashboard picked in Settings > Display
└── OBDMenu.qml                Parameter Cards; previews a dashboard edited in a browser

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
