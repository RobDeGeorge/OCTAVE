// GForceGauge.qml
//
// 2D G-force dot: concentric rings with a crosshair, a dot that moves with
// lateral (x) and longitudinal (y) acceleration, an optional fading trail,
// and an optional total-G readout. Compact dashboard sibling of the
// full-screen GForceDisplay.qml.
//
// Self-binding: reads LATERAL_G / LONGITUDINAL_G from OBDParameterModel
// (fed by the berryIMU manager) — no paramId, so the editor hides the PID
// picker for it (supportedKinds: []).
//
// Usage:
//     GForceGauge { maxG: 1.5; showTrail: true }
//
// See docs/GAUGE_AUTHORING.md for the full API.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Data binding ────────────────────────────────────────────────
    // NaN until the IMU reports: no dot, no trail, "--" (not a centred 0 g)
    readonly property real lateralG: App.OBDParameterModel.sample("LATERAL_G")
    readonly property real longitudinalG: App.OBDParameterModel.sample("LONGITUDINAL_G")
    readonly property bool hasValue: isFinite(lateralG) && isFinite(longitudinalG)

    // ── Features ────────────────────────────────────────────────────
    property real maxG: 1.5         // g at the outer ring
    property bool showTrail: true
    property bool showValue: true   // total-G readout at the bottom

    // ── Style ───────────────────────────────────────────────────────
    property color ringColor: Qt.darker(App.Style.obdBarColor, 1.4)
    property color dotColor: App.Style.obdBarColor
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    // ── Computed ────────────────────────────────────────────────────
    property real _animLat: hasValue ? lateralG : 0
    Behavior on _animLat { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }
    property real _animLon: hasValue ? longitudinalG : 0
    Behavior on _animLon { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }

    readonly property real _r: Math.min(width, height) / 2 - App.Spacing.dp(6)
    readonly property real _cx: width / 2
    readonly property real _cy: height / 2
    readonly property real _totalG: Math.sqrt(lateralG * lateralG
                                              + longitudinalG * longitudinalG)

    // (lat, lon) in ring units, clamped to the outer ring along the vector
    // (clamping each axis alone let a diagonal sit outside the circle)
    function _ringPos(lat, lon) {
        if (!(maxG > 0)) return Qt.point(0, 0)
        var x = lat / maxG, y = lon / maxG
        var len = Math.sqrt(x * x + y * y)
        return len > 1 ? Qt.point(x / len, y / len) : Qt.point(x, y)
    }
    readonly property point _dotPos: _ringPos(_animLat, _animLon)

    // ── Rings + crosshair ───────────────────────────────────────────
    Repeater {
        model: [1.0, 0.5]
        delegate: Rectangle {
            x: root._cx - root._r * modelData
            y: root._cy - root._r * modelData
            width: root._r * 2 * modelData
            height: width
            radius: width / 2
            color: "transparent"
            border.color: root.ringColor
            border.width: 1
            opacity: index === 0 ? 0.9 : 0.5
        }
    }
    Rectangle {
        x: root._cx - root._r; y: root._cy
        width: root._r * 2; height: 1
        color: root.ringColor; opacity: 0.4
    }
    Rectangle {
        x: root._cx; y: root._cy - root._r
        width: 1; height: root._r * 2
        color: root.ringColor; opacity: 0.4
    }

    // Outer-ring scale label (e.g. "1.5g")
    Text {
        x: root._cx + root._r * 0.72
        y: root._cy - root._r * 0.72 - height
        text: root.maxG.toFixed(1) + "g"
        color: root.labelColor
        font.family: App.Style.fontFamily
        font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.7, root._r * 0.18))
        opacity: 0.7
    }

    // ── Trail ───────────────────────────────────────────────────────
    ListModel { id: trailModel }

    Timer {
        interval: 100
        repeat: true
        running: root.visible && root.showTrail
        onTriggered: {
            if (!root.hasValue) return
            var p = root._ringPos(root.lateralG, root.longitudinalG)
            trailModel.append({ "tx": p.x, "ty": p.y })
            while (trailModel.count > 20) trailModel.remove(0)
        }
    }

    onShowTrailChanged: if (!showTrail) trailModel.clear()

    Repeater {
        model: root.showTrail ? trailModel : null
        delegate: Rectangle {
            readonly property real _d: Math.max(2, root._r * 0.06)
            x: root._cx + model.tx * root._r - _d / 2
            y: root._cy + model.ty * root._r - _d / 2
            width: _d; height: _d; radius: _d / 2
            color: root.dotColor
            opacity: 0.35 * (index + 1) / Math.max(1, trailModel.count)
        }
    }

    // ── The dot ─────────────────────────────────────────────────────
    Rectangle {
        readonly property real _d: Math.max(App.Spacing.dp(6), root._r * 0.16)
        visible: root.hasValue
        x: root._cx + root._dotPos.x * root._r - _d / 2
        y: root._cy + root._dotPos.y * root._r - _d / 2
        width: _d; height: _d; radius: _d / 2
        color: root.dotColor
        border.color: Qt.lighter(root.dotColor, 1.4)
        border.width: 1
    }

    // ── Total-G readout ─────────────────────────────────────────────
    Text {
        visible: root.showValue
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: root.hasValue ? root._totalG.toFixed(2) + " g" : "-- g"
        color: root.valueColor
        font.bold: true
        font.family: App.Style.fontFamily
        font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.95, root._r * 0.22))
    }
}
