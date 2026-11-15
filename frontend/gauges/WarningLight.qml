// WarningLight.qml
//
// Small binary tell-tale indicator bound to a threshold condition. Lights up
// when the bound PID crosses `triggerAbove` or `triggerBelow`. Composable —
// drop several into a row for an "idiot-light strip" along the bottom of a
// dashboard, or overlay one on top of a larger gauge as a critical alert.
//
// Usage:
//     WarningLight { paramId: "COOLANT_TEMP"; triggerAbove: 110; label: "TEMP" }
//     WarningLight { paramId: "FUEL_LEVEL";   triggerBelow: 12;  label: "FUEL"; activeColor: "#F1C40F" }
//     WarningLight { paramId: "RPM";          triggerAbove: 7000; label: "REV"; pulse: true }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App
import "AlertLevel.js" as Alerts

Item {
    id: root

    readonly property var octaveSupportedKinds: ["*"]

    // ── Data binding ────────────────────────────────────────────────
    property string paramId: ""

    // The PID's row in the gauges' units, null without one
    readonly property var _pInfo: App.OBDParameterModel.displayInfo(paramId)

    property string title: _pInfo ? _pInfo.title : ""
    property string unit: _pInfo ? _pInfo.unit : ""
    property real min: _pInfo ? _pInfo.min : 0
    property real max: _pInfo ? _pInfo.max : 100
    // NaN = no reading yet: the light stays off (a missing fuel level is
    // not a low one)
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)

    // ── Threshold ───────────────────────────────────────────────────
    // Light turns on when value >= triggerAbove OR value <= triggerBelow.
    // Both default NaN = disabled; set whichever direction applies.
    property real triggerAbove: NaN
    property real triggerBelow: NaN
    // Once lit, the light stays lit until the value is back inside the
    // threshold by this much (display unit), so a reading hovering at the
    // threshold does not blink it. NaN = 1 % of the range, at least 1.
    property real alertHysteresis: NaN
    property var _memo: ({ "level": 0, "key": "" })   // see ValueGaugeBase._alertMemo

    // ── Style ───────────────────────────────────────────────────────
    property string label: ""                 // short text shown inside the light
    property color activeColor: App.Style.statusDanger   // theme danger color (red by default)
    property color inactiveColor: Qt.darker(App.Style.obdBoxBackground, 1.3)
    property color labelColor: App.Style.obdLabelColor
    // Dark text on a light lamp (amber, yellow), white on a dark one
    property color activeLabelColor: (0.299 * activeColor.r + 0.587 * activeColor.g + 0.114 * activeColor.b) > 0.6
                                     ? "#141414" : "white"
    property bool pulse: false                // optional attention pulse when lit
    // Max diameter in dp. 0 = fill parent's smaller dim (default). Set this
    // when a WarningLight lives in a big cell and should stay a compact
    // idiot-light rather than a huge disc.
    property real maxSize: 0

    // ── Computed ────────────────────────────────────────────────────
    readonly property bool _active:
        Alerts.track(_memo, value, NaN, NaN, triggerAbove, triggerBelow,
                     Alerts.margin(min, max, 0, alertHysteresis)) === 2

    // ── Light ───────────────────────────────────────────────────────
    Rectangle {
        id: light
        anchors.centerIn: parent
        width: {
            var base = Math.min(parent.width, parent.height)
            if (root.maxSize > 0) return Math.min(base, App.Spacing.dp(root.maxSize))
            return base
        }
        height: width
        radius: width / 2
        color: root._active ? root.activeColor : root.inactiveColor
        border.color: root._active
                      ? Qt.lighter(root.activeColor, 1.35)
                      : Qt.darker(root.inactiveColor, 1.4)
        border.width: Math.max(1, width * 0.05)
        Behavior on color       { ColorAnimation { duration: 160 } }
        Behavior on border.color { ColorAnimation { duration: 160 } }

        // Attention pulse — only drives opacity while lit + enabled.
        // When stopped, opacity falls back to the static binding (1.0).
        opacity: 1.0
        SequentialAnimation on opacity {
            running: root._active && root.pulse && root.visible  // visible includes hidden pages/sections
            loops: Animation.Infinite
            alwaysRunToEnd: false
            NumberAnimation { from: 1.0; to: 0.45; duration: 500; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.45; to: 1.0; duration: 500; easing.type: Easing.InOutSine }
        }

        Text {
            anchors.centerIn: parent
            text: root.label
            color: root._active ? root.activeLabelColor : root.labelColor
            // Fitted inside the disc (a long label used to overflow it)
            width: parent.width * 0.74
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Math.max(1, parent.width * 0.3)
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
            font.bold: true
            font.family: App.Style.fontFamily
            Behavior on color { ColorAnimation { duration: 160 } }
        }
    }
}
