import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings" as Settings

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: sensorMenu
    objectName: "sensorMenu"
    required property StackView stackView
    required property ApplicationWindow mainWindow
    width: parent.width
    height: parent.height

    property string globalFont: App.Style.fontFamily

    // Live IMU values
    property real currentPitch: 0
    property real currentRoll: 0
    property real currentHeading: 0
    property real currentAltitude: 0
    property real currentGForce: 0
    property real currentLateralG: 0
    property real currentLongitudinalG: 0
    property real currentBaroTemp: 0
    property bool imuConnected: false

    // Display settings
    property int decimalPlaces: 1
    property bool useFahrenheit: true
    property bool useImperial: false
    property int emitRate: 60
    property bool showPitch: true
    property bool showRoll: true
    property bool showHeading: true
    property bool showGForce: true
    property bool showAltitude: true
    property bool showTemperature: true

    function headingToCardinal(h) {
        var dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        var idx = Math.round(h / 45) % 8
        return dirs[idx]
    }

    function tempDisplay(celsius) {
        if (useFahrenheit)
            return (celsius * 9 / 5 + 32).toFixed(decimalPlaces)
        return celsius.toFixed(decimalPlaces)
    }

    function altDisplay(meters) {
        if (useImperial)
            return (meters * 3.28084).toFixed(decimalPlaces)
        return meters.toFixed(decimalPlaces)
    }

    Component.onCompleted: {
        imuConnected = berryIMU.connected
        decimalPlaces = settingsManager.get_setting_with_default("sensorDecimalPlaces", 1)
        useFahrenheit = settingsManager.get_setting_with_default("sensorTempUnit", "F") === "F"
        useImperial = settingsManager.get_setting_with_default("sensorAltitudeUnit", "m") === "ft"
        emitRate = settingsManager.get_setting_with_default("sensorEmitRate", 60)
        showPitch = settingsManager.get_setting_with_default("sensorShowPitch", true)
        showRoll = settingsManager.get_setting_with_default("sensorShowRoll", true)
        showHeading = settingsManager.get_setting_with_default("sensorShowHeading", true)
        showGForce = settingsManager.get_setting_with_default("sensorShowGForce", true)
        showAltitude = settingsManager.get_setting_with_default("sensorShowAltitude", true)
        showTemperature = settingsManager.get_setting_with_default("sensorShowTemperature", true)
        berryIMU.setEmitRate(emitRate)
    }

    Connections {
        target: berryIMU
        function onPitchChanged(val) { currentPitch = val }
        function onRollChanged(val) { currentRoll = val }
        function onHeadingChanged(val) { currentHeading = val }
        function onAltitudeChanged(val) { currentAltitude = val }
        function onAccelMagnitudeChanged(val) { currentGForce = val }
        function onLateralGChanged(val) { currentLateralG = val }
        function onLongitudinalGChanged(val) { currentLongitudinalG = val }
        function onBaroTempChanged(val) { currentBaroTemp = val }
        function onConnectionStatusChanged(status) { imuConnected = (status === "Connected") }
    }

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: App.Spacing.overallMargin
            spacing: dp(16)

            // Sensor cards — 3x2 grid
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                rowSpacing: dp(12)
                columnSpacing: dp(12)

                SensorCard {
                    label: "PITCH"
                    value: currentPitch.toFixed(decimalPlaces)
                    unit: "\u00B0"
                    icon: "\u2195"
                    barValue: (currentPitch + 90) / 180
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    visible: showPitch
                }

                SensorCard {
                    label: "ROLL"
                    value: currentRoll.toFixed(decimalPlaces)
                    unit: "\u00B0"
                    icon: "\u2194"
                    barValue: (currentRoll + 90) / 180
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    visible: showRoll
                }

                SensorCard {
                    label: "HEADING"
                    value: currentHeading.toFixed(decimalPlaces)
                    unit: "\u00B0 " + headingToCardinal(currentHeading)
                    icon: "\u25c9"
                    barValue: currentHeading / 360
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    visible: showHeading
                }

                GForceDisplay {
                    lateralG: currentLateralG
                    longitudinalG: currentLongitudinalG
                    magnitude: currentGForce
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    decimalPlaces: sensorMenu.decimalPlaces
                    visible: showGForce
                }

                SensorCard {
                    label: "ALTITUDE"
                    value: altDisplay(currentAltitude)
                    unit: useImperial ? "ft" : "m"
                    icon: "\u25B2"
                    barValue: -1
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    visible: showAltitude
                }

                SensorCard {
                    label: "TEMPERATURE"
                    value: tempDisplay(currentBaroTemp)
                    unit: useFahrenheit ? "\u00B0F" : "\u00B0C"
                    icon: "\u2600"
                    barValue: -1
                    globalFont: sensorMenu.globalFont
                    connected: imuConnected
                    visible: showTemperature && (berryIMU.hasTemperature !== false)
                }
            }

            // Bottom controls
            RowLayout {
                Layout.fillWidth: true
                spacing: dp(12)

                // Tare button
                Rectangle {
                    width: dp(100)
                    height: dp(36)
                    radius: dp(6)
                    color: tareArea.pressed ? Qt.darker(App.Style.accent, 1.3) : App.Style.accent
                    visible: imuConnected

                    Text {
                        anchors.centerIn: parent
                        text: "ZERO"
                        color: "white"
                        font.pixelSize: dp(13)
                        font.bold: true
                        font.family: sensorMenu.globalFont
                    }

                    MouseArea {
                        id: tareArea
                        anchors.fill: parent
                        onClicked: berryIMU.calibrateTare()
                    }
                }

                // Reset tare button
                Rectangle {
                    width: dp(100)
                    height: dp(36)
                    radius: dp(6)
                    color: resetArea.pressed ? Qt.darker(App.Style.secondaryTextColor, 1.3) : App.Style.headerBackgroundColor
                    border.color: App.Style.secondaryTextColor
                    border.width: 1
                    visible: imuConnected

                    Text {
                        anchors.centerIn: parent
                        text: "RESET"
                        color: App.Style.primaryTextColor
                        font.pixelSize: dp(13)
                        font.bold: true
                        font.family: sensorMenu.globalFont
                    }

                    MouseArea {
                        id: resetArea
                        anchors.fill: parent
                        onClicked: berryIMU.resetTare()
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
