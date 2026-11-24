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

ValueGaugeBase {
    id: root

    readonly property var octaveSupportedKinds: ["*"]
    // paramId, title, unit, min, max, value, hasValue, decimals and the alert
    // properties (warnAbove ... alertLevel) are ValueGaugeBase's

    // ── Style ───────────────────────────────────────────────────────
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor
    property color unitColor: App.Style.obdLabelColor

    // ── Features ────────────────────────────────────────────────────
    property int padDigits: 0       // pad with leading zeros to this width (0 = off)
    property real valueScale: 3.5   // multiplier on App.Spacing.overallText for the big number
    property bool showTitle: true
    property bool showUnit: true
    property int alignment: Qt.AlignHCenter   // Qt.AlignLeft / AlignHCenter / AlignRight
    // The Home screen's reading card: centred in its cell, the decimals the
    // reading has by itself (90.0 °C, 14.1 V) and a bar along the bottom for
    // where the value stands in its range. With the cell's `frame`, the box.
    property bool card: false
    property color barColor: App.Style.obdBarColor
    readonly property int _decimals: (card && paramId !== "") ? App.OBDParameterModel.decimalsFor(paramId) : decimals
    readonly property real _barHeight: card ? Math.max(2, Math.min(App.Spacing.dp(4), height * 0.06)) : 0

    // ── Computed ────────────────────────────────────────────────────
    // Shrink the whole stack when the cell is shorter than the natural
    // height of title + value + unit at default sizes, so the readout
    // scales with its widget instead of spilling out of a small cell.
    readonly property real _naturalH:
        (showTitle && title.length > 0 ? App.Spacing.overallText * 0.85 : 0)
      + App.Spacing.overallText * valueScale
      + (showUnit && unit.length > 0 ? App.Spacing.overallText * 0.85 : 0)
      + App.Spacing.dp(6)
    readonly property real _vfit: Math.min(1, (height - _barHeight) / Math.max(1, _naturalH))

    readonly property string _formatted: {
        if (!hasValue) return "--"
        var v = value.toFixed(Math.max(0, Math.min(4, _decimals)))
        if (Number(v) === 0 && v.charAt(0) === "-") v = v.substring(1)   // never "-0"
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
        scale: Math.max(0.01, Math.min(1, (root.height - root._barHeight) / Math.max(1, implicitHeight)))
        y: root.card ? Math.max(0, (root.height - root._barHeight - implicitHeight * scale) / 2) : 0

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
            color: root.alertLevel > 0 ? root._alertColor : root.valueColor
            opacity: root._alertOpacity
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

    // Card style: where the value stands in its range
    Rectangle {
        visible: root.card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root._barHeight
        radius: height / 2
        color: Qt.darker(App.Style.obdBoxBackground, 1.35)

        Rectangle {
            visible: root.hasValue && root.max > root.min
            width: Math.max(parent.height, parent.width * Math.max(0, Math.min(1, (root.value - root.min) / (root.max - root.min))))
            height: parent.height
            radius: parent.radius
            color: root.alertLevel > 0 ? root._alertColor : root.barColor
            Behavior on width { NumberAnimation { duration: 200 } }
        }
    }
}
