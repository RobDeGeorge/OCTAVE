// DigitalReadout.qml
//
// Big numeric value with subdued label and unit. Use for the dominant
// number on a dashboard (e.g. Speed, RPM, Coolant) when an arc is overkill.
//
// Usage:
//     DigitalReadout { paramId: "SPEED"; padDigits: 3 }
//     DigitalReadout { paramId: "RPM"; valueScale: 2.5 }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: ["*"]
    readonly property var octaveEditableProps: [
        { name: "padDigits",  type: "int",  label: "Pad digits",   default: 0,   min: 0, max: 8 },
        { name: "valueScale", type: "real", label: "Value scale",  default: 3.5, min: 0.5, max: 12 },
        { name: "decimals",   type: "int",  label: "Decimals",     default: 0,   min: 0, max: 4 },
        { name: "showTitle",  type: "bool", label: "Show title",   default: true },
        { name: "showUnit",   type: "bool", label: "Show unit",    default: true }
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
    // NaN = no reading yet (shows "--")
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)

    // ── Style ───────────────────────────────────────────────────────
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor
    property color unitColor: App.Style.obdLabelColor

    // ── Features ────────────────────────────────────────────────────
    property int decimals: 0
    property int padDigits: 0       // pad with leading zeros to this width (0 = off)
    property real valueScale: 3.5   // multiplier on App.Spacing.overallText for the big number
    property bool showTitle: true
    property bool showUnit: true
    property int alignment: Qt.AlignHCenter   // Qt.AlignLeft / AlignHCenter / AlignRight

    // ── Computed ────────────────────────────────────────────────────
    // Shrink the whole stack when the cell is shorter than the natural
    // height of title + value + unit at default sizes, so the readout
    // scales with its widget instead of spilling out of a small cell.
    readonly property real _naturalH:
        (showTitle && title.length > 0 ? App.Spacing.overallText * 0.85 : 0)
      + App.Spacing.overallText * valueScale
      + (showUnit && unit.length > 0 ? App.Spacing.overallText * 0.85 : 0)
      + App.Spacing.dp(6)
    readonly property real _vfit: Math.min(1, height / Math.max(1, _naturalH))

    readonly property string _formatted: {
        if (!hasValue) return "--"
        var v = value.toFixed(Math.max(0, Math.min(4, decimals)))
        if (padDigits > 0) {
            var dotIdx = v.indexOf(".")
            var intPart = dotIdx >= 0 ? v.substring(0, dotIdx) : v
            var rest = dotIdx >= 0 ? v.substring(dotIdx) : ""
            var sign = ""
            if (intPart.charAt(0) === "-") { sign = "-"; intPart = intPart.substring(1) }
            while (intPart.length < padDigits) intPart = "0" + intPart
            v = sign + intPart + rest
        }
        return v
    }

    Column {
        width: parent.width
        spacing: App.Spacing.dp(2)
        // _vfit sizes the fonts; this takes up what it misses (line
        // spacing, the gaps) so the unit never falls out of a short cell
        transformOrigin: Item.Top
        scale: Math.max(0.01, Math.min(1, root.height / Math.max(1, implicitHeight)))

        Text {
            visible: root.showTitle && root.title.length > 0
            text: root.title
            color: root.labelColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.85 * root._vfit)
            font.family: App.Style.fontFamily
            width: parent.width
            horizontalAlignment: root.alignment
            elide: Text.ElideRight
        }

        Text {
            text: root._formatted
            color: root.valueColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * root.valueScale * root._vfit)
            font.bold: true
            font.family: App.Style.fontFamily
            width: parent.width
            horizontalAlignment: root.alignment
            // Keep digits from jumping in width as they change
            font.styleName: "Bold"
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
        }

        Text {
            visible: root.showUnit && root.unit.length > 0
            text: root.unit
            color: root.unitColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.85 * root._vfit)
            font.family: App.Style.fontFamily
            width: parent.width
            horizontalAlignment: root.alignment
            elide: Text.ElideRight
        }
    }
}
