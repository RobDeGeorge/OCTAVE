// SparklineGauge.qml
//
// Rolling history line showing recent values. Great for trend-style readouts
// where absolute value matters less than motion (boost, load, fuel trim).
// Timer-sampled so the x-axis scale stays stable regardless of OBD poll rate.
//
// Usage:
//     SparklineGauge { paramId: "ENGINE_LOAD" }
//     SparklineGauge { paramId: "SHORT_FUEL_TRIM_1"; autoScale: true }
//     SparklineGauge { paramId: "INTAKE_PRESSURE"; fillBelow: false }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: ["*"]
    readonly property var octaveEditableProps: [
        { name: "fillBelow",        type: "bool", label: "Fill below line", default: true  },
        { name: "autoScale",        type: "bool", label: "Auto-scale Y",    default: false },
        { name: "showHeader",       type: "bool", label: "Show header",     default: true  },
        { name: "maxSamples",       type: "int",  label: "Max samples",     default: 60,  min: 10, max: 300 },
        { name: "sampleIntervalMs", type: "int",  label: "Sample (ms)",     default: 500, min: 100, max: 5000 },
        { name: "decimals",         type: "int",  label: "Decimals",        default: 0,   min: 0, max: 4 }
    ]

    // ── Data binding ────────────────────────────────────────────────
    property string paramId: ""

    readonly property var _pInfo: {
        if (!paramId) return null
        var all = App.OBDParameterModel.allParameters
        for (var i = 0; i < all.length; i++) {
            if (all[i].id === paramId) return all[i]
        }
        return null
    }

    property string title: _pInfo ? _pInfo.title : ""
    property string unit: _pInfo ? _pInfo.unit : ""
    property real min: _pInfo ? _pInfo.min : 0
    property real max: _pInfo ? _pInfo.max : 100
    property real value: paramId ? (App.OBDParameterModel.paramValues[paramId] || 0) : 0
    property int decimals: 0

    // ── Style ───────────────────────────────────────────────────────
    property color lineColor: App.Style.obdBarColor
    // Follows lineColor so a single color override recolors line + area.
    property color fillColor: Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.22)
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor
    property color backgroundColor: Qt.darker(App.Style.obdBoxBackground, 1.1)

    // ── Features ────────────────────────────────────────────────────
    // Default: 60 samples × 500ms = 30s rolling window.
    property int maxSamples: 60
    property int sampleIntervalMs: 500
    property bool fillBelow: true
    property bool showHeader: true
    // If true, y-axis rescales to the visible window instead of using min/max.
    // Good for small-swing values like fuel trim where the full PID range is
    // much larger than anything you ever actually see.
    property bool autoScale: false

    // ── Sample buffer ───────────────────────────────────────────────
    property var _samples: []

    Timer {
        interval: root.sampleIntervalMs
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var s = root._samples.slice()
            s.push(root.value)
            while (s.length > root.maxSamples) s.shift()
            root._samples = s
        }
    }

    // ── Layout math ─────────────────────────────────────────────────
    // Shrink typography in cells smaller than the size the default type was
    // tuned for, so labels never spill out of a small editor cell.
    readonly property real _fontFit: Math.max(0.1, Math.min(1,
        height / App.Spacing.dp(70), width / App.Spacing.dp(160)))

    readonly property real _headerH:
        showHeader ? Math.min(Math.max(App.Spacing.overallText, App.Spacing.dp(14)),
                              height * 0.4)
                   : 0
    readonly property real _plotTop: _headerH + (showHeader ? App.Spacing.dp(4) : 0)

    readonly property real _rangeMin: {
        if (autoScale && _samples.length > 0) return Math.min.apply(null, _samples)
        return min
    }
    readonly property real _rangeMax: {
        if (autoScale && _samples.length > 0) return Math.max.apply(null, _samples)
        return max
    }
    readonly property real _rangeSpan: Math.max(0.0001, _rangeMax - _rangeMin)

    // ── Header (compact, left-aligned) ──────────────────────────────
    // "Title  Value Unit" adjacent — a wide cell doesn't scatter the text
    // to opposite edges the way edge-anchored layouts would.
    Row {
        id: header
        visible: root.showHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: App.Spacing.dp(4)
        height: root._headerH
        spacing: App.Spacing.dp(6)

        Text {
            text: root.title
            color: root.labelColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.9 * root._fontFit)
            font.family: App.Style.fontFamily
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
        Text {
            text: root.value.toFixed(root.decimals) + (root.unit ? " " + root.unit : "")
            color: root.valueColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.95 * root._fontFit)
            font.bold: true
            font.family: App.Style.fontFamily
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ── Plot area: background + canvas line ─────────────────────────
    // Small left/right/bottom inset so the trace doesn't ride the cell
    // edges, and top gap below the header.
    Rectangle {
        id: plotBackground
        anchors.top: parent.top
        anchors.topMargin: root._plotTop
        anchors.left: parent.left
        anchors.leftMargin: App.Spacing.dp(4)
        anchors.right: parent.right
        anchors.rightMargin: App.Spacing.dp(4)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: App.Spacing.dp(4)
        color: root.backgroundColor
        radius: App.Spacing.dpMin(6, 2)

        Canvas {
            id: plot
            anchors.fill: parent

            property var samples: root._samples
            property real rangeMin: root._rangeMin
            property real rangeSpan: root._rangeSpan
            property int maxSamples: root.maxSamples
            property color lineColor: root.lineColor
            property color fillColor: root.fillColor
            property bool fillBelow: root.fillBelow

            onSamplesChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()

                var n = samples.length
                if (n < 2) return

                var w = width
                var h = height
                var inset = Math.max(App.Spacing.dp(3), h * 0.08)
                var xStep = w / Math.max(1, maxSamples - 1)
                // Right-align: newest sample sits flush with the right edge
                var xStart = w - (n - 1) * xStep

                function mapY(v) {
                    var t = (v - rangeMin) / rangeSpan
                    t = Math.max(0, Math.min(1, t))
                    return h - inset - t * (h - 2 * inset)
                }

                if (fillBelow) {
                    ctx.beginPath()
                    ctx.moveTo(xStart, h)
                    for (var i = 0; i < n; i++) {
                        ctx.lineTo(xStart + i * xStep, mapY(samples[i]))
                    }
                    ctx.lineTo(xStart + (n - 1) * xStep, h)
                    ctx.closePath()
                    ctx.fillStyle = fillColor.toString()
                    ctx.fill()
                }

                ctx.beginPath()
                ctx.moveTo(xStart, mapY(samples[0]))
                for (var j = 1; j < n; j++) {
                    ctx.lineTo(xStart + j * xStep, mapY(samples[j]))
                }
                ctx.lineWidth = App.Spacing.dp(2)
                ctx.strokeStyle = lineColor.toString()
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                ctx.stroke()
            }
        }
    }
}
