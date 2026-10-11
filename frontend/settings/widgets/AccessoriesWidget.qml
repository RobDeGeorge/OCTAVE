import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App
import ".."

Item {
    id: widgetRoot
    property int cardSpan: 1

    signal navigateToSubSection(string subSection)

    property bool espConnected: typeof esp32VolumeManager !== "undefined"
        && esp32VolumeManager && esp32VolumeManager.is_connected()
    property bool gestureEnabled: settingsManager ? settingsManager.gestureSensorEnabled : false
    property string gestureStatus: typeof gestureSensor !== "undefined" && gestureSensor
        ? gestureSensor.connectionStatus : "Disabled"
    property bool phoneDockOn: settingsManager
        && settingsManager.phoneMirrorEnabled

    Connections {
        target: typeof esp32VolumeManager !== "undefined" && esp32VolumeManager ? esp32VolumeManager : null
        function onConnectionStatusChanged() {
            widgetRoot.espConnected = esp32VolumeManager.is_connected()
        }
    }

    Connections {
        target: typeof gestureSensor !== "undefined" && gestureSensor ? gestureSensor : null
        function onConnectionStatusChanged(status) {
            widgetRoot.gestureStatus = status
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: App.Spacing.overallSpacing * 0.25

        WidgetPill {
            label: "Knob"
            value: widgetRoot.espConnected ? "Connected" : "Disconnected"
            statusColor: widgetRoot.espConnected ? App.Style.statusConnected : App.Style.statusDisconnected
        }

        SettingsToggle {
            compact: true
            text: "Gestures"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: widgetRoot.gestureEnabled
            onToggled: {
                if (settingsManager)
                    settingsManager.save_gesture_sensor_enabled(checked)
            }
        }

        WidgetPill {
            label: "Phone Dock"
            value: widgetRoot.phoneDockOn ? "On" : "Off"
            statusColor: widgetRoot.phoneDockOn ? App.Style.statusConnected : App.Style.statusDisconnected
        }

        Item { Layout.fillHeight: true }

        // Sub-section chips
        Flow {
            Layout.fillWidth: true
            spacing: App.Spacing.overallSpacing * 0.3

            WidgetChip {
                text: "Volume Knob"
                onClicked: widgetRoot.navigateToSubSection("Volume Knob")
            }

            WidgetChip {
                text: "Gesture Sensor"
                onClicked: widgetRoot.navigateToSubSection("Gesture Sensor")
            }

            WidgetChip {
                text: "Phone Dock"
                onClicked: widgetRoot.navigateToSubSection("Phone Dock")
            }
        }
    }
}
