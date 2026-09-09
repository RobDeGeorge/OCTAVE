import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: carouselLayout

    // Required from orchestrator
    property var settingsMenu: null
    property var hubModel: []

    // Expose swipeView for external navigation
    property alias currentIndex: swipeView.currentIndex

    // Environment backgrounds provided by SettingsMenu parent

    // ─── Category tab strip (top) ───
    Row {
        id: tabStrip
        anchors.top: parent.top
        anchors.topMargin: dp(2)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: dp(4)
        z: 20

        // Uniform tab width: divide available space equally
        property real tabWidth: hubModel.length > 0
            ? (parent.width - dp(4) * (hubModel.length - 1)) / hubModel.length
            : dp(80)

        Repeater {
            model: hubModel.length

            Rectangle {
                property bool isCurrent: index === swipeView.currentIndex

                width: tabStrip.tabWidth
                height: dp(28)
                radius: dpMin(App.EnvironmentTheme.active.chipRadius === -1
                    ? 6 : App.EnvironmentTheme.active.chipRadius, 2)

                color: isCurrent
                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                    : tabArea.containsMouse
                        ? Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.08)
                        : "transparent"

                border.width: isCurrent ? 1 : 0
                border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)

                Behavior on color { ColorAnimation { duration: 150 } }

                // Icon + name. The glyph used to be concatenated into the label
                // text; it is a tinted SVG now, so it needs its own item.
                Row {
                    id: tabLabel
                    anchors.centerIn: parent
                    spacing: dp(6)
                    opacity: isCurrent ? 1.0 : 0.6

                    Behavior on opacity { NumberAnimation { duration: 150 } }

                    App.ThemedIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        width: App.Spacing.overallText * 0.9
                        height: width
                        shadow: false
                        source: hubModel[index] ? (hubModel[index].iconSource || "") : ""
                        color: isCurrent ? App.Style.accent : App.Style.secondaryTextColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (hubModel[index] ? hubModel[index].name : "")
                        color: isCurrent ? App.Style.accent : App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                        font.bold: isCurrent
                        font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
                        font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase

                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                }

                // Update notification dot
                Rectangle {
                    width: dp(7)
                    height: dp(7)
                    radius: width / 2
                    color: "#FF9800"
                    z: 10
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: dp(-2)
                    anchors.rightMargin: dp(-2)
                    visible: hubModel[index] && hubModel[index].section === "about"
                             && settingsMenu && settingsMenu.updateAvailable

                    SequentialAnimation on opacity {
                        running: hubModel[index] && hubModel[index].section === "about"
                                 && settingsMenu && settingsMenu.updateAvailable
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                        NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                    }
                }

                MouseArea {
                    id: tabArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: swipeView.currentIndex = index
                }
            }
        }
    }

    // ─── SwipeView: swipe left/right between settings pages ───
    SwipeView {
        id: swipeView
        anchors.top: tabStrip.bottom
        anchors.topMargin: dp(2)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        // Track current section and persist it
        onCurrentIndexChanged: {
            if (hubModel[currentIndex] && settingsMenu) {
                settingsMenu.currentSection = hubModel[currentIndex].section
                if (settingsManager)
                    settingsManager.set_last_settings_section(hubModel[currentIndex].section)
            }
        }

        Repeater {
            model: hubModel.length

            SettingsHubCard {
                property var itemData: hubModel[index] || {}

                categoryName: itemData.name || ""
                section: itemData.section || ""
                pageSource: itemData.source || ""
                categoryIcon: itemData.icon || ""
                groupName: itemData.group || ""
                isCenter: SwipeView.isCurrentItem
                radius: dpMin(App.EnvironmentTheme.active.hubCardRadius, 2)
                settingsMenu: carouselLayout.settingsMenu
            }
        }
    }
}
