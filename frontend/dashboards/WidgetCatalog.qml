// WidgetCatalog.qml
//
// Single source of truth for the gauge primitives a dashboard can contain.
// Maps a spec `type` string to its QML source, plus the display metadata and
// curated editable props the Phase 3 editor surfaces.
//
// DashboardRenderer (render-time), the editor palette (which types you can add),
// and the properties panel (which props you can tweak) all read from here, so
// the supported-widget set can't drift between rendering and editing. It's also
// the natural home for the type whitelist the deferred renderer validation will
// use (see TODO/dashboards-roadmap.md, "fast-follow").
//
// Url note: this file lives in frontend/dashboards/, so the `../gauges/` prefix
// resolves to frontend/gauges/ regardless of which context loads the catalog.
pragma Singleton
import QtQuick 2.15

QtObject {
    id: catalog

    // Ordered list — also drives palette ordering. Each entry:
    //   type:            spec `type` string (matches the .qml filename)
    //   url:             resolved source for a Loader
    //   displayName:     human label for the palette
    //   glyph:           single-character icon for the palette card
    //   defaultColSpan/defaultRowSpan: span a freshly-placed cell takes
    //   supportedKinds:  PID `kind` values this widget accepts — MUST mirror the
    //                    `octaveSupportedKinds` declared in the gauge QML itself
    //                    (the catalog exists so the PID picker can filter without
    //                    instantiating a widget). "*" = any PID.
    //   labelProps:      true if the widget has `title` + `unit` string props
    //                    that default to the bound PID's metadata — the panel
    //                    then offers per-widget Title / Unit overrides.
    //   rangeProps:      true if the widget has `min` + `max` real props that
    //                    default to the PID's range — the panel offers Min / Max.
    //   colorProps:      props the panel's single "Color" swatch row writes
    //                    (all set to the same color; empty = no color control).
    //                    Unset = the theme color, so dashboards keep following
    //                    EnvironmentTheme unless the user picks a color.
    //   editableProps:   widget-specific props the properties panel exposes.
    //                    Each: { key, label, kind, def[, min, max][, options] }
    //                    where kind ∈ "bool" | "real" | "int" | "string" | "enum".
    //                    `def` is the gauge's actual default (null for
    //                    NaN-when-unset thresholds — the panel shows these as
    //                    empty/unset). `min`/`max` clamp numeric input; `options`
    //                    lists the enum values. Keep in step with the gauge's own
    //                    `octaveEditableProps`.
    readonly property var widgets: [
        {
            "type": "CircularGauge",
            "url": Qt.resolvedUrl("../gauges/CircularGauge.qml"),
            "displayName": "Circular",
            "glyph": "◯",
            "defaultColSpan": 4, "defaultRowSpan": 4,
            "supportedKinds": ["*"],
            "labelProps": true, "rangeProps": true,
            "colorProps": ["fillColor", "needleColor"],
            "editableProps": [
                { "key": "showNeedle",         "label": "Needle",          "kind": "bool", "def": false },
                { "key": "showTicks",          "label": "Ticks",           "kind": "bool", "def": true },
                { "key": "showCenterReadout",  "label": "Readout",         "kind": "bool", "def": true },
                { "key": "redlineStart",       "label": "Redline start",   "kind": "real", "def": null },
                { "key": "decimals",           "label": "Decimals",        "kind": "int",  "def": 0, "min": 0, "max": 4 },
                { "key": "majorTickCount",     "label": "Major ticks",     "kind": "int",  "def": 9, "min": 2, "max": 20 },
                { "key": "minorTicksPerMajor", "label": "Minor per major", "kind": "int",  "def": 5, "min": 0, "max": 10 },
                { "key": "startAngle",         "label": "Start angle",     "kind": "real", "def": 135, "min": 0, "max": 360 },
                { "key": "sweepAngle",         "label": "Sweep angle",     "kind": "real", "def": 270, "min": 30, "max": 360 }
            ]
        },
        {
            "type": "ArcGauge",
            "url": Qt.resolvedUrl("../gauges/ArcGauge.qml"),
            "displayName": "Arc",
            "glyph": "◠",
            "defaultColSpan": 6, "defaultRowSpan": 3,
            "supportedKinds": ["*"],
            "labelProps": true, "rangeProps": true,
            "colorProps": ["fillColor", "needleColor"],
            "editableProps": [
                { "key": "showNeedle",         "label": "Needle",          "kind": "bool", "def": false },
                { "key": "showTicks",          "label": "Ticks",           "kind": "bool", "def": true },
                { "key": "showReadout",        "label": "Readout",         "kind": "bool", "def": true },
                { "key": "redlineStart",       "label": "Redline start",   "kind": "real", "def": null },
                { "key": "decimals",           "label": "Decimals",        "kind": "int",  "def": 0, "min": 0, "max": 4 },
                { "key": "majorTickCount",     "label": "Major ticks",     "kind": "int",  "def": 5, "min": 2, "max": 20 },
                { "key": "minorTicksPerMajor", "label": "Minor per major", "kind": "int",  "def": 4, "min": 0, "max": 10 }
            ]
        },
        {
            "type": "BarGauge",
            "url": Qt.resolvedUrl("../gauges/BarGauge.qml"),
            "displayName": "Bar",
            "glyph": "▮",
            "defaultColSpan": 3, "defaultRowSpan": 2,
            "supportedKinds": ["percentage", "temperature", "numeric", "pressure", "voltage"],
            "labelProps": true, "rangeProps": true,
            "colorProps": ["fillColor"],
            "editableProps": [
                { "key": "orientation", "label": "Orientation", "kind": "enum", "def": "horizontal",
                  "options": ["horizontal", "vertical"] },
                { "key": "warnAbove",   "label": "Warn above",  "kind": "real", "def": null },
                { "key": "showLabel",   "label": "Label",       "kind": "bool", "def": true },
                { "key": "showValue",   "label": "Value",       "kind": "bool", "def": true },
                { "key": "decimals",    "label": "Decimals",    "kind": "int",  "def": 0, "min": 0, "max": 4 }
            ]
        },
        {
            "type": "LinearGauge",
            "url": Qt.resolvedUrl("../gauges/LinearGauge.qml"),
            "displayName": "Linear",
            "glyph": "↔",
            "defaultColSpan": 4, "defaultRowSpan": 2,
            "supportedKinds": ["bidirectional"],
            "labelProps": true, "rangeProps": true,
            "colorProps": ["fillColor"],
            "editableProps": [
                { "key": "showTicks",          "label": "Ticks",           "kind": "bool", "def": true },
                { "key": "decimals",           "label": "Decimals",        "kind": "int",  "def": 0, "min": 0, "max": 4 },
                { "key": "majorTickCount",     "label": "Major ticks",     "kind": "int",  "def": 5, "min": 2, "max": 20 },
                { "key": "minorTicksPerMajor", "label": "Minor per major", "kind": "int",  "def": 4, "min": 0, "max": 10 }
            ]
        },
        {
            "type": "DigitalReadout",
            "url": Qt.resolvedUrl("../gauges/DigitalReadout.qml"),
            "displayName": "Digital",
            "glyph": "88",
            "defaultColSpan": 4, "defaultRowSpan": 2,
            "supportedKinds": ["*"],
            "labelProps": true, "rangeProps": false,
            "colorProps": ["valueColor"],
            "editableProps": [
                { "key": "showTitle",  "label": "Title",       "kind": "bool", "def": true },
                { "key": "showUnit",   "label": "Unit",        "kind": "bool", "def": true },
                { "key": "decimals",   "label": "Decimals",    "kind": "int",  "def": 0,   "min": 0,   "max": 4 },
                { "key": "padDigits",  "label": "Pad digits",  "kind": "int",  "def": 0,   "min": 0,   "max": 8 },
                { "key": "valueScale", "label": "Digit size",  "kind": "real", "def": 3.5, "min": 0.5, "max": 12 }
            ]
        },
        {
            "type": "SparklineGauge",
            "url": Qt.resolvedUrl("../gauges/SparklineGauge.qml"),
            "displayName": "Sparkline",
            "glyph": "∿",
            "defaultColSpan": 6, "defaultRowSpan": 2,
            "supportedKinds": ["*"],
            "labelProps": true, "rangeProps": true,
            "colorProps": ["lineColor"],
            "editableProps": [
                { "key": "autoScale",        "label": "Auto-scale",  "kind": "bool", "def": false },
                { "key": "fillBelow",        "label": "Area fill",   "kind": "bool", "def": true },
                { "key": "showHeader",       "label": "Header",      "kind": "bool", "def": true },
                { "key": "decimals",         "label": "Decimals",    "kind": "int",  "def": 0,   "min": 0,   "max": 4 },
                { "key": "maxSamples",       "label": "Samples",     "kind": "int",  "def": 60,  "min": 10,  "max": 300 },
                { "key": "sampleIntervalMs", "label": "Sample (ms)", "kind": "int",  "def": 500, "min": 100, "max": 5000 }
            ]
        },
        {
            "type": "WarningLight",
            "url": Qt.resolvedUrl("../gauges/WarningLight.qml"),
            "displayName": "Warning",
            "glyph": "⚠",
            "defaultColSpan": 2, "defaultRowSpan": 2,
            "supportedKinds": ["*"],
            "labelProps": false, "rangeProps": false,
            "colorProps": ["activeColor"],
            "editableProps": [
                { "key": "label",        "label": "Light text",    "kind": "string", "def": "" },
                { "key": "triggerAbove", "label": "Trigger above", "kind": "real",   "def": null },
                { "key": "triggerBelow", "label": "Trigger below", "kind": "real",   "def": null },
                { "key": "pulse",        "label": "Pulse",         "kind": "bool",   "def": false }
            ]
        },
        // ── Self-binding widgets (supportedKinds: [] = no PID picker) ──
        {
            "type": "GForceGauge",
            "url": Qt.resolvedUrl("../gauges/GForceGauge.qml"),
            "displayName": "G-Force",
            "glyph": "◎",
            "defaultColSpan": 4, "defaultRowSpan": 4,
            "supportedKinds": [],
            "labelProps": false, "rangeProps": false,
            "colorProps": ["dotColor"],
            "editableProps": [
                { "key": "maxG",      "label": "Max G",   "kind": "real", "def": 1.5, "min": 0.2, "max": 5 },
                { "key": "showTrail", "label": "Trail",   "kind": "bool", "def": true },
                { "key": "showValue", "label": "Readout", "kind": "bool", "def": true }
            ]
        },
        {
            "type": "CompassGauge",
            "url": Qt.resolvedUrl("../gauges/CompassGauge.qml"),
            "displayName": "Compass",
            "glyph": "N",
            "defaultColSpan": 4, "defaultRowSpan": 4,
            "supportedKinds": [],
            "labelProps": false, "rangeProps": false,
            "colorProps": ["northColor"],
            "editableProps": [
                { "key": "showDegrees", "label": "Degrees", "kind": "bool", "def": true }
            ]
        },
        {
            "type": "NowPlayingWidget",
            "url": Qt.resolvedUrl("widgets/NowPlayingWidget.qml"),
            "displayName": "Now Playing",
            "glyph": "♪",
            "defaultColSpan": 6, "defaultRowSpan": 2,
            "supportedKinds": [],
            "labelProps": false, "rangeProps": false,
            "colorProps": ["progressColor"],
            "editableProps": [
                { "key": "showArt",      "label": "Album art", "kind": "bool", "def": true },
                { "key": "showArtist",   "label": "Artist",    "kind": "bool", "def": true },
                { "key": "showProgress", "label": "Progress",  "kind": "bool", "def": true }
            ]
        },
        {
            "type": "MediaControlsWidget",
            "url": Qt.resolvedUrl("widgets/MediaControlsWidget.qml"),
            "displayName": "Media Keys",
            "glyph": "▶",
            "defaultColSpan": 4, "defaultRowSpan": 2,
            "supportedKinds": [],
            "labelProps": false, "rangeProps": false,
            "colorProps": ["accentColor"],
            "editableProps": []
        }
    ]

    // Swatches offered by the properties panel's Color row. Unset (the first,
    // "theme" swatch) keeps the widget on the live EnvironmentTheme colors.
    readonly property var colorSwatches: [
        "#00BFFF", "#2ECC71", "#F1C40F", "#E67E22",
        "#E74C3C", "#FF00AA", "#9C27B0", "#FFFFFF"
    ]

    // type-string → entry, or null if unknown.
    function metaFor(typeName) {
        for (var i = 0; i < widgets.length; i++) {
            if (widgets[i].type === typeName)
                return widgets[i]
        }
        return null
    }

    // type-string → resolved QML url, or "" if unknown.
    function urlFor(typeName) {
        var m = metaFor(typeName)
        return m ? m.url : ""
    }

    // True if `typeName` is a supported primitive (whitelist check).
    function isKnownType(typeName) {
        return metaFor(typeName) !== null
    }
}
