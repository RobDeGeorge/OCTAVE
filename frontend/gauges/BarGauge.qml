// BarGauge.qml
//
// Animated filled bar that binds to any OBD parameter ID. Horizontal or vertical.
//
// Usage:
//     BarGauge { paramId: "FUEL_LEVEL"; orientation: "horizontal" }
//     BarGauge { paramId: "COOLANT_TEMP"; orientation: "vertical"; warnAbove: 105 }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    // Bidirectional PIDs go to LinearGauge, not here — BarGauge grows from
    // one end and doesn't handle signed ranges well.
    readonly property var octaveSupportedKinds: ["percentage", "temperature", "numeric", "pressure", "voltage"]
    readonly property var octaveEditableProps: [
        { name: "orientation",  type: "enum", label: "Orientation",  default: "horizontal",
          options: ["horizontal", "vertical"] },
        { name: "warnAbove",    type: "real", label: "Warn above",   default: NaN },
        { name: "showLabel",    type: "bool", label: "Show label",   default: true },
        { name: "showValue",    type: "bool", label: "Show value",   default: true },
        { name: "decimals",     type: "int",  label: "Decimals",     default: 0, min: 0, max: 4 },
        { name: "maxThickness", type: "real", label: "Max thickness (dp, 0 = fill)",
          default: 0, min: 0, max: 200 }
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
    // NaN = no reading yet: "--", an empty track, no warning colour
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)

    // ── Style ───────────────────────────────────────────────────────
    property string orientation: "horizontal"  // "horizontal" or "vertical"
    property color trackColor: Qt.darker(App.Style.obdBoxBackground, 1.15)
    property color fillColor: App.Style.obdBarColor
    property color warnColor: App.Style.statusDanger
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor
    property real warnAbove: NaN   // value at/above which the bar turns warnColor
    property int decimals: 0
    property bool showLabel: true
    property bool showValue: true
    // Max bar thickness in dp. 0 = fill the space (default). Caps the
    // height of a horizontal bar / the width of a vertical one.
    property real maxThickness: 0

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

    readonly property bool _warning: hasValue && !isNaN(warnAbove) && value >= warnAbove
    readonly property color _activeFill: _warning ? warnColor : fillColor
    readonly property string _formatted: hasValue ? value.toFixed(Math.max(0, Math.min(4, decimals))) : "--"
    readonly property string _valueText: _formatted + (unit ? " " + unit : "")

    readonly property bool _horizontal: orientation !== "vertical"
    readonly property real _gap: Math.min(App.Spacing.dp(4), height * 0.05)
    readonly property real _maxPx: maxThickness > 0 ? App.Spacing.dp(maxThickness) : 0

    // Explicit geometry per orientation (no anchors that change with it, so
    // switching orientation after creation lays out like creating it so).
    //
    // Horizontal:  [ Title ........ Value Unit ]
    //              [ ███████████░░░░░░░░░░░░░░ ]   ← track
    // Vertical:    Title / track (bottom-up) / Value Unit
    readonly property real _lineH: Math.max(1, Math.min(
        Math.max(App.Spacing.overallText, App.Spacing.dp(14)) * 1.25,
        height * (_horizontal ? 0.4 : 0.22)))
    readonly property real _topTextH: (_horizontal ? (showLabel || showValue) : showLabel) ? _lineH : 0
    readonly property real _bottomTextH: (!_horizontal && showValue) ? _lineH : 0
    readonly property real _fontPx: Math.max(1, _lineH * 0.72)

    readonly property rect _trackRect: {
        var top = _topTextH > 0 ? _topTextH + _gap : 0
        var bottom = _bottomTextH > 0 ? _bottomTextH + _gap : 0
        var availH = Math.max(0, height - top - bottom)
        if (_horizontal) {
            var h = _maxPx > 0 ? Math.min(_maxPx, availH) : availH
            return Qt.rect(0, top + (availH - h) / 2, width, h)
        }
        var w = _maxPx > 0 ? Math.min(_maxPx, width)
                           : Math.min(width, Math.max(App.Spacing.dp(10), width * 0.4))
        return Qt.rect((width - w) / 2, top, w, availH)
    }

    // Title + value: the value keeps its width, the title gets the rest
    Item {
        id: header
        visible: root._topTextH > 0
        width: parent.width
        height: root._topTextH

        Text {
            id: valueText
            visible: root._horizontal && root.showValue
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width)
            text: root._valueText
            color: root._warning ? root.warnColor : root.valueColor
            font.pixelSize: root._fontPx
            font.bold: true
            font.family: App.Style.fontFamily
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
            horizontalAlignment: Text.AlignRight
        }
        Text {
            visible: root.showLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - (valueText.visible ? valueText.width + App.Spacing.dp(6) : 0))
            text: root.title
            color: root.labelColor
            font.pixelSize: root._fontPx * 0.92
            font.family: App.Style.fontFamily
            elide: Text.ElideRight
            horizontalAlignment: root._horizontal ? Text.AlignLeft : Text.AlignHCenter
        }
    }

    Text {
        objectName: "barValueBottom"
        visible: root._bottomTextH > 0
        y: parent.height - root._bottomTextH
        width: parent.width
        height: root._bottomTextH
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        text: root._valueText
        color: root._warning ? root.warnColor : root.valueColor
        font.pixelSize: root._fontPx
        font.bold: true
        font.family: App.Style.fontFamily
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 1
    }

    Rectangle {
        id: track
        objectName: "barTrack"
        x: root._trackRect.x
        y: root._trackRect.y
        width: root._trackRect.width
        height: root._trackRect.height
        color: root.trackColor
        radius: Math.min(width, height) / 2

        // The fill is the track's own shape revealed by a clip that grows
        // from the start: exactly proportional (no minimum blob at small
        // values) and always inside the track.
        Item {
            id: fillClip
            objectName: "barFill"
            clip: true
            visible: root._animNorm > 0
            x: 0
            width: root._horizontal ? track.width * root._animNorm : track.width
            height: root._horizontal ? track.height : track.height * root._animNorm
            y: track.height - height

            Rectangle {
                y: fillClip.height - track.height
                width: track.width
                height: track.height
                radius: track.radius
                color: root._activeFill
                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }
    }
}
