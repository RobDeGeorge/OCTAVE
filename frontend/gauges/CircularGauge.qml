// CircularGauge.qml
//
// Animated circular arc gauge that binds to any OBD parameter ID.
//
// Usage (auto-binds title/unit/min/max from OBDParameterModel):
//     CircularGauge { paramId: "RPM"; showNeedle: true; redlineStart: 6500 }
//
// Usage (manual override):
//     CircularGauge { value: customValue; min: 0; max: 100; title: "Boost"; unit: "psi" }
//
// See docs/GAUGE_AUTHORING.md for the full API and all 93 PID IDs.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    // `octaveSupportedKinds` filters the PID picker; `*` = any PID.
    // `octaveEditableProps` is what shows up in the properties panel
    // when a user selects this widget in the editor.
    readonly property var octaveSupportedKinds: ["*"]
    readonly property var octaveEditableProps: [
        { name: "showNeedle",         type: "bool", label: "Show needle",       default: false },
        { name: "showTicks",          type: "bool", label: "Show ticks",        default: true  },
        { name: "redlineStart",       type: "real", label: "Redline start",     default: NaN   },
        { name: "majorTickCount",     type: "int",  label: "Major ticks",       default: 9,  min: 2, max: 20 },
        { name: "minorTicksPerMajor", type: "int",  label: "Minor per major",   default: 5,  min: 0, max: 10 },
        { name: "decimals",           type: "int",  label: "Decimals",          default: 0,  min: 0, max: 4  },
        { name: "startAngle",         type: "real", label: "Start angle",       default: 135 },
        { name: "sweepAngle",         type: "real", label: "Sweep angle",       default: 270 },
        { name: "showCenterReadout",  type: "bool", label: "Show center text",  default: true  }
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

    // ── Geometry ────────────────────────────────────────────────────
    property real startAngle: 135    // PathAngleArc degrees, 0=3 o'clock CW
    property real sweepAngle: 270    // 270 = 3/4 dial; use 180 for half-dial

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
    property int majorTickCount: 9             // includes both endpoints
    property int minorTicksPerMajor: 5         // minor ticks between each major
    // Tick steps per major segment; 0 minor ticks means major ticks only
    readonly property int _tickSteps: Math.max(1, minorTicksPerMajor)
    property real redlineStart: NaN            // value above which arc turns red
    property bool showCenterReadout: true
    property int decimals: 0

    // ── Computed ────────────────────────────────────────────────────
    readonly property real _normalized: {
        var r = max - min
        if (r <= 0) return 0
        return Math.max(0, Math.min(1, (value - min) / r))
    }

    property real _animNorm: _normalized
    Behavior on _animNorm {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    readonly property real _cx: width / 2
    readonly property real _cy: height / 2
    readonly property real _outerR: Math.min(width, height) / 2 - App.Spacing.dp(4)
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

    // ── Redline zone (static, always shown if set) ──────────────────
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

    // ── Filled value arc ────────────────────────────────────────────
    Shape {
        anchors.fill: parent
        visible: root._animNorm > 0.001
        ShapePath {
            // Turn red once we cross the redline value
            strokeColor: (!isNaN(root.redlineStart) && root.value >= root.redlineStart)
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
            rotation: _angleDeg + 90  // align long axis with radial direction
        }
    }

    // ── Needle ──────────────────────────────────────────────────────
    Rectangle {
        visible: root.showNeedle
        width: App.Spacing.dp(3)
        height: root._arcR * 0.92
        radius: width / 2
        color: root.needleColor
        x: root._cx - width / 2
        y: root._cy - height
        transformOrigin: Item.Bottom
        // PathAngleArc 0 = 3 o'clock; Item rotation 0 = pointing up.
        // Needle at angle a (PathAngleArc) needs item rotation a + 90.
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
        anchors.centerIn: parent
    }

    // ── Center readout ──────────────────────────────────────────────
    Column {
        visible: root.showCenterReadout
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.showNeedle ? -root._arcR * 0.45 : 0
        spacing: App.Spacing.dp(2)

        // Font sizes are capped by the arc radius so the readout scales
        // with the gauge — editor cells can be tiny, and the default
        // App.Spacing sizes would spill outside a small dial.
        Text {
            text: root.title
            color: root.labelColor
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.85,
                                                 root._arcR * 0.22))
            font.family: App.Style.fontFamily
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            width: root._arcR * 1.6
        }
        Text {
            text: root.value.toFixed(root.decimals)
            color: root.valueColor
            font.pixelSize: root.showNeedle
                            ? Math.max(1, Math.min(App.Spacing.overallText * 1.3, root._arcR * 0.3))
                            : Math.max(1, Math.min(App.Spacing.overallText * 1.9, root._arcR * 0.42))
            font.bold: true
            font.family: App.Style.fontFamily
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: root.unit
            color: root.labelColor
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.75,
                                                 root._arcR * 0.18))
            font.family: App.Style.fontFamily
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
