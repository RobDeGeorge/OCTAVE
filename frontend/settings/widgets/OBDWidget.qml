import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App
import ".."

Item {
    id: widgetRoot
    property int cardSpan: 1

    signal navigateToSubSection(string subSection)

    property string obdStatus: obdManager ? obdManager.get_connection_status() : "N/A"
    property color obdStatusColor: {
        if (obdStatus.indexOf("Connected") !== -1)
            return App.Style.statusConnected
        if (obdStatus.indexOf("Connecting") !== -1)
            return App.Style.statusWarning
        return App.Style.statusDisconnected
    }
    property bool obdConnected: obdManager ? obdManager.is_connected() : false

    // Live status updates
    Connections {
        target: obdManager ? obdManager : null
        function onConnectionStatusChanged() {
            widgetRoot.obdStatus = obdManager.get_connection_status()
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: widgetRoot.cardSpan >= 2 ? 2 : 1
        columnSpacing: App.Spacing.overallSpacing * 0.4
        rowSpacing: App.Spacing.overallSpacing * 0.25

        // Status + reconnect row
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: App.Spacing.dp(24)
            radius: App.Spacing.dpMin(App.EnvironmentTheme.active.cardRadius * 0.5, 2)
            color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.7)

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: App.Spacing.overallSpacing * 0.5
                anchors.right: reconnectBtn.left
                anchors.rightMargin: App.Spacing.overallSpacing * 0.3
                spacing: App.Spacing.overallSpacing * 0.3

                Rectangle {
                    width: App.Spacing.dp(7)
                    height: App.Spacing.dp(7)
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: widgetRoot.obdStatusColor
                }

                Text {
                    text: widgetRoot.obdStatus
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.family: App.Style.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                }
            }

            Text {
                id: reconnectBtn
                anchors.right: parent.right
                anchors.rightMargin: App.Spacing.overallSpacing * 0.5
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2B6E"
                color: reconnectArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.family: App.Style.symbolFont
                visible: !widgetRoot.obdConnected

                Behavior on color { ColorAnimation { duration: 150 } }

                MouseArea {
                    id: reconnectArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (obdManager)
                            obdManager.reconnect()
                    }
                }
            }
        }

        SettingsToggle {
            compact: true
            text: "Fast"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: settingsManager ? settingsManager.obdFastMode : false
            onToggled: {
                if (settingsManager)
                    settingsManager.save_obd_fast_mode(checked)
            }
        }

        SettingsToggle {
            compact: true
            text: "Circular Gauges"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: settingsManager ? settingsManager.get_setting_with_default("obdCardStyleCircular", false) : false
            onToggled: {
                if (settingsManager)
                    settingsManager.save_setting("obdCardStyleCircular", checked)
            }
        }

        Item { Layout.fillHeight: true; Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1 }

        // Sub-section chips
        Flow {
            Layout.fillWidth: true
            Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1
            spacing: App.Spacing.overallSpacing * 0.3

            WidgetChip {
                text: "Connection"
                onClicked: widgetRoot.navigateToSubSection("Connection")
            }

            WidgetChip {
                text: "Parameters"
                onClicked: widgetRoot.navigateToSubSection("Parameters")
            }
        }
    }
}
