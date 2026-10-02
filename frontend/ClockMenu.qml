import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: clockMenu
    required property StackView stackView
    required property ApplicationWindow mainWindow
    width: parent.width
    height: parent.height

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // The page keeps its own 1 s tick instead of listening to clock.timeChanged:
    // that signal only fires when the bottom-bar string changes (once a minute
    // with seconds hidden, never with the clock hidden), so a freshly opened
    // page sat blank until the next minute and the analog hands jumped once a
    // minute. Running only while visible keeps it free when the page is cached.
    property date now: new Date()
    readonly property bool use24Hour: settingsManager ? settingsManager.clockFormat24Hour : true
    readonly property bool showSeconds: settingsManager ? settingsManager.clockShowSeconds : false

    Timer {
        interval: 1000
        repeat: true
        running: clockMenu.visible
        triggeredOnStart: true
        onTriggered: clockMenu.now = new Date()
    }

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        ColumnLayout {
            anchors.centerIn: parent
            spacing: dp(24)

            // Analog clock face
            Rectangle {
                id: clockFace
                Layout.alignment: Qt.AlignCenter
                width: Math.min(dp(240), clockMenu.height * 0.45)
                height: width
                radius: width/2
                color: App.Style.contentColor
                border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.6)
                border.width: dpMin(3, 1)

                // Hour ticks (major every 3 hours)
                Repeater {
                    model: 12
                    Item {
                        required property int index
                        anchors.fill: parent
                        rotation: index * 30
                        Rectangle {
                            readonly property bool major: index % 3 === 0
                            width: major ? dpMin(4, 2) : dpMin(2, 1)
                            height: clockFace.height * (major ? 0.09 : 0.05)
                            radius: width / 2
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: clockFace.height * 0.05
                            color: major ? App.Style.primaryTextColor : App.Style.secondaryTextColor
                            antialiasing: true
                        }
                    }
                }

                // Hour hand
                Rectangle {
                    width: dpMin(6, 2)
                    height: parent.height * 0.26
                    radius: width / 2
                    color: App.Style.primaryTextColor
                    antialiasing: true
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.verticalCenter
                    }
                    transformOrigin: Item.Bottom
                    rotation: (clockMenu.now.getHours() % 12) * 30 + clockMenu.now.getMinutes() * 0.5
                }

                // Minute hand
                Rectangle {
                    width: dpMin(4, 2)
                    height: parent.height * 0.38
                    radius: width / 2
                    color: App.Style.primaryTextColor
                    antialiasing: true
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.verticalCenter
                    }
                    transformOrigin: Item.Bottom
                    rotation: clockMenu.now.getMinutes() * 6 + clockMenu.now.getSeconds() * 0.1
                }

                // Second hand
                Rectangle {
                    width: dpMin(2, 1)
                    height: parent.height * 0.42
                    color: App.Style.accent
                    antialiasing: true
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.verticalCenter
                    }
                    transformOrigin: Item.Bottom
                    rotation: clockMenu.now.getSeconds() * 6
                }

                // Center dot
                Rectangle {
                    width: dpMin(10, 4)
                    height: width
                    radius: width / 2
                    color: App.Style.accent
                    anchors.centerIn: parent
                }
            }

            // Digital Clock
            Text {
                Layout.alignment: Qt.AlignCenter
                color: App.Style.clockTextColor
                text: Qt.formatTime(clockMenu.now,
                                    (clockMenu.use24Hour ? "HH:mm" : "h:mm")
                                    + (clockMenu.showSeconds ? ":ss" : "")
                                    + (clockMenu.use24Hour ? "" : " AP"))
                font {
                    pixelSize: dp(64)
                    family: clockMenu.globalFont
                    bold: true
                }
            }

            // Date display
            Text {
                Layout.alignment: Qt.AlignCenter
                color: App.Style.secondaryTextColor
                text: Qt.formatDate(clockMenu.now, "dddd, MMMM d, yyyy")
                font {
                    pixelSize: dp(22)
                    family: clockMenu.globalFont
                }
            }
        }
    }
}
