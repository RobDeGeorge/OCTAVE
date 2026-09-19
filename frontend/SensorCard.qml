import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: card

    property string label: ""
    property string value: "--"
    property string unit: ""
    property string icon: ""
    property real barValue: -1  // 0-1 for bar, -1 to hide
    property string globalFont: ""
    property bool connected: false

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumHeight: dp(100)
    radius: dp(10)
    color: App.Style.headerBackgroundColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: dp(14)
        spacing: dp(6)

        // Label row
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: dp(6)

            Text {
                text: card.icon
                color: App.Style.accent
                font.family: App.Style.symbolFont
                font.pixelSize: dp(16)
            }

            Text {
                text: card.label
                color: App.Style.secondaryTextColor
                font.pixelSize: dp(11)
                font.bold: true
                font.family: card.globalFont
                font.letterSpacing: dp(1.5)
            }
        }

        Item { Layout.fillHeight: true }

        // Value
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: connected ? card.value : "--"
            color: App.Style.primaryTextColor
            font.pixelSize: dp(36)
            font.bold: true
            font.family: card.globalFont

            Behavior on text {
                enabled: false
            }
        }

        // Unit
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: card.unit
            color: App.Style.secondaryTextColor
            font.pixelSize: dp(14)
            font.family: card.globalFont
        }

        // Optional bar indicator
        Rectangle {
            Layout.fillWidth: true
            height: dp(4)
            radius: dp(2)
            color: Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.2)
            visible: card.barValue >= 0

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, card.barValue))
                height: parent.height
                radius: parent.radius
                color: App.Style.accent

                Behavior on width {
                    NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
                }
            }
        }
    }
}
