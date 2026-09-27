import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    id: hubCard

    property string categoryName: ""
    property string section: ""
    property var widgetItems: []
    property string categoryIcon: ""

    signal categorySelected(string section)

    color: "transparent"
    radius: App.EnvironmentTheme.active.hubCardRadius
    clip: true

    // Accent border (spacecraft)
    border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
    border.color: App.EnvironmentTheme.active.accentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                  App.EnvironmentTheme.active.accentBorderOpacity) : "transparent"

    // Hover / press transforms
    scale: cardMouseArea.pressed ? 0.98 : (cardMouseArea.containsMouse ? 1.02 : 1.0)
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

    // Gradient background — accent-tinted left to transparent right
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Frosted glass gradient overlay (deep sea)
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        visible: App.EnvironmentTheme.active.cardGlassEffect
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.05) }
            GradientStop { position: 0.4; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.03) }
        }
    }

    App.CornerBrackets {
        visible: App.EnvironmentTheme.active.cornerBrackets
    }

    // Bottom accent line
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: App.Style.accent }
            GradientStop { position: 0.7; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: cardMouseArea.containsMouse ? 1.0 : 0.6
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // Card content
    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: App.Spacing.overallSpacing * 1.2
            rightMargin: App.Spacing.overallSpacing * 1.2
            topMargin: App.Spacing.overallSpacing * 0.8
            bottomMargin: App.Spacing.overallSpacing * 0.8
        }
        spacing: App.Spacing.rowSpacing * 0.3

        // Category name row with chevron
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Spacing.overallSpacing * 0.5

            Text {
                text: hubCard.categoryName
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.3
                font.bold: true
                font.family: App.Style.fontFamily
                font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
                font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
                Layout.fillWidth: true
                // Shrink to fit rather than truncate so titles always show the whole word.
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: dp(8)
                elide: Text.ElideNone
            }

            Text {
                text: "\u203A"
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.1
                font.family: App.Style.fontFamily
            }
        }

        // Widget info rows
        Column {
            Layout.fillWidth: true
            spacing: 2
            visible: hubCard.widgetItems.length > 0

            Repeater {
                model: hubCard.widgetItems.length

                Row {
                    spacing: App.Spacing.overallSpacing * 0.3

                    Text {
                        text: hubCard.widgetItems[index] ? hubCard.widgetItems[index].label + ":" : ""
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.85
                        font.family: App.Style.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: hubCard.widgetItems[index] ? hubCard.widgetItems[index].value : ""
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText * 0.85
                        font.bold: true
                        font.family: App.Style.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    MouseArea {
        id: cardMouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: hubCard.categorySelected(hubCard.section)
    }
}
