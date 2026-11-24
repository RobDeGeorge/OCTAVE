import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdHome
    objectName: "obdHome"
    required property StackView stackView
    required property ApplicationWindow mainWindow

    property string globalFont: App.Style.fontFamily
    property color backgroundColor: App.Style.obdBoxBackground
    property color accentColor: App.Style.obdBarColor
    property color textColor: App.Style.obdValueColor
    property color labelColor: App.Style.obdLabelColor

    // Track last-visited subpage for visual indicator
    property string lastSubpage: settingsManager
        ? settingsManager.get_setting_with_default("lastOBDPage", "")
        : ""

    // Connection status: bound to the manager, so a page built after the
    // link came up (the startup pre-build, the browser demo) shows it too
    readonly property bool connected: obdManager ? obdManager.connected : false
    // Every step toward a link (the BLE ones included), from the manager's state

    // Helper to push a subpage
    function pushSubpage(qmlFile) {
        if (settingsManager) {
            settingsManager.save_setting("lastOBDPage", qmlFile)
            lastSubpage = qmlFile
        }
        stackView.openPage(qmlFile)
    }

    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        // No adapter connected: the page is just the connect prompt; the
        // status bar, log and cards come back once the link is up
        Column {
            objectName: "obdHomeDisconnected"
            visible: !obdHome.connected
            anchors.centerIn: parent
            width: Math.min(parent.width - dp(40), dp(560))
            spacing: dp(16)

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "No vehicle connected"
                color: textColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                font.family: obdHome.globalFont
            }

            App.OBDConnectButton {
                anchors.horizontalCenter: parent.horizontalCenter
                mainWindow: obdHome.mainWindow
                fontFamily: obdHome.globalFont
            }
        }

        ColumnLayout {
            visible: obdHome.connected
            anchors.fill: parent
            anchors.margins: dp(20)
            spacing: dp(20)

            // ── Button grid ──────────────────────────────────────
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                rowSpacing: dp(16)
                columnSpacing: dp(16)

                // --- Dashboards card ---
                Rectangle {
                    id: dashboardsCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: dashboardsMouse.containsMouse
                           ? Qt.lighter(Qt.darker(backgroundColor, 0.85), 1.08)
                           : Qt.darker(backgroundColor, 0.85)
                    border.color: lastSubpage === "OBDMenu.qml" ? accentColor : "transparent"
                    border.width: lastSubpage === "OBDMenu.qml" ? 2 : 0

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: dashboardsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: obdHome.pushSubpage("OBDMenu.qml")
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        // Icon placeholder — tachometer shape
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: dp(56)
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.color: accentColor
                            border.width: 3

                            // Needle
                            Rectangle {
                                width: 3
                                height: parent.height * 0.35
                                color: accentColor
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.verticalCenter
                                transformOrigin: Item.Bottom
                                rotation: 45
                            }
                        }

                        Text {
                            text: "Live Data"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Every reading from the car"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

                // --- Trouble Codes (DTC) card ---
                Rectangle {
                    id: dtcCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: dtcMouse.containsMouse
                           ? Qt.lighter(Qt.darker(backgroundColor, 0.85), 1.08)
                           : Qt.darker(backgroundColor, 0.85)
                    border.color: lastSubpage === "OBDDiagnostics.qml" ? accentColor : "transparent"
                    border.width: lastSubpage === "OBDDiagnostics.qml" ? 2 : 0

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: dtcMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: obdHome.pushSubpage("OBDDiagnostics.qml")
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        // Icon placeholder — warning triangle
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            width: dp(56)
                            height: width

                            Text {
                                font.family: App.Style.symbolFont
                                anchors.centerIn: parent
                                text: "\u26A0"
                                font.pixelSize: dp(36)
                                color: accentColor
                            }
                        }

                        Text {
                            text: "Trouble Codes"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Read & clear DTCs"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

                // --- Trip History (black box) card ---
                Rectangle {
                    id: tripCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(12, 4)
                    color: tripMouse.containsMouse
                           ? Qt.lighter(Qt.darker(backgroundColor, 0.85), 1.08)
                           : Qt.darker(backgroundColor, 0.85)
                    border.color: lastSubpage === "TripHistory.qml" ? accentColor : "transparent"
                    border.width: lastSubpage === "TripHistory.qml" ? 2 : 0

                    readonly property bool recording: typeof tripRecorder !== "undefined" && tripRecorder && tripRecorder.recording
                    readonly property bool unavailable: typeof tripRecorder !== "undefined" && tripRecorder && !tripRecorder.storeReady
                    readonly property int tripCount: typeof tripRecorder !== "undefined" && tripRecorder ? tripRecorder.trips.length : 0

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: tripMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: obdHome.pushSubpage("TripHistory.qml")
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: dp(12)

                        // Icon — a black box: rounded square with a recording dot
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: dp(56)
                            height: width
                            radius: dpMin(10, 4)
                            color: "transparent"
                            border.color: accentColor
                            border.width: 3

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width * 0.32
                                height: width
                                radius: width / 2
                                color: tripCard.recording ? "#FF4444" : accentColor
                                SequentialAnimation on opacity {
                                    running: tripCard.recording && obdHome.visible
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.3; duration: 700 }
                                    NumberAnimation { to: 1.0; duration: 700 }
                                }
                            }
                        }

                        Text {
                            text: "Trip History"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: tripCard.unavailable ? "Black box unavailable"
                                : tripCard.recording ? "Recording this drive"
                                : tripCard.tripCount > 0 ? tripCard.tripCount + (tripCard.tripCount === 1 ? " drive logged" : " drives logged")
                                : "Black box drive logs"
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: obdHome.globalFont
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }
    }
}
