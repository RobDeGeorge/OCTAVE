// VehicleRigPanel.qml
//
// Test controls for the vehicle rig: every panel and lamp, steering, wheel
// speed, engine speed and, without an IMU, pitch and roll by hand. Not part
// of the page a driver sees: CarMenu loads it only while `rigControls` is set
// (its menu offers that in a --dev run; tools/vehicle/check_jeep_view.py sets
// it), and the object names here are what that checker drives.
import QtQuick
import QtQuick.Controls
import "." as App

Rectangle {
    id: panel

    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    // The JeepVehicle to drive
    required property var vehicle
    // Live OBD owns wheel and engine speed: those controls are disabled
    property bool liveDrive: false
    // Offer pitch / roll sliders (the host has no IMU to follow)
    property bool manualAttitude: false
    property real pitch: 0
    property real roll: 0
    signal attitudeMoved(real pitch, real roll)

    implicitHeight: controls.implicitHeight + 12
    color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.92)
    radius: Math.max(2, dp(10))
    border.width: 1
    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)

    // Stock Basic controls, coloured from the theme. Basic reads:
    // button/buttonText (idle), dark/brightText (checked button, slider fill
    // + handle border), mid (pressed blend), midlight (slider groove),
    // window/light (handle).
    palette.button: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.14)
    palette.buttonText: App.Style.primaryTextColor
    palette.brightText: App.Style.backgroundColor
    palette.dark: App.Style.accent
    palette.mid: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.45)
    palette.midlight: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.18)
    palette.window: App.Style.primaryTextColor
    palette.light: App.Style.accent
    palette.windowText: App.Style.primaryTextColor

    Flow {
        id: controls
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 6 }
        spacing: 4
        enabled: panel.vehicle.ready
        Repeater {
            model: [
                {label: "Driver door", key: "driverDoorOpen"},
                {label: "Passenger door", key: "passengerDoorOpen"},
                {label: "Rear glass", key: "rearGlassOpen", glass: true},
                {label: "Tailgate", key: "tailgateOpen"},
                {label: "Hood", key: "hoodOpen"},
                {label: "Headlights", key: "headlightsOn"},
                {label: "Fog lights", key: "fogLightsOn"},
                {label: "Brake lights", key: "brakeLightsOn"},
                {label: "Reverse lights", key: "reverseLightsOn"}
            ]
            delegate: Button {
                required property var modelData
                objectName: "jeepControl_" + modelData.key
                visible: !modelData.glass || panel.vehicle.hasRearGlass
                text: modelData.label
                checkable: true
                checked: panel.vehicle[modelData.key]
                onClicked: panel.vehicle[modelData.key] = !panel.vehicle[modelData.key]
            }
        }
        Button { objectName: "jeepOpenAll"; text: "Open all"; onClicked: panel.vehicle.openAll() }
        Button { text: "Close all"; onClicked: panel.vehicle.closeAll() }
        Row {
            spacing: 6
            Label { text: "Steer"; anchors.verticalCenter: parent.verticalCenter }
            Slider {
                objectName: "jeepSteering"
                width: panel.dp(130)
                from: -1; to: 1; value: -panel.vehicle.steeringInput
                onMoved: panel.vehicle.steeringInput = -value
            }
            Button { objectName: "centerWheels"; text: "Center wheels"; onClicked: panel.vehicle.steeringInput = 0 }
        }
        Row {
            spacing: 6
            Button {
                objectName: "jeepSpinWheels"; text: "Spin wheels"
                enabled: !panel.liveDrive
                checkable: true; checked: panel.vehicle.wheelSpeedKph !== 0
                onClicked: panel.vehicle.wheelSpeedKph = panel.vehicle.wheelSpeedKph !== 0 ? 0 : 10
            }
            Slider {
                objectName: "jeepWheelSpeed"; width: panel.dp(130)
                enabled: !panel.liveDrive
                from: -30; to: 30; value: panel.vehicle.wheelSpeedKph
                onMoved: panel.vehicle.wheelSpeedKph = value
            }
            Label {
                text: panel.vehicle.wheelSpeedKph.toFixed(0) + " km/h"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 6
            Label { text: "Engine"; anchors.verticalCenter: parent.verticalCenter }
            Slider {
                objectName: "jeepEngineRpm"; width: panel.dp(130)
                enabled: !panel.liveDrive
                from: 0; to: 6000; stepSize: 50; value: panel.vehicle.engineRpm
                onMoved: panel.vehicle.engineRpm = value
            }
            Label {
                text: panel.vehicle.engineRpm.toFixed(0) + " rpm" + (panel.liveDrive ? " · live OBD" : "")
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 6
            visible: panel.manualAttitude
            Label { text: "Pitch"; anchors.verticalCenter: parent.verticalCenter }
            Slider {
                objectName: "jeepPitch"; width: panel.dp(130)
                from: -60; to: 60; value: panel.pitch
                onMoved: panel.attitudeMoved(value, panel.roll)
            }
            Label { text: "Roll"; anchors.verticalCenter: parent.verticalCenter }
            Slider {
                objectName: "jeepRoll"; width: panel.dp(130)
                from: -60; to: 60; value: panel.roll
                onMoved: panel.attitudeMoved(panel.pitch, value)
            }
            Button { text: "Level"; onClicked: panel.attitudeMoved(0, 0) }
        }
    }
}
