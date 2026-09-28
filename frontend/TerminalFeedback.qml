import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

// Reusable terminal-style feedback component for displaying backend progress
// Usage: Add to settings or any page where backend feedback is needed
// Call appendLine(text) to add lines, clear() to reset
// Touch-friendly with flick scrolling

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: terminalFeedback

    // Configurable properties
    property int maxLines: 50           // Maximum lines to keep in buffer
    property bool autoScroll: true      // Auto-scroll to bottom on new lines
    property string title: "Output"     // Header title
    property bool showHeader: true      // Show/hide header
    property color terminalBackground: "#1a1a1a"
    property color terminalBorder: "#333333"
    property color textColor: "#00ff00"  // Classic terminal green
    property color errorColor: "#ff4444"
    property color infoColor: "#4a9eff"
    property color successColor: "#44ff44"
    property color warningColor: "#ffaa00"
    property real fontSize: App.Spacing.overallText * 0.85

    // Internal state
    property var lines: []

    color: terminalBackground
    border.width: App.EnvironmentTheme.active.terminalAccentBorder ? 1 : 1
    border.color: App.EnvironmentTheme.active.terminalAccentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
        : terminalBorder
    radius: dpMin(App.EnvironmentTheme.active.terminalRadius, 2)
    clip: true

    // Default size (can be overridden)
    implicitHeight: dp(150)
    implicitWidth: dp(300)

    CornerBrackets {
        visible: App.EnvironmentTheme.active.terminalCornerBrackets
    }

    // Scanline overlay (spacecraft) — static CRT-style horizontal lines
    Canvas {
        anchors.fill: parent
        visible: App.EnvironmentTheme.active.terminalScanlines
        opacity: 0.04
        z: 2
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = "white"
            ctx.lineWidth = 1
            for (var y = 0; y < height; y += 3) {
                ctx.beginPath()
                ctx.moveTo(0, y + 0.5)
                ctx.lineTo(width, y + 0.5)
                ctx.stroke()
            }
        }
    }

    // Top-edge accent highlight (spacecraft)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: dp(App.EnvironmentTheme.active.terminalRadius)
        anchors.rightMargin: dp(App.EnvironmentTheme.active.terminalRadius)
        height: 1
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
        visible: App.EnvironmentTheme.active.terminalAccentBorder
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // Header bar
        Rectangle {
            id: headerBar
            Layout.fillWidth: true
            Layout.preferredHeight: showHeader ? dp(36) : 0  // Larger for touch
            visible: showHeader
            color: App.EnvironmentTheme.active.terminalHeaderAccent
                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                : "#2d2d2d"

            // Bottom accent line under header (spacecraft)
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: App.EnvironmentTheme.active.terminalHeaderAccent
                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                    : Qt.rgba(1, 1, 1, 0.05)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dp(12)
                anchors.rightMargin: dp(12)

                Text {
                    text: App.EnvironmentTheme.active.terminalHeaderAccent
                        ? "// " + terminalFeedback.title.toUpperCase()
                        : terminalFeedback.title
                    color: App.EnvironmentTheme.active.terminalHeaderAccent
                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.7)
                        : "#888888"
                    font.pixelSize: App.Spacing.overallText * 1.2
                    font.family: App.Style.fontFamily
                    font.bold: true
                    font.letterSpacing: App.EnvironmentTheme.active.terminalHeaderAccent ? 1.5 : 0
                }

                Item { Layout.fillWidth: true }

                // Status indicator dot (spacecraft)
                Rectangle {
                    width: dp(6); height: dp(6)
                    radius: App.EnvironmentTheme.active.terminalRadius === 2 ? 1 : dpMin(3, 1)
                    color: terminalFeedback.lines.length > 0 ? App.Style.accent : "#555555"
                    visible: App.EnvironmentTheme.active.terminalHeaderAccent

                    // Pulsing when active
                    SequentialAnimation on opacity {
                        running: App.EnvironmentTheme.active.terminalHeaderAccent && terminalFeedback.lines.length > 0 && terminalFeedback.visible  // visible includes hidden pages/sections
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.4; duration: 1000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
                    }
                }

                // Clear button - larger touch target
                Rectangle {
                    width: App.EnvironmentTheme.active.terminalHeaderAccent ? dp(64) : dp(48)
                    height: dp(30)
                    radius: dpMin(App.EnvironmentTheme.active.terminalRadius, 2)
                    color: clearMouseArea.pressed
                        ? (App.EnvironmentTheme.active.terminalHeaderAccent
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                            : "#555555")
                        : clearMouseArea.containsMouse
                            ? (App.EnvironmentTheme.active.terminalHeaderAccent
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                                : "#444444")
                            : (App.EnvironmentTheme.active.terminalHeaderAccent
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.1)
                                : "#333333")

                    border.width: App.EnvironmentTheme.active.terminalAccentBorder ? 1 : 0
                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)

                    Text {
                        anchors.centerIn: parent
                        text: App.EnvironmentTheme.active.terminalHeaderAccent ? "CLR" : "Clear"
                        color: App.EnvironmentTheme.active.terminalHeaderAccent
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                            : "#aaaaaa"
                        font.pixelSize: dp(12)
                        font.bold: true
                        font.letterSpacing: App.EnvironmentTheme.active.terminalHeaderAccent ? 1 : 0
                    }

                    MouseArea {
                        id: clearMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: terminalFeedback.clear()
                    }
                }
            }
        }

        // Terminal content area - touch-friendly Flickable
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: flickable
                anchors.fill: parent
                anchors.margins: dp(8)
                contentWidth: width
                contentHeight: contentColumn.height
                clip: true

                // Touch-friendly flicking
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                pressDelay: 0  // Immediate response for touch

                // Momentum and deceleration for smooth touch scrolling
                flickDeceleration: 1500
                maximumFlickVelocity: 4000

                // Content column with all lines
                Column {
                    id: contentColumn
                    width: flickable.width
                    spacing: dp(4)

                    Repeater {
                        model: terminalFeedback.lines

                        Text {
                            width: contentColumn.width
                            text: modelData.text
                            color: modelData.color || terminalFeedback.textColor
                            font.pixelSize: terminalFeedback.fontSize
                            font.family: "Consolas, Monaco, monospace"
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            textFormat: Text.PlainText
                            lineHeight: 1.3  // Better readability
                        }
                    }
                }

                // Auto-scroll to bottom when content changes
                onContentHeightChanged: {
                    if (terminalFeedback.autoScroll && contentHeight > height) {
                        scrollToBottomAnimation.to = contentHeight - height
                        scrollToBottomAnimation.start()
                    }
                }

                // Smooth scroll animation
                NumberAnimation {
                    id: scrollToBottomAnimation
                    target: flickable
                    property: "contentY"
                    duration: 150
                    easing.type: Easing.OutQuad
                }
            }

            // Vertical scroll indicator (touch-friendly, visible when scrolling)
            Rectangle {
                id: scrollIndicator
                anchors.right: parent.right
                anchors.rightMargin: dp(2)
                width: App.EnvironmentTheme.active.terminalAccentScroll ? dp(2) : dp(4)
                radius: App.EnvironmentTheme.active.terminalAccentScroll ? 1 : 2
                color: App.EnvironmentTheme.active.terminalAccentScroll
                    ? App.Style.accent : "#666666"
                opacity: flickable.moving ? 0.8 : (flickable.contentHeight > flickable.height ? 0.3 : 0)
                visible: flickable.contentHeight > flickable.height

                // Calculate position and size
                y: {
                    var ratio = flickable.contentY / (flickable.contentHeight - flickable.height)
                    var trackHeight = flickable.height - height
                    return Math.max(0, Math.min(trackHeight, ratio * trackHeight)) + dp(8)
                }
                height: Math.max(dp(30), (flickable.height / flickable.contentHeight) * (flickable.height - dp(16)))

                Behavior on opacity {
                    NumberAnimation { duration: 200 }
                }
            }
        }
    }

    // Public function to append a line with automatic color detection
    function appendLine(text) {
        var lineColor = textColor

        // Auto-detect color based on prefix
        if (text.indexOf("[ERROR]") === 0) {
            lineColor = errorColor
        } else if (text.indexOf("[DONE]") === 0 || text.indexOf("[SUCCESS]") === 0) {
            lineColor = successColor
        } else if (text.indexOf("[INFO]") === 0 || text.indexOf("[SCAN]") === 0 || text.indexOf("[CLEAR]") === 0) {
            lineColor = infoColor
        } else if (text.indexOf("[WARN]") === 0 || text.indexOf("[WARNING]") === 0) {
            lineColor = warningColor
        } else if (text.indexOf("[FOUND]") === 0 || text.indexOf("[OK]") === 0) {
            lineColor = successColor
        } else if (text.indexOf("[PATH]") === 0) {
            lineColor = "#aaaaaa"  // Gray for paths
        }

        // Add timestamp
        var now = new Date()
        var timestamp = now.toLocaleTimeString(Qt.locale(), "hh:mm:ss")
        var formattedText = "[" + timestamp + "] " + text

        // Create new array with the new line
        var newLines = lines.slice()
        newLines.push({ text: formattedText, color: lineColor })

        // Trim to max lines
        while (newLines.length > maxLines) {
            newLines.shift()
        }

        lines = newLines
    }

    // Public function to append a line with specific color
    function appendLineWithColor(text, color) {
        var now = new Date()
        var timestamp = now.toLocaleTimeString(Qt.locale(), "hh:mm:ss")
        var formattedText = "[" + timestamp + "] " + text

        var newLines = lines.slice()
        newLines.push({ text: formattedText, color: color })

        while (newLines.length > maxLines) {
            newLines.shift()
        }

        lines = newLines
    }

    // Public function to clear all lines
    function clear() {
        lines = []
    }
}
