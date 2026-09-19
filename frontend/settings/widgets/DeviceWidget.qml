import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App
import ".."

Item {
    id: widgetRoot
    property int cardSpan: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: App.Spacing.overallSpacing * 0.3

        WidgetPill {
            label: "Name"
            value: settingsManager ? (settingsManager.deviceName || "OCTAVE") : "OCTAVE"
        }

        // Network status row with refresh
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: App.Spacing.dp(24)
            radius: App.Spacing.dpMin(App.EnvironmentTheme.active.cardRadius * 0.5, 2)
            color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.7)

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: App.Spacing.overallSpacing * 0.5
                anchors.right: refreshBtn.left
                anchors.rightMargin: App.Spacing.overallSpacing * 0.3
                spacing: App.Spacing.overallSpacing * 0.3

                Rectangle {
                    width: App.Spacing.dp(7)
                    height: App.Spacing.dp(7)
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: networkManager && networkManager.isConnected
                        ? App.Style.statusConnected : App.Style.statusDisconnected
                }

                Text {
                    text: {
                        if (!networkManager) return "Unknown"
                        if (!networkManager.isConnected) return "No network"
                        return networkManager.networkName || "Connected"
                    }
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.85
                    font.family: App.Style.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                }
            }

            Text {
                id: refreshBtn
                anchors.right: parent.right
                anchors.rightMargin: App.Spacing.overallSpacing * 0.5
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2B6E"
                color: refreshArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.family: App.Style.symbolFont

                Behavior on color { ColorAnimation { duration: 150 } }

                MouseArea {
                    id: refreshArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (networkManager)
                            networkManager.refreshNetwork()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
