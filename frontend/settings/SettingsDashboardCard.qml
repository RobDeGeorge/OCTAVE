import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: hubCard

    property string categoryName: ""
    property string section: ""
    property var widgetItems: []
    property string categoryIcon: ""
    property int cardSpan: 1
    property string widgetSource: ""

    signal categorySelected(string section)
    signal subSectionSelected(string section, string subSection)

    color: "transparent"
    radius: dpMin(App.EnvironmentTheme.active.cardRadius, 2)
    clip: true

    // Accent border (spacecraft)
    border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
    border.color: App.EnvironmentTheme.active.accentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                  App.EnvironmentTheme.active.accentBorderOpacity) : "transparent"

    // Glassy translucent card background
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: {
            var base = App.Style.contentColor
            return Qt.rgba(base.r, base.g, base.b, 0.55)
        }
    }

    // Frosted highlight gradient — lighter at top edge, fading down
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 0.15; color: Qt.rgba(1, 1, 1, 0.02) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Top edge highlight line
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: parent.radius
        anchors.rightMargin: parent.radius
        height: 1
        color: Qt.rgba(1, 1, 1, 0.08)
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
        opacity: 0.6
    }

    // Hover glow overlay
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 1.0)
        opacity: cardMouseArea.containsMouse ? 0.06 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
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

        // Category name row with indicator
        RowLayout {
            id: titleRow
            Layout.fillWidth: true
            spacing: App.Spacing.overallSpacing * 0.5
            z: 2

            Text {
                text: hubCard.categoryName
                color: titleMouseArea.containsMouse ? App.Style.accent : App.Style.primaryTextColor
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

                Behavior on color { ColorAnimation { duration: 150 } }
            }

            Text {
                text: "\u203A"
                color: titleMouseArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.1
                font.family: App.Style.fontFamily

                Behavior on color { ColorAnimation { duration: 150 } }
            }

            // Title row mouse area — always goes to detail page
            MouseArea {
                id: titleMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: hubCard.categorySelected(hubCard.section)
            }
        }

        // Widget Loader — loads per-category interactive widget
        Loader {
            id: widgetLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: hubCard.widgetSource
            active: hubCard.widgetSource !== ""
            z: 1
            onLoaded: {
                if (item) {
                    item.cardSpan = Qt.binding(function() { return hubCard.cardSpan })
                    // Wire sub-section navigation if the widget supports it
                    if (typeof item.navigateToSubSection !== "undefined") {
                        item.navigateToSubSection.connect(function(subSection) {
                            hubCard.subSectionSelected(hubCard.section, subSection)
                        })
                    }
                }
            }
        }

        // Fallback: passive widget info rows (used when no widgetSource)
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: hubCard.cardSpan >= 2 ? 2 : 1
            columnSpacing: App.Spacing.overallSpacing * 0.4
            rowSpacing: App.Spacing.overallSpacing * 0.25
            visible: hubCard.widgetSource === "" && hubCard.widgetItems.length > 0

            Repeater {
                model: hubCard.widgetItems.length

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: dataRow.height + App.Spacing.overallSpacing * 0.5
                    radius: dpMin(App.EnvironmentTheme.active.cardRadius * 0.5, 2)
                    color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.7)

                    Row {
                        id: dataRow
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: App.Spacing.overallSpacing * 0.5
                        anchors.right: parent.right
                        anchors.rightMargin: App.Spacing.overallSpacing * 0.5
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
        }
    }

    // Card background mouse area — below widget controls (z: -1)
    MouseArea {
        id: cardMouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        z: -1
        onClicked: hubCard.categorySelected(hubCard.section)
    }
}
