// CompassGauge.qml
//
// Rotating compass card bound to the BerryIMU magnetometer heading. A fixed
// lubber line points up; the card (cardinal letters + tick ring) rotates so
// the current heading sits under it. Optional numeric degrees readout in the
// center. This is the CompassGauge the dashboards roadmap deferred until
// heading data existed — HEADING is now a model parameter.
//
// Self-binding: reads HEADING from OBDParameterModel (fed by the berryIMU
// manager) — no paramId, so the editor hides the PID picker for it
// (supportedKinds: []).
//
// Usage:
//     CompassGauge { showDegrees: true }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Data binding ────────────────────────────────────────────────
    // NaN until the magnetometer reports: the card stays put and the
    // readout says "--" (an unknown heading is not north)
    readonly property real heading: App.OBDParameterModel.sample("HEADING")
    readonly property bool hasValue: isFinite(heading)
    // Whole degrees 0-359 (359.6 rounds to 0, never "360°")
    readonly property int _degrees: hasValue ? ((Math.round(heading) % 360) + 360) % 360 : 0

    // ── Features ────────────────────────────────────────────────────
    property bool showDegrees: true

    // ── Style ───────────────────────────────────────────────────────
    property color ringColor: Qt.darker(App.Style.obdBarColor, 1.4)
    property color cardinalColor: App.Style.obdValueColor
    property color northColor: App.Style.statusDanger
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    // ── Computed ────────────────────────────────────────────────────
    property real _animHeading: hasValue ? heading : 0
    Behavior on _animHeading {
        // Shortest path across the 359° → 0° wrap.
        RotationAnimation { duration: 150; direction: RotationAnimation.Shortest }
    }

    readonly property real _r: Math.min(width, height) / 2 - App.Spacing.dp(6)
    readonly property real _cx: width / 2
    readonly property real _cy: height / 2

    // Outer ring
    Rectangle {
        x: root._cx - root._r
        y: root._cy - root._r
        width: root._r * 2
        height: width
        radius: width / 2
        color: "transparent"
        border.color: root.ringColor
        border.width: Math.max(1, root._r * 0.02)
    }

    // ── Rotating card: ticks + cardinal letters ─────────────────────
    Item {
        id: card
        x: root._cx - root._r
        y: root._cy - root._r
        width: root._r * 2
        height: width
        rotation: -root._animHeading

        // Tick marks every 30°
        Repeater {
            model: 12
            delegate: Rectangle {
                readonly property bool _cardinal: index % 3 === 0
                width: Math.max(1, root._r * (_cardinal ? 0.03 : 0.015))
                height: root._r * (_cardinal ? 0.14 : 0.08)
                radius: width / 2
                color: root.ringColor
                opacity: _cardinal ? 0.9 : 0.6
                x: card.width / 2 - width / 2
                y: root._r * 0.04
                transformOrigin: Item.Center
                transform: Rotation {
                    origin.x: width / 2
                    origin.y: root._r - root._r * 0.04
                    angle: index * 30
                }
            }
        }

        // Cardinal letters
        Repeater {
            model: [
                { "label": "N", "angle": 0 },
                { "label": "E", "angle": 90 },
                { "label": "S", "angle": 180 },
                { "label": "W", "angle": 270 }
            ]
            delegate: Text {
                readonly property real _a: modelData.angle * Math.PI / 180
                readonly property real _lr: root._r * 0.68
                x: card.width / 2 + Math.sin(_a) * _lr - width / 2
                y: card.height / 2 - Math.cos(_a) * _lr - height / 2
                text: modelData.label
                color: modelData.label === "N" ? root.northColor : root.cardinalColor
                font.bold: true
                font.family: App.Style.fontFamily
                font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 1.1,
                                                     root._r * 0.28))
                // Counter-rotate so letters stay upright while the card spins.
                rotation: root._animHeading
            }
        }
    }

    // ── Fixed lubber line (points up = current heading) ─────────────
    Canvas {
        id: lubber
        x: root._cx - width / 2
        y: root._cy - root._r - height * 0.25
        width: Math.max(App.Spacing.dp(8), root._r * 0.18)
        height: Math.max(App.Spacing.dp(8), root._r * 0.16)
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = String(App.Style.accent)
            ctx.beginPath()
            ctx.moveTo(width / 2, height)
            ctx.lineTo(0, 0)
            ctx.lineTo(width, 0)
            ctx.closePath()
            ctx.fill()
        }
        Connections {
            target: App.Style
            ignoreUnknownSignals: true
            function onAccentChanged() { lubber.requestPaint() }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // ── Center readout ──────────────────────────────────────────────
    Column {
        visible: root.showDegrees
        anchors.centerIn: parent
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.hasValue ? root._degrees + "°" : "--°"
            color: root.valueColor
            font.bold: true
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 1.4,
                                                 root._r * 0.34))
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (!root.hasValue) return ""
                var dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
                return dirs[Math.round(root._degrees / 45) % 8]
            }
            color: root.labelColor
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.75,
                                                 root._r * 0.18))
        }
    }
}
