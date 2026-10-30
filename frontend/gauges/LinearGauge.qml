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
    // NaN = no reading yet: "--", no fill or marker
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)

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
        if (!hasValue || !(r > 0)) return 0
        return Math.max(0, Math.min(1, (value - min) / r))
    }
    property real _animNorm: _normalized
    Behavior on _animNorm {
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }

    readonly property real _zeroT: (min < 0 && max > 0) ? (-min) / (max - min) : 0
    readonly property string _formatted: hasValue ? value.toFixed(Math.max(0, Math.min(4, decimals))) : "--"

    // Explicit vertical budget: header, marker, track, ticks — each sized
    // from what the cell has, so a short cell shrinks them instead of
    // pushing the ticks or the marker outside it
    readonly property real _gap: Math.min(App.Spacing.dp(4), height * 0.04)
    readonly property real _headerH: Math.max(1, Math.min(
        Math.max(App.Spacing.overallText, App.Spacing.dp(14)) * 1.25, height * 0.4))
    readonly property real _bodyH: Math.max(0, height - _headerH - _gap)
    readonly property real _markerH: Math.min(App.Spacing.dp(10), _bodyH * 0.25)
    readonly property real _tickH: showTicks ? Math.min(App.Spacing.dp(8), _bodyH * 0.22) : 0
    readonly property real _trackH: Math.max(1, Math.min(App.Spacing.dp(14),
        _bodyH - _markerH - _tickH - 2 * _gap))
    readonly property real _fontPx: Math.max(1, _headerH * 0.72)

    // Header: the value keeps its width, the title gets the rest
    Item {
        width: parent.width
        height: root._headerH

        Text {
            id: valueText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width)
            text: root._formatted + (root.unit ? " " + root.unit : "")
            color: root.valueColor
            font.pixelSize: root._fontPx
            font.bold: true
            font.family: App.Style.fontFamily
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
            horizontalAlignment: Text.AlignRight
        }
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - valueText.width - App.Spacing.dp(6))
            text: root.title
            color: root.labelColor
            font.pixelSize: root._fontPx * 0.92
            font.family: App.Style.fontFamily
            elide: Text.ElideRight
        }
    }

    Item {
        id: trackArea
        y: root._headerH + root._gap
        width: parent.width
        height: root._bodyH

        // Value marker above the track, kept inside the cell at the ends
        Rectangle {
            visible: root.hasValue && root._markerH >= 2
            width: root._markerH
            height: root._markerH
            radius: width / 2
            color: root.fillColor
            border.color: Qt.darker(root.fillColor, 1.4)
            border.width: 1
            x: Math.max(0, Math.min(trackArea.width - width, trackArea.width * root._animNorm - width / 2))
            y: track.y - root._gap - height
        }

        Rectangle {
            id: track
            y: root._markerH + root._gap
            width: parent.width
            height: root._trackH
            color: root.trackColor
            radius: height / 2

            Rectangle {
                id: fill
                visible: root.hasValue
                color: root.fillColor
                radius: parent.radius
                height: parent.height
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

            // Centre origin marker (bidirectional)
            Rectangle {
                visible: root.centerOriginAtZero
                width: App.Spacing.dp(2)
                height: parent.height
                color: root.labelColor
                opacity: 0.5
                x: parent.width * root._zeroT - width / 2
            }
        }

        // Tick marks below the track
        Repeater {
            model: root.showTicks && root._tickH >= 2
                   ? (Math.max(1, root.majorTickCount - 1) * root._tickSteps + 1) : 0
            delegate: Rectangle {
                property bool _major: (index % root._tickSteps) === 0
                property real _t: index / (Math.max(1, root.majorTickCount - 1) * root._tickSteps)
                width: _major ? App.Spacing.dp(2) : App.Spacing.dp(1)
                height: _major ? root._tickH : root._tickH * 0.6
                color: root.labelColor
                opacity: _major ? 0.7 : 0.4
                x: Math.max(0, Math.min(trackArea.width - width, trackArea.width * _t - width / 2))
                y: track.y + track.height + root._gap
            }
        }
    }
}
