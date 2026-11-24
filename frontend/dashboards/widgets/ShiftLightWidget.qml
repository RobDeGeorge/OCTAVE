// ShiftLightWidget.qml
//
// The RPM shift light as a lamp: dark until a shift-light flag is reached,
// then that flag's colour (flashing where the flag says so). It only draws:
// the flags, the flash phase and the look (glow, pulse, colour transition)
// are ShiftLightManager's (Settings > OBD > Shift Light).
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []); the
// catalog's `demand: ["RPM"]` keeps RPM polled while a dashboard shows it.
import QtQuick 2.15
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // (null-guarded: at shutdown the managers go before the engine)
    readonly property var _sl: (typeof shiftLightManager !== "undefined" && shiftLightManager) ? shiftLightManager : null
    readonly property bool lit: !!_sl && _sl.enabled && _sl.lit

    Rectangle {
        id: lamp
        objectName: "dashboardShiftLamp"
        anchors.centerIn: parent
        width: Math.max(1, Math.min(root.width, root.height) * 0.8)
        height: width
        radius: width / 2
        // Dark silhouette when off or in a flash's off phase
        color: root.lit ? root._sl.activeFlag.color : "#1a1a1a"
        border.color: root._sl && root._sl.active ? Qt.darker(color, 1.3) : "#333333"
        border.width: Math.max(1, width * 0.03)
        Behavior on color { ColorAnimation { duration: root._sl ? root._sl.colorTransitionSpeed : 0 } }

        Rectangle {
            anchors.centerIn: parent
            width: parent.width * (root._sl ? root._sl.glowSize : 0)
            height: width
            radius: width / 2
            color: Qt.lighter(parent.color, 1.5)
            opacity: root._sl ? root._sl.glowIntensity : 0
            visible: root.lit && root._sl.glowSize > 0
        }

        SequentialAnimation {
            running: root.lit && root._sl.pulseEnabled && root.visible
            loops: Animation.Infinite
            onRunningChanged: if (!running) lamp.scale = 1.0
            PropertyAnimation { target: lamp; property: "scale"; to: 1.12; duration: 200; easing.type: Easing.OutQuad }
            PropertyAnimation { target: lamp; property: "scale"; to: 1.0; duration: 400; easing.type: Easing.InOutQuad }
            PauseAnimation { duration: 200 }
        }
    }
}
