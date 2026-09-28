import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App

Rectangle {
    id: pill

    property string label: ""
    property string value: ""
    property color statusColor: "transparent"
    property bool showDot: statusColor.a > 0 && statusColor !== Qt.rgba(0, 0, 0, 0)

    Layout.fillWidth: true
    implicitHeight: App.Spacing.dp(24)
    radius: App.Spacing.dpMin(App.EnvironmentTheme.active.cardRadius * 0.5, 2)
    color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.7)

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: App.Spacing.overallSpacing * 0.5
        anchors.right: parent.right
        anchors.rightMargin: App.Spacing.overallSpacing * 0.5
        spacing: App.Spacing.overallSpacing * 0.3

        // Status dot with pulse glow
        Item {
            width: App.Spacing.dp(7)
            height: App.Spacing.dp(7)
            visible: pill.showDot
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: App.Spacing.dp(11)
                height: App.Spacing.dp(11)
                radius: width / 2
                anchors.centerIn: parent
                color: pill.statusColor
                property real pulseOpacity: 0.3
                opacity: pulseOpacity
                SequentialAnimation on pulseOpacity {
                    running: pill.showDot && pill.visible  // visible includes hidden pages/sections
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 1500; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.1; duration: 1500; easing.type: Easing.InOutSine }
                }
            }

            Rectangle {
                width: App.Spacing.dp(7)
                height: App.Spacing.dp(7)
                radius: width / 2
                anchors.centerIn: parent
                color: pill.statusColor
            }
        }

        Text {
            text: pill.label + ":"
            color: App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.overallText * 0.85
            font.family: App.Style.fontFamily
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: pill.value
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText * 0.85
            font.bold: true
            font.family: App.Style.fontFamily
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
}
