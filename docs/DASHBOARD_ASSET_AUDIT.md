# Dashboard asset audit — 2026-10-29

The library has a useful visual foundation and sound dashboard plumbing, but
several existing gauges need correctness and sizing repairs before it can be
called consistently polished. The strongest current presentation is the
large landscape circular-gauge layouts. Small cells, vertical bars, and
missing readings expose the largest problems.

This pass audits the 11 original widgets and five presets, and adds two
widgets: Segmented and Clock. The legacy defects listed below remain open.

## What was checked

- The actual C++ app with a separate `--sim --profile` session: all five
  presets captured at 1280×720, 800×480, and 390×844. No saved user dashboards
  or settings were changed. These captures show the simulated car at idle.
- All original widgets instantiated from their actual QML sources, with
  controlled readings, Google Sans, CosmicVoyager and SolarizedLight palettes,
  at large and small sizes. Both orientations of BarGauge were included.
- Missing readings and geometry measurements in that fixture. The fixture
  uses PySide/Qt 6.11.1; the app and committed C++ tests use Qt 6.12.0.
- The catalog, renderer, editor interfaces, parameter/unit binding, theme
  assets and existing automated coverage. Ten relevant CTest targets passed,
  including the new widget tests, dashboard manager, vehicle gate, parameter
  catalog/store, themes, now-playing, units, API documentation and conformance.

Desktop captures used Xvfb and software OpenGL. This is not a real touchscreen,
Android glyph, sunlight-legibility, real-ECU or target-hardware frame-rate test.
Media widgets were visually inspected with controlled metadata; actual phone
transport taps were not exercised in this pass.

## Inventory and verdicts

| Asset | Assessment | Main follow-up |
| --- | --- | --- |
| CircularGauge | Attractive in roomy landscape cells; consistent theme colors and useful optional redline. | Needle crosses the readout at some values; outer tick tips can clip at cell edges; missing input becomes zero. |
| ArcGauge | Useful shape, but geometry does not reserve enough room for its full readout. | In the 164×103 fixture the value extends below the cell and the unit begins around y=118. Size arc and text together. |
| BarGauge | Simple horizontal display; vertical switching is currently unreliable. | Fix anchor/size transitions and proportional fill, then constrain header text. |
| LinearGauge | Bidirectional zero-origin presentation is useful. | Short-cell text/track can clip, endpoint indicator extends beyond the cell, and missing input looks like a real zero. |
| DigitalReadout | Clear hierarchy at generous sizes; padding and width fitting work. | Height fit uses font pixel sizes rather than measured line heights. In the 164×103 fixture, the unit begins at y=107 and is clipped. |
| SparklineGauge | Useful stable-cadence history; theme-linked fill and line work. | Missing samples become fabricated zero history; long headers lack a width budget. No time/range annotations. |
| WarningLight | Active/inactive state is obvious, but the default text treatment is too large. | Missing input can activate low-threshold warnings; labels have no width fit; white-on-yellow warnings need a darker foreground. |
| GForceGauge | Clean, recognizable rings and trail. | No-data state reads as 0.00 g. Independent axis clamping permits diagonal dots outside the circular scale. |
| CompassGauge | Legible card with upright letters and shortest-path rotation. | No-data state reads as north; numeric rounding can show 360° near the wrap. |
| NowPlayingWidget | Good title/artist hierarchy, fallback artwork and bounded progress. | Smaller cells truncate titles quickly; image clipping is rectangular despite the frame radius. |
| MediaControlsWidget | Familiar three-button layout and shared now-playing routing. | No minimum touch size; glyphs use the selected text font rather than the bundled symbol font. Android/custom-font behavior needs device verification. |

The shared assets are two bundled text fonts (Google Sans and JetBrains Mono),
a symbol font, SVG navigation/transport icons, missing-art artwork and the
theme catalog (18 named static palettes plus Album Art Capture). Existing
SVG transport icons could replace the text glyphs in MediaControlsWidget.
The Jeep and music-device GLB assets belong to other app views; they are not
currently dashboard palette entries. Their mesh/rig quality is outside this
widget audit.

[Original widgets, dark](audits/dashboard-assets-2026-10-29/widgets-dark.png) ·
[Original widgets, light](audits/dashboard-assets-2026-10-29/widgets-light.png)

## Repair priorities

### 1. Make missing data truthful — high priority

Every original PID-bound primitive uses `liveValue(paramId) || 0`.
G-force and compass use the same fallback for their sensor readings.
Clearing the fixture's readings produced zero readouts, a north heading, and
an **active low-fuel warning with no fuel sample**. Sparkline keeps sampling
those zeroes. The renderer hides PIDs unsupported by a scanned vehicle, but
does not mask missing samples for supported PIDs or self-binding sensors.

Use an explicit finite-sample state, `--` readouts, inactive needles/fills and
no threshold evaluation until a sample exists. Decide separately how old a
sample may be before it is considered stale. Zero must remain a valid sample.

Sources: `frontend/gauges/*.qml`, `frontend/OBDParameterModel.qml`,
`frontend/dashboards/DashboardRenderer.qml`.

### 2. Repair BarGauge geometry — high priority

