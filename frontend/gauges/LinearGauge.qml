// LinearGauge.qml
//
// Horizontal scale with tick marks and a sliding indicator. Good for ranges
// that include negative values (fuel trim, EGR error, timing advance).
//
// Usage:
//     LinearGauge { paramId: "SHORT_FUEL_TRIM_1" }   // -25..25 with center 0
//     LinearGauge { paramId: "TIMING_ADVANCE"; majorTickCount: 5 }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    // LinearGauge is specifically for signed ranges; it renders the fill
    // outward from zero. Unidirectional PIDs should use BarGauge.
    readonly property var octaveSupportedKinds: ["bidirectional"]
    readonly property var octaveEditableProps: [
        { name: "majorTickCount",     type: "int",  label: "Major ticks",     default: 5, min: 2, max: 20 },
        { name: "minorTicksPerMajor", type: "int",  label: "Minor per major", default: 4, min: 0, max: 10 },
        { name: "showTicks",          type: "bool", label: "Show ticks",      default: true },
        { name: "decimals",           type: "int",  label: "Decimals",        default: 0, min: 0, max: 4 }
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

    // ── Style ───────────────────────────────────────────────────────
    property color trackColor: Qt.darker(App.Style.obdBoxBackground, 1.15)
    property color fillColor: App.Style.obdBarColor
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    // ── Features ────────────────────────────────────────────────────
    property int majorTickCount: 5             // includes both endpoints
    property int minorTicksPerMajor: 4
    // Tick steps per major segment; 0 minor ticks means major ticks only
    readonly property int _tickSteps: Math.max(1, minorTicksPerMajor)
    property bool showTicks: true
    property bool centerOriginAtZero: min < 0 && max > 0   // bidirectional fill from 0
    property int decimals: 0

    // ── Computed ────────────────────────────────────────────────────
    readonly property real _normalized: {
        var r = max - min
        if (r <= 0) return 0
        return Math.max(0, Math.min(1, (value - min) / r))
    }
    property real _animNorm: _normalized
    Behavior on _animNorm {
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }

    readonly property real _zeroT: (min < 0 && max > 0) ? (-min) / (max - min) : 0

    // Shrink typography in cells smaller than the size the default type was
    // tuned for, so labels never spill out of a small editor cell.
    readonly property real _fontFit: Math.max(0.1, Math.min(1,
        height / App.Spacing.dp(80), width / App.Spacing.dp(160)))

    Column {
        anchors.fill: parent
        spacing: App.Spacing.dp(4)

        // Header
        Item {
            width: parent.width
            height: Math.min(Math.max(App.Spacing.overallText, App.Spacing.dp(14)),
                             root.height * 0.35)

            Text {
                text: root.title
                color: root.labelColor
                font.pixelSize: Math.max(1, App.Spacing.overallText * 0.9 * root._fontFit)
                font.family: App.Style.fontFamily
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                width: parent.width * 0.55
            }
            Text {
                text: root.value.toFixed(root.decimals) + (root.unit ? " " + root.unit : "")
                color: root.valueColor
                font.pixelSize: Math.max(1, App.Spacing.overallText * 0.95 * root._fontFit)
                font.bold: true
                font.family: App.Style.fontFamily
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Track
        Item {
            id: trackArea
            width: parent.width
            height: Math.max(App.Spacing.dp(28), parent.height * 0.45)

            Rectangle {
                id: track
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: Math.max(App.Spacing.dp(6), parent.height * 0.32)
                color: root.trackColor
                radius: height / 2

                Rectangle {
                    id: fill
                    color: root.fillColor
                    radius: parent.radius
                    height: parent.height
                    Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    x: {
                        if (!root.centerOriginAtZero) return 0
                        var zeroX = parent.width * root._zeroT
                        return root.value >= 0 ? zeroX : parent.width * root._animNorm
                    }
                    width: {
                        if (!root.centerOriginAtZero) return parent.width * root._animNorm
                        var zeroX = parent.width * root._zeroT
                        var nowX = parent.width * root._animNorm
                        return Math.max(2, Math.abs(nowX - zeroX))
                    }
                }

                // Center origin marker (when bidirectional)
                Rectangle {
                    visible: root.centerOriginAtZero
                    width: App.Spacing.dp(2)
                    height: parent.height + App.Spacing.dp(6)
                    color: root.labelColor
                    opacity: 0.5
                    x: parent.width * root._zeroT - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Tick marks below the track
            Repeater {
                model: root.showTicks ? (Math.max(1, root.majorTickCount - 1) * root._tickSteps + 1) : 0
                delegate: Rectangle {
                    property bool _major: (index % root._tickSteps) === 0
                    property real _t: index / (Math.max(1, root.majorTickCount - 1) * root._tickSteps)
                    width: _major ? App.Spacing.dp(2) : App.Spacing.dp(1)
                    height: _major ? App.Spacing.dp(8) : App.Spacing.dp(5)
                    color: root.labelColor
                    opacity: _major ? 0.7 : 0.4
                    x: trackArea.width * _t - width / 2
                    anchors.top: track.bottom
                    anchors.topMargin: App.Spacing.dp(2)
                }
            }

            // Indicator (small arrow above the track at the value position)
            Rectangle {
                width: App.Spacing.dp(10)
                height: App.Spacing.dp(10)
                radius: width / 2
                color: root.fillColor
                border.color: Qt.darker(root.fillColor, 1.4)
                border.width: 1
                x: trackArea.width * root._animNorm - width / 2
                anchors.bottom: track.top
                anchors.bottomMargin: App.Spacing.dp(2)
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
}
