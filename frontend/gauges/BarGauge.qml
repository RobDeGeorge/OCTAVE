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
    property real value: paramId ? (App.OBDParameterModel.liveValue(paramId) || 0) : 0

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
    // Max bar thickness in dp. 0 = fill container (default). Set this for
    // horizontal-mode bars in tall cells so the pill doesn't get chunky —
    // e.g. a 40dp maxThickness keeps bars reading as "bar" not "capsule".
    property real maxThickness: 0

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

    readonly property color _activeFill:
        (!isNaN(warnAbove) && value >= warnAbove) ? warnColor : fillColor

    readonly property bool _horizontal: orientation === "horizontal"

    // Shrink typography in cells smaller than the size the default type was
    // tuned for, so labels never spill out of a small editor cell.
    readonly property real _fontFit: Math.max(0.1, Math.min(1,
        height / App.Spacing.dp(70), width / App.Spacing.dp(140)))

    // ── Layout ──────────────────────────────────────────────────────
    // Horizontal:  [ Title ........ Value Unit ]
    //              [ ===========════════════    ]   ← bar
    //
    // Vertical:    Title
    //              [ |======|  ]   ← bar
    //              Value Unit

    Item {
        anchors.fill: parent

        // Header row (compact, left-aligned) — "Title  Value Unit" next to
        // each other so a wide cell doesn't scatter the text to opposite
        // edges. Always positioned at the top of the BarGauge.
        Row {
            id: header
            visible: root._horizontal && (root.showLabel || root.showValue)
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: parent.right
            height: root.showLabel || root.showValue
                    ? Math.min(Math.max(App.Spacing.overallText, App.Spacing.dp(14)),
                               root.height * 0.4)
                    : 0
            spacing: App.Spacing.dp(6)

            Text {
                id: hTitle
                visible: root.showLabel
                text: root.title
                color: root.labelColor
                font.pixelSize: Math.max(1, App.Spacing.overallText * 0.9 * root._fontFit)
                font.family: App.Style.fontFamily
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
            }
            Text {
                visible: root.showValue
                text: root.value.toFixed(root.decimals) + (root.unit ? " " + root.unit : "")
                color: root.valueColor
                font.pixelSize: Math.max(1, App.Spacing.overallText * 0.95 * root._fontFit)
                font.bold: true
                font.family: App.Style.fontFamily
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Vertical mode: title at top
        Text {
            id: vTitleTop
            visible: !root._horizontal && root.showLabel
            text: root.title
            color: root.labelColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.85 * root._fontFit)
            font.family: App.Style.fontFamily
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            elide: Text.ElideRight
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }

        // Vertical mode: value at bottom
        Text {
            id: vValueBottom
            visible: !root._horizontal && root.showValue
            text: root.value.toFixed(root.decimals) + (root.unit ? " " + root.unit : "")
            color: root.valueColor
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.95 * root._fontFit)
            font.bold: true
            font.family: App.Style.fontFamily
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // The track.
        // Horizontal mode: anchored full-width below header. When
        // maxThickness > 0, the track stays full-height-anchored but the
        // top/bottom margins squeeze it down to the desired pill height
        // and keep it vertically centered in its space.
        // Vertical mode: centered column between title and value; width
        // is capped (either by maxThickness or a default 40%).
        Rectangle {
            id: track
            color: root.trackColor
            radius: root._horizontal ? height / 2 : width / 2

            readonly property real _maxPx: root.maxThickness > 0
                                           ? App.Spacing.dp(root.maxThickness) : 0
            readonly property real _hAvail: Math.max(
                0, parent.height - header.height - App.Spacing.dp(4))
            readonly property real _hExtra: (_maxPx > 0 && _hAvail > _maxPx)
                                            ? (_hAvail - _maxPx) : 0

            anchors.left:  root._horizontal ? parent.left  : undefined
            anchors.right: root._horizontal ? parent.right : undefined
            anchors.horizontalCenter: root._horizontal ? undefined : parent.horizontalCenter

            anchors.top:    root._horizontal
                            ? header.bottom
                            : (vTitleTop.visible ? vTitleTop.bottom : parent.top)
            anchors.bottom: root._horizontal
                            ? parent.bottom
                            : (vValueBottom.visible ? vValueBottom.top : parent.bottom)

            // Horizontal: base top/bottom margins + extra padding to squeeze
            // the pill to `maxThickness` centered in the available space.
            anchors.topMargin:    root._horizontal
                                  ? App.Spacing.dp(4) + _hExtra / 2
                                  : App.Spacing.dp(4)
            anchors.bottomMargin: root._horizontal
                                  ? _hExtra / 2
                                  : App.Spacing.dp(4)

            width: root._horizontal
                   ? parent.width
                   : (_maxPx > 0 ? _maxPx
                                 : Math.max(App.Spacing.dp(10), parent.width * 0.4))

            Rectangle {
                id: fill
                // Hide entirely when value is at (or near) min — otherwise
                // the radius*2 floor that keeps the pill cap intact at mid
                // values would render as a big blob at 0.
                visible: root._animNorm > 0.01
                color: root._activeFill
                radius: parent.radius
                Behavior on color { ColorAnimation { duration: 200 } }

                // Horizontal: grow rightward. Floor at diameter=cap*2 keeps
                // the fill readable (rounded pill) once it's visible.
                width: root._horizontal
                       ? Math.max(track.radius * 2, parent.width * root._animNorm)
                       : parent.width
                // Vertical: grow upward (anchored to bottom)
                height: root._horizontal
                        ? parent.height
                        : Math.max(track.radius * 2, parent.height * root._animNorm)

                anchors.left: root._horizontal ? parent.left : undefined
                anchors.bottom: root._horizontal ? undefined : parent.bottom
                anchors.horizontalCenter: root._horizontal ? undefined : parent.horizontalCenter
                anchors.verticalCenter: root._horizontal ? parent.verticalCenter : undefined
            }
        }
    }
}
