import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: sensorHome
    objectName: "sensorHome"
    required property StackView stackView
    required property ApplicationWindow mainWindow

    property string globalFont: App.Style.fontFamily

    // Match OBDHome's palette so the two hubs feel like the same surface.
    property color backgroundColor: App.Style.obdBoxBackground
    property color accentColor: App.Style.obdBarColor
    property color textColor: App.Style.obdValueColor
    property color labelColor: App.Style.obdLabelColor

    // Track last-visited subpage for the "you were just here" border.
    property string lastSubpage: settingsManager
        ? settingsManager.get_setting_with_default("lastSensorPage", "")
        : ""

    property bool imuConnected: berryIMU ? berryIMU.connected : false

    Connections {
        target: berryIMU
        ignoreUnknownSignals: true
        function onConnectionStatusChanged(status) {
            sensorHome.imuConnected = (status === "Connected")
        }
    }

    // Remember the subpage only once it actually opened. Persisting up front
    // meant a page that can no longer load (CarMenu without the optional
    // QtQuick3D module) became the bottom bar's remembered target, so the
    // sensor button silently did nothing on every later launch.
    function pushSubpage(qmlFile) {
        if (!stackView.openPage(qmlFile))
            return false
        if (settingsManager) {
            settingsManager.save_setting("lastSensorPage", qmlFile)
            lastSubpage = qmlFile
        }
        return true
    }

    // QtQuick3D is an optional runtime dependency; probe it once so the 3D View
    // card can say so instead of looking clickable and doing nothing.
    property bool has3DView: {
        var probe = Qt.createComponent("CarMenu.qml", Component.PreferSynchronous)
        return probe.status === Component.Ready
    }

    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: dp(20)
            spacing: dp(20)

            // ── Connection status bar ────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: dp(48)
                radius: dpMin(8, 3)
                color: Qt.darker(backgroundColor, 0.9)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: dp(16)
                    anchors.rightMargin: dp(16)
                    spacing: dp(10)

                    Rectangle {
                        width: dp(12)
                        height: width
                        radius: width / 2
                        color: sensorHome.imuConnected ? "#00FF00" : "#FF4444"
                    }

                    Text {
                        text: sensorHome.imuConnected ? "IMU Connected" : "IMU Disconnected"
                        color: textColor
                        font.pixelSize: App.Spacing.overallText
                        font.bold: true
                        font.family: sensorHome.globalFont
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // ── Button grid ──────────────────────────────────────
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                rowSpacing: dp(16)
                columnSpacing: dp(16)

                // --- Sensors card (live readouts) ---
                Rectangle {
                    id: sensorsCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: sensorsMouse.containsMouse
                           ? Qt.lighter(Qt.darker(backgroundColor, 0.85), 1.08)
                           : Qt.darker(backgroundColor, 0.85)
                    border.color: lastSubpage === "SensorMenu.qml" ? accentColor : "transparent"
                    border.width: lastSubpage === "SensorMenu.qml" ? 2 : 0

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: sensorsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sensorHome.pushSubpage("SensorMenu.qml")
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "◉"
                            font.family: App.Style.symbolFont
                            color: accentColor
                            font.pixelSize: dp(48)
                        }

                        Text {
                            text: "Sensors"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Pitch, roll, heading, G-force"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

                // --- 3D View card ---
                Rectangle {
                    id: viewCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: viewMouse.containsMouse
                           ? Qt.lighter(Qt.darker(backgroundColor, 0.85), 1.08)
                           : Qt.darker(backgroundColor, 0.85)
                    border.color: lastSubpage === "CarMenu.qml" ? accentColor : "transparent"
                    border.width: lastSubpage === "CarMenu.qml" ? 2 : 0
                    opacity: sensorHome.has3DView ? 1.0 : 0.5

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: viewMouse
                        anchors.fill: parent
                        hoverEnabled: sensorHome.has3DView
                        enabled: sensorHome.has3DView
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sensorHome.pushSubpage("CarMenu.qml")
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            width: dp(56)
                            height: width

                            Rectangle {
                                anchors.centerIn: parent
                                width: dp(40)
                                height: dp(28)
                                radius: dpMin(4, 1)
                                color: "transparent"
                                border.color: accentColor
                                border.width: 3

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.top
                                    width: parent.width * 0.55
                                    height: dp(8)
                                    radius: dpMin(2, 1)
                                    color: "transparent"
                                    border.color: accentColor
                                    border.width: 3
                                }
                            }
                        }

                        Text {
                            text: "3D View"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: sensorHome.has3DView ? "Live vehicle attitude"
                                                       : "Requires the QtQuick3D module"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

                // --- Calibration card (placeholder) ---
                Rectangle {
                    id: calibrationCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: Qt.darker(backgroundColor, 0.85)
                    opacity: 0.5

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "⌖"
                            font.family: App.Style.symbolFont
                            color: labelColor
                            font.pixelSize: dp(48)
                        }

                        Text {
                            text: "Calibration"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Coming soon"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                            opacity: 0.7
                        }
                    }
                }

                // --- Logging card (placeholder) ---
                Rectangle {
                    id: loggingCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: Qt.darker(backgroundColor, 0.85)
                    opacity: 0.5

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "◬"
                            font.family: App.Style.symbolFont
                            color: labelColor
                            font.pixelSize: dp(48)
                        }

                        Text {
                            text: "Sensor Log"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Coming soon"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: sensorHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                            opacity: 0.7
                        }
                    }
                }
            }
        }
    }
}
