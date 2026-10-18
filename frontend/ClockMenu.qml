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
                    id: hourHand
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
                }

                // Minute hand
                Rectangle {
                    id: minuteHand
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
                }

                // Second hand
                Rectangle {
                    id: secondHand
                    width: dpMin(2, 1)
                    height: parent.height * 0.42
                    color: App.Style.accent
                    antialiasing: true
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.verticalCenter
                    }
                    transformOrigin: Item.Bottom
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
                id: digitalClock
                Layout.alignment: Qt.AlignCenter
                color: App.Style.clockTextColor
                font {
                    pixelSize: dp(64)
                    family: clockMenu.globalFont
                    bold: true
                }
            }

            // Date display
            Text {
                id: dateDisplay
                Layout.alignment: Qt.AlignCenter
                color: App.Style.secondaryTextColor
                font {
                    pixelSize: dp(22)
                    family: clockMenu.globalFont
                }
            }
        }
    }

    // Ticks on its own while shown rather than following clock.timeChanged:
    // that signal only fires when the text changes (once a minute without
    // seconds), so a freshly opened page stayed blank and the second hand
    // never moved. The digital text uses the same settings as the Clock
    // manager (24 h / seconds).
    function refresh() {
        var now = new Date()
        var showSeconds = settingsManager ? settingsManager.clockShowSeconds : false
        var use24h = settingsManager ? settingsManager.clockFormat24Hour : true
        // "hh" is 12-hour only when the format also has "AP"
        var fmt = (use24h ? "HH" : "hh") + ":mm" + (showSeconds ? ":ss" : "") + (use24h ? "" : " AP")
        digitalClock.text = Qt.formatTime(now, fmt)
        dateDisplay.text = Qt.formatDate(now, "dddd, MMMM d, yyyy")

        var hours = now.getHours()
        var minutes = now.getMinutes()
        var seconds = now.getSeconds()
        hourHand.rotation = (hours % 12) * 30 + (minutes / 60) * 30
        minuteHand.rotation = minutes * 6 + (seconds / 60) * 6
        secondHand.rotation = seconds * 6
    }

    // Re-aimed at the next whole second on every tick (+15 ms so it lands just
    // past the boundary). A plain 1000 ms repeat runs at whatever phase the
    // page opened with, so the hand trailed the real second by up to a second
    // and drift made it skip or repeat a tick.
    Timer {
        // Assigning interval restarts a running Timer with the new value, so
        // no restart() call (that would break the running: visible binding).
        id: tickTimer
        interval: 1000
        repeat: true
        running: clockMenu.visible
        triggeredOnStart: true
        onTriggered: {
            clockMenu.refresh()
            interval = 1015 - new Date().getMilliseconds()
        }
    }
}
