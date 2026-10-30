// ArcGauge.qml
//
// Half-dial gauge ("n" shape — top semicircle with readout below the chord).
// Compact, wide-aspect alternative to CircularGauge for speedometer-style
// layouts.
//
// Usage:
//     ArcGauge { paramId: "SPEED"; showNeedle: true }
//     ArcGauge { paramId: "COOLANT_TEMP"; redlineStart: 105 }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import ".." as App
import "AlertLevel.js" as Alerts

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: ["*"]
    readonly property var octaveEditableProps: [
        { name: "showNeedle",         type: "bool", label: "Show needle",      default: false },
        { name: "showTicks",          type: "bool", label: "Show ticks",       default: true  },
        { name: "redlineStart",       type: "real", label: "Redline start",    default: NaN   },
        { name: "majorTickCount",     type: "int",  label: "Major ticks",      default: 5,  min: 2, max: 20 },
        { name: "minorTicksPerMajor", type: "int",  label: "Minor per major",  default: 4,  min: 0, max: 10 },
        { name: "decimals",           type: "int",  label: "Decimals",         default: 0,  min: 0, max: 4  },
        { name: "startAngle",         type: "real", label: "Start angle",      default: 180 },
        { name: "sweepAngle",         type: "real", label: "Sweep angle",      default: 180 },
        { name: "showReadout",        type: "bool", label: "Show readout",     default: true  }
    ]

    // ── Data binding ────────────────────────────────────────────────
    property string paramId: ""

    readonly property var _pInfo: {
        if (!paramId) return null
        var all = App.OBDParameterModel.displayParameters   // in the gauges' units
        for (var i = 0; i < all.length; i++) {
            if (all[i].id === paramId) return all[i]
        }
        return null
    }

    property string title: _pInfo ? _pInfo.title : ""
    property string unit: _pInfo ? _pInfo.unit : ""
    property real min: _pInfo ? _pInfo.min : 0
    property real max: _pInfo ? _pInfo.max : 100
    // NaN = no reading yet: "--", no fill or needle, no redline colour
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)

    // ── Alerts (gauges/AlertLevel.js) ───────────────────────────────
    // Limits in the display unit, NaN = unset. Level 1 colours the value
    // warnColor, level 2 critColor; alertFlash blinks it while critical.
    property real warnAbove: NaN
    property real warnBelow: NaN
    property real critAbove: NaN
    property real critBelow: NaN
    property bool alertFlash: false
    property color warnColor: App.Style.statusWarning
    property color critColor: App.Style.statusDanger
    readonly property int alertLevel: Alerts.level(value, warnAbove, warnBelow, critAbove, critBelow)
    readonly property color _alertColor: alertLevel === 2 ? critColor : warnColor
    property bool _flashOn: true
    readonly property real _alertOpacity: (alertFlash && alertLevel === 2 && !_flashOn) ? 0.3 : 1.0
    Timer {
        running: root.alertFlash && root.alertLevel === 2
        interval: 400
        repeat: true
        onTriggered: root._flashOn = !root._flashOn
        onRunningChanged: if (!running) root._flashOn = true
    }
    readonly property string _formatted: hasValue ? value.toFixed(Math.max(0, Math.min(4, decimals))) : "--"

    // ── Geometry (top-arc "n" shape by default) ─────────────────────
    // PathAngleArc: 0° = 3 o'clock, positive sweep = CW.
    // 180° start + 180° CW sweep = 9 o'clock → 12 o'clock → 3 o'clock.
    property real startAngle: 180
    property real sweepAngle: 180

    // ── Style ───────────────────────────────────────────────────────
    property color trackColor: Qt.darker(App.Style.obdBoxBackground, 1.15)
    property color fillColor: App.Style.obdBarColor
    property color redlineColor: App.Style.statusDanger
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor
    property color needleColor: App.Style.obdBarColor

    // ── Features ────────────────────────────────────────────────────
    property bool showNeedle: false
    property bool showTicks: true
    property int majorTickCount: 5
    property int minorTicksPerMajor: 4
    // Tick steps per major segment; 0 minor ticks means major ticks only
    readonly property int _tickSteps: Math.max(1, minorTicksPerMajor)
    property real redlineStart: NaN
    property bool showReadout: true
    property int decimals: 0

    // ── Computed ────────────────────────────────────────────────────
    readonly property real _normalized: {
        var r = max - min
        if (!hasValue || !(r > 0)) return 0
        return Math.max(0, Math.min(1, (value - min) / r))
    }

    property real _animNorm: _normalized
    Behavior on _animNorm {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    // Arc center sits near the top of the item so the lower half is free
    // for the readout. Radius is capped by half-width OR 60% of height,
    // whichever is smaller — keeps the arc round in any aspect ratio.
    readonly property real _outerR: Math.max(
        App.Spacing.dp(10),
        Math.min(width / 2, height * 0.6) - App.Spacing.dp(4)
    )
    readonly property real _cx: width / 2
    readonly property real _cy: _outerR + App.Spacing.dp(8)
    readonly property real _arcWidth: Math.max(App.Spacing.dp(6), _outerR * 0.12)
    readonly property real _arcR: _outerR - _arcWidth / 2

    // ── Background arc ──────────────────────────────────────────────
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeColor: root.trackColor
            strokeWidth: root._arcWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root._cx
                centerY: root._cy
                radiusX: root._arcR
                radiusY: root._arcR
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle
            }
        }
    }

    // ── Redline zone ────────────────────────────────────────────────
    Shape {
        anchors.fill: parent
        visible: !isNaN(root.redlineStart) && root.redlineStart < root.max && root.redlineStart > root.min
        ShapePath {
            strokeColor: root.redlineColor
            strokeWidth: root._arcWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                property real _t: (root.redlineStart - root.min) / (root.max - root.min)
                centerX: root._cx
                centerY: root._cy
                radiusX: root._arcR
                radiusY: root._arcR
                startAngle: root.startAngle + root.sweepAngle * _t
                sweepAngle: root.sweepAngle * (1 - _t)
            }
        }
    }

    // ── Warning / critical bands (static, thin, on the inner edge) ──
    readonly property var _bands: Alerts.bands(min, max, warnAbove, warnBelow, critAbove, critBelow)
    readonly property real _bandW: Math.max(App.Spacing.dp(2), _arcWidth * 0.3)
    Repeater {
        model: root._bands
        delegate: Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: modelData.level === 2 ? root.critColor : root.warnColor
                strokeWidth: root._bandW
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                PathAngleArc {
                    centerX: root._cx
                    centerY: root._cy
                    radiusX: root._arcR - root._arcWidth / 2 - root._bandW / 2 - App.Spacing.dp(1)
                    radiusY: radiusX
                    startAngle: root.startAngle + root.sweepAngle * modelData.from
                    sweepAngle: root.sweepAngle * (modelData.to - modelData.from)
                }
            }
        }
    }

    // ── Filled value arc ────────────────────────────────────────────
    Shape {
        anchors.fill: parent
        visible: root._animNorm > 0.001
        ShapePath {
            strokeColor: root.alertLevel > 0 ? root._alertColor
                         : (root.hasValue && !isNaN(root.redlineStart) && root.value >= root.redlineStart)
                           ? root.redlineColor : root.fillColor
            strokeWidth: root._arcWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root._cx
                centerY: root._cy
                radiusX: root._arcR
                radiusY: root._arcR
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle * root._animNorm
            }
        }
    }

    // ── Tick marks ──────────────────────────────────────────────────
    Repeater {
        model: root.showTicks ? (Math.max(1, root.majorTickCount - 1) * root._tickSteps + 1) : 0
        delegate: Rectangle {
            property bool _major: (index % root._tickSteps) === 0
            property real _t: index / (Math.max(1, root.majorTickCount - 1) * root._tickSteps)
            property real _angleDeg: root.startAngle + root.sweepAngle * _t
            property real _angleRad: _angleDeg * Math.PI / 180
            property real _tickR: root._arcR + root._arcWidth / 2 + App.Spacing.dp(4)
            width: _major ? App.Spacing.dp(3) : App.Spacing.dp(1)
            height: _major ? root._arcWidth * 0.7 : root._arcWidth * 0.4
            radius: width / 2
            color: root.labelColor
            opacity: _major ? 0.85 : 0.45
            x: root._cx + _tickR * Math.cos(_angleRad) - width / 2
            y: root._cy + _tickR * Math.sin(_angleRad) - height / 2
            transformOrigin: Item.Center
            rotation: _angleDeg + 90
        }
    }

    // ── Needle ──────────────────────────────────────────────────────
    Rectangle {
        visible: root.showNeedle && root.hasValue
        width: App.Spacing.dp(3)
        height: root._arcR * 0.92
        radius: width / 2
        color: root.needleColor
        x: root._cx - width / 2
        y: root._cy - height
        transformOrigin: Item.Bottom
        rotation: (root.startAngle + root.sweepAngle * root._animNorm) + 90
    }

    Rectangle {
        visible: root.showNeedle
        width: Math.max(App.Spacing.dp(10), root._arcR * 0.16)
        height: width
        radius: width / 2
        color: Qt.darker(App.Style.obdBoxBackground, 1.25)
        border.color: root.needleColor
        border.width: App.Spacing.dp(2)
        x: root._cx - width / 2
        y: root._cy - height / 2
    }

    // ── Readout (below the arc, in the lower portion of the item) ──
    Column {
        id: readout
        visible: root.showReadout
        anchors.horizontalCenter: parent.horizontalCenter
        y: root._cy + App.Spacing.dp(6)
        spacing: App.Spacing.dp(2)
        width: root._outerR * 1.6
        // Measured fit: shrink the whole block into the space under the arc
        // (font sizes alone leave line spacing out and spill in short cells)
        transformOrigin: Item.Top
        scale: Math.max(0.01, Math.min(1, (root.height - y) / Math.max(1, implicitHeight)))

        // Font sizes capped by the arc radius so the readout scales with
        // the gauge instead of spilling out of a small editor cell.
        Text {
            text: root.title
            color: root.labelColor
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.85,
                                                 root._outerR * 0.2))
            font.family: App.Style.fontFamily
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            width: parent.width
        }
        Text {
            text: root._formatted
            color: root.alertLevel > 0 ? root._alertColor : root.valueColor
            opacity: root._alertOpacity
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 1.6,
                                                 root._outerR * 0.36))
            font.bold: true
            font.family: App.Style.fontFamily
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
        }
        Text {
            text: root.unit
            color: root.labelColor
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.75,
                                                 root._outerR * 0.16))
            font.family: App.Style.fontFamily
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            visible: root.unit !== ""
        }
    }
}
