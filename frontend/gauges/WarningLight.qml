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

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: ["*"]
    readonly property var octaveEditableProps: [
        { name: "label",        type: "string", label: "Label text",          default: "" },
        { name: "triggerAbove", type: "real",   label: "Trigger above",       default: NaN },
        { name: "triggerBelow", type: "real",   label: "Trigger below",       default: NaN },
        { name: "pulse",        type: "bool",   label: "Pulse when lit",      default: false },
        { name: "activeColor",  type: "color",  label: "Active color",        default: "#E74C3C" },
        { name: "maxSize",      type: "real",   label: "Max size (dp, 0 = fill)",
          default: 0, min: 0, max: 300 }
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

    // ── Threshold ───────────────────────────────────────────────────
    // Light turns on when value >= triggerAbove OR value <= triggerBelow.
    // Both default NaN = disabled; set whichever direction applies.
    property real triggerAbove: NaN
    property real triggerBelow: NaN

    // ── Style ───────────────────────────────────────────────────────
    property string label: ""                 // short text shown inside the light
    property color activeColor: App.Style.statusDanger   // theme danger color (red by default)
    property color inactiveColor: Qt.darker(App.Style.obdBoxBackground, 1.3)
    property color labelColor: App.Style.obdLabelColor
    property color activeLabelColor: "white"
    property bool pulse: false                // optional attention pulse when lit
    // Max diameter in dp. 0 = fill parent's smaller dim (default). Set this
    // when a WarningLight lives in a big cell and should stay a compact
    // idiot-light rather than a huge disc.
    property real maxSize: 0

    // ── Computed ────────────────────────────────────────────────────
    readonly property bool _active: {
        if (!isNaN(triggerAbove) && value >= triggerAbove) return true
        if (!isNaN(triggerBelow) && value <= triggerBelow) return true
        return false
    }

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
            font.pixelSize: Math.max(App.Spacing.dp(8), parent.width * 0.38)
            font.bold: true
            font.family: App.Style.fontFamily
            Behavior on color { ColorAnimation { duration: 160 } }
        }
    }
}
