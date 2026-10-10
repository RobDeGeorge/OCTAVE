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
        color: "#2c3e50"  // Dark blue-gray background

        ColumnLayout {
            anchors.centerIn: parent
            spacing: dp(30)

            // Digital Clock
            Text {
                id: digitalClock
                Layout.alignment: Qt.AlignCenter
                color: "white"
                font {
                    pixelSize: dp(72)
                    family: clockMenu.globalFont
                    bold: true
                }
            }

            // Date display
            Text {
                id: dateDisplay
                Layout.alignment: Qt.AlignCenter
                color: "#ecf0f1"
                font {
                    pixelSize: dp(24)
                    family: clockMenu.globalFont
                }
            }

            // Analog clock face
            Rectangle {
                id: clockFace
                Layout.alignment: Qt.AlignCenter
                width: dp(200)
                height: dp(200)
                radius: width/2
                color: "transparent"
                border.color: "white"
                border.width: dp(3)

                // Hour hand
                Rectangle {
                    id: hourHand
                    width: dp(4)
                    height: parent.height * 0.3
                    color: "white"
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
                    width: dp(2)
                    height: parent.height * 0.4
                    color: "white"
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
                    width: 1
                    height: parent.height * 0.45
                    color: "#e74c3c"  // Red color
                    antialiasing: true
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.verticalCenter
                    }
                    transformOrigin: Item.Bottom
                }

                // Center dot
                Rectangle {
                    width: dp(8)
                    height: dp(8)
                    radius: 4
                    color: "#e74c3c"
                    anchors.centerIn: parent
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