In the loader fixture, changing `orientation` to `vertical` after creation
leaves the track at the former full width. At a widget size of 284×243 and
fuel = 8%, the track measures 284×191 and its fill 284×235, extending above
the track. This follows the same post-load property assignment pattern used
by the dashboard renderer and editor.

Even outside that transition, the fill's `radius * 2` minimum can greatly
overstate a small percentage in a thick bar. Separate orientation layouts
or use explicit geometry, and clip a proportional fill inside the track.
Verify 0%, 1%, 8%, 50%, 100%, orientation changes and narrow/tall cells.

Source: `frontend/gauges/BarGauge.qml` (track anchors and fill dimensions).

### 3. Establish minimum readable layouts — high priority

`clip: true` keeps a widget inside its grid cell but does not make its content
fit. ArcGauge and DigitalReadout clip their readouts in the small fixture.
Bar/Sparkline headers are unconstrained rows; `elide` without a width cannot
protect the adjacent value. Sport puts four bars in a one-row footer: at
800×480 their labels are nearly absent. TJ Wrangler's dense portrait layout
also clips labels and distorts thick bars.

Reserve measured text heights, give labels/value fields explicit width
budgets, and introduce sensible minimum spans or compact variants in the
editor. Add portrait-specific layouts or reflow for presets rather than
keeping all landscape columns in a narrow window.

[Sport at 800×480](audits/dashboard-assets-2026-10-29/sport-800x480.png) ·
[TJ Wrangler in portrait](audits/dashboard-assets-2026-10-29/tj-wrangler-390x844.png)

### 4. Improve glance readability — medium priority

Needles pass behind circular title/value/unit text. Dial ticks have no numeric
labels, making the needle secondary to the central number. Warning labels
consume nearly the entire disc; the white active label is particularly weak
against the yellow fuel warning. Use a separate needle/readout zone, optional
sparse scale labels, fitted warning text and a foreground appropriate to the
warning fill. G-force should clamp vectors radially if the dot is intended to
stay inside the scale.

### 5. Complete editor and theme behavior — medium priority

- `maxThickness` and `maxSize` are used in shipped presets, but are missing
  from the catalog's editable property list. They cannot be adjusted in the
  properties panel.
- Sparkline repaint handlers cover samples and dimensions, but not line/fill
  colors, range, or `fillBelow`; a running trace catches up on the next sample
  (up to five seconds at the slowest catalog interval). Add explicit repaint
  dependencies for immediate theme/option feedback.
- Verify MediaControlsWidget with the bundled symbol font or SVG assets on
  Android, and establish a minimum usable control size.

## Preset assessment

| Preset | Landscape | Portrait |
| --- | --- | --- |
| Minimal | Coherent and legible; very large dials with relatively small central numbers. | Three tiny dials across the middle with large empty areas. Needs stacking. |
| Full Grid | Strongest consistent presentation at 1280×720. | Four columns shrink the readouts excessively; needs fewer columns. |
| Sport | Good main speed/RPM hierarchy. Bottom bar labels fail at 800×480. | Retains the landscape grid and loses effective use of space. |
| Performance | Understandable layout, but the arc leaves substantial unused width and warning text is crowded. | Arc survives better than other presets; secondary content remains small and widely spaced. |
| TJ Wrangler | Useful information density; needle/readout overlaps and rounded readings weaken it. | Dense rows and bars become difficult to interpret. |

The renderer/catalog architecture is worth keeping: one shared type registry,
native-unit JSON thresholds converted at render time, explicit PID demand,
unsupported-vehicle placeholders, data-only import validation and predictable
grid placement are all useful foundations. The tests passing does not certify
the legacy widgets' visual correctness; most existing coverage exercises the
backend and integration rules.

## First additions completed

**SegmentedGauge** is now available as **Segmented** in the palette (6×2).
It provides 2–40 segments, optional high threshold, theme colors, title/value
toggles and explicit decimal precision. Missing/non-finite samples show `--`,
fill no segments and cannot activate a warning. Numeric values remain unclamped
while the level clamps to its range. This is a discrete numeric gauge; it does
not replace the app's shift-light rule engine.

**ClockWidget** is now available as **Clock** (4×2). It follows the app's
12/24-hour preference, optionally shows date/seconds, updates only while
visible, and remains useful with the bottom-bar clock hidden. It adds no PID
demand.

Both are registered in the renderer/editor catalog and published API catalog.
Both were also placed through the running app's editor; mouse-wheel scrolling
to Clock and its PID-free property panel were checked.
Adding the thirteenth widget exposed a palette overflow; the palette now
scrolls within 90% of window height and adapts to 2–4 columns.
The new `tst_dashboardwidgets` covers missing versus zero, non-finite values,
range limits, precision, renderer loading, native/display threshold conversion,
PID demand, clock format and text bounds at 80×32, 160×50 and 320×100.
Palette tests cover desktop, small landscape and portrait windows, including
scrolling to and selecting the final Clock entry.

[New widgets, dark](audits/dashboard-assets-2026-10-29/additions-dark.png) ·
[New widgets, light](audits/dashboard-assets-2026-10-29/additions-light.png)

After the high-priority repairs, useful next additions are a pitch/roll widget
(using the existing IMU parameters), a current/peak readout with reset, and a
compact labeled status strip. A shared data-state/formatting helper would keep
those additions from repeating the original primitives' missing-data behavior.
