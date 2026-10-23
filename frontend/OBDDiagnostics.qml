import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdDiagnosticsPage
    objectName: "obdDiagnostics"
    required property StackView stackView
    required property ApplicationWindow mainWindow

    // Global font binding for all text in this component
    property string globalFont: App.Style.fontFamily

    // Define background and accent colors matching OBDMenu
    property color backgroundColor: App.Style.obdBoxBackground
    property color accentColor: App.Style.obdBarColor
    property color textColor: App.Style.primaryTextColor

    // Diagnostic data storage
    property var dtcCodes: []
    property int dtcCount: 0
    property bool milStatus: false
    property var freezeFrameData: []
    property bool isLoading: false
    property string statusMessage: ""

    // OBD Diagnostic Connections
    Connections {
        target: obdManager
        enabled: obdManager !== null

        function onDtcCodesChanged(codes) {
            console.log("[OBDDiagnostics] onDtcCodesChanged SIGNAL RECEIVED, count:", codes ? codes.length : "null")
            dtcCodes = codes
            isLoading = false
            statusMessage = codes.length > 0 ? "Found " + codes.length + " DTC(s)" : "No DTCs found"

            // Output to terminal
            if (codes && codes.length > 0) {
                terminalOutput.appendLine("[FOUND] " + codes.length + " DTC(s) detected:")
                for (var i = 0; i < codes.length; i++) {
                    var dtc = codes[i]
                    console.log("[OBDDiagnostics] DTC:", dtc.code, "-", dtc.description)
                    terminalOutput.appendLine("[DTC] " + dtc.code + " - " + dtc.description)
                }
            } else {
                terminalOutput.appendLine("[DONE] No trouble codes found")
            }
            console.log("[OBDDiagnostics] Terminal lines count:", terminalOutput.lines.length)
        }

        function onDtcCountChanged(count) {
            console.log("[OBDDiagnostics] onDtcCountChanged SIGNAL RECEIVED:", count)
            dtcCount = count
            terminalOutput.appendLine("[INFO] DTC count: " + count)
        }

        function onMilStatusChanged(status) {
            console.log("[OBDDiagnostics] onMilStatusChanged SIGNAL RECEIVED:", status)
            milStatus = status
            isLoading = false
            if (status) {
                terminalOutput.appendLine("[WARN] Check Engine Light is ON")
            } else {
                terminalOutput.appendLine("[DONE] Check Engine Light is OFF")
            }
        }

        function onDtcClearResult(success, message) {
            console.log("[OBDDiagnostics] onDtcClearResult SIGNAL RECEIVED:", success, message)
            isLoading = false
            statusMessage = message
            if (success) {
                dtcCodes = []
                dtcCount = 0
                milStatus = false
                terminalOutput.appendLine("[SUCCESS] " + message)
            } else {
                terminalOutput.appendLine("[ERROR] " + message)
            }
        }

        function onFreezeFrameChanged(data) {
            console.log("[OBDDiagnostics] onFreezeFrameChanged SIGNAL RECEIVED, count:", data ? data.length : "null")
            freezeFrameData = data
            isLoading = false

            if (data && data.length > 0) {
                terminalOutput.appendLine("[FOUND] Freeze frame data:")
                for (var i = 0; i < data.length; i++) {
                    var ff = data[i]
                    terminalOutput.appendLine("[DTC] " + ff.code + " - " + ff.description)
                    // What the car recorded when it stored the code, in the user's units
                    var vals = ff.values || {}
                    for (var id in vals) {
                        var info = App.OBDParameterModel.getParamInfo(id)
                        var v = App.OBDParameterModel.toDisplay(id, vals[id])
                        terminalOutput.appendLine("      " + info.title + ": "
                                                  + (typeof v === "number" ? Number(v).toFixed(info.decimals || 0) : v)
                                                  + (info.unit ? " " + info.unit : ""))
                    }
                }
            } else {
                terminalOutput.appendLine("[DONE] No freeze frame data stored")
            }
        }
    }

    // Handle diagnostic mode based on visibility
    // This is crucial because StackView.push() doesn't destroy the page,
    // so Component.onDestruction won't be called when navigating away
    onVisibleChanged: {
        if (visible) {
            console.log("[OBDDiagnostics] Page became visible, obdManager exists:", obdManager !== null)
            // Re-enter diagnostic mode if we're becoming visible again
            if (obdManager && obdManager.is_connected() && !obdManager.is_diagnostic_mode()) {
                console.log("[OBDDiagnostics] Re-entering diagnostic mode on visibility")
                terminalOutput.appendLine("[INFO] Re-entering diagnostic mode...")
                obdManager.enter_diagnostic_mode()
            }
        } else {
            console.log("[OBDDiagnostics] Page became hidden")
            // Exit diagnostic mode when page becomes hidden (navigating away)
            if (obdManager && obdManager.is_diagnostic_mode()) {
                console.log("[OBDDiagnostics] Exiting diagnostic mode on hide")
                obdManager.exit_diagnostic_mode()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: App.Spacing.overallMargin
            spacing: dp(15)


            // 4x2 Grid - 4 buttons on top, terminal + clear on bottom
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 4
                rows: 2
                rowSpacing: dp(15)
                columnSpacing: dp(15)

                // Row 1: Read DTCs, Current DTCs, Read Status, Freeze Frame

                // Read DTCs Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 0
                    Layout.column: 0
                    radius: dpMin(12, 2)
                    color: mouseAreaReadDTC.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: dp(8)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Read DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Stored trouble codes"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09)
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            opacity: 0.8
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: mouseAreaReadDTC
                        anchors.fill: parent
                        onClicked: {
                            isLoading = true
                            statusMessage = "Reading DTCs..."
                            terminalOutput.appendLine("[SCAN] Reading stored DTCs...")
                            obdManager.read_dtc()
                        }
                    }
                }

                // Read Current DTCs Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 0
                    Layout.column: 1
                    radius: dpMin(12, 2)
                    color: mouseAreaReadCurrentDTC.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: dp(8)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Current DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Active this drive cycle"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09)
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            opacity: 0.8
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: mouseAreaReadCurrentDTC
                        anchors.fill: parent
                        onClicked: {
                            isLoading = true
                            statusMessage = "Reading current DTCs..."
                            terminalOutput.appendLine("[SCAN] Reading current drive cycle DTCs...")
                            obdManager.read_current_dtc()
                        }
                    }
                }

                // Read Status Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 0
                    Layout.column: 2
                    radius: dpMin(12, 2)
                    color: mouseAreaReadStatus.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: dp(8)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Read Status"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "CEL & DTC count"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09)
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            opacity: 0.8
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: mouseAreaReadStatus
                        anchors.fill: parent
                        onClicked: {
                            isLoading = true
                            statusMessage = "Reading vehicle status..."
                            terminalOutput.appendLine("[SCAN] Reading vehicle status...")
                            obdManager.read_status()
                        }
                    }
                }

                // Freeze Frame Button (row 0, column 3)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 0
                    Layout.column: 3
                    radius: dpMin(12, 2)
                    color: mouseAreaFreezeFrame.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: dp(8)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Freeze Frame"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Snapshot at fault"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09)
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            opacity: 0.8
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: mouseAreaFreezeFrame
                        anchors.fill: parent
                        onClicked: {
                            isLoading = true
                            statusMessage = "Reading freeze frame..."
                            terminalOutput.appendLine("[SCAN] Reading freeze frame data...")
                            obdManager.read_freeze_frame()
                        }
                    }
                }

                // Row 2: Terminal Results Panel (3 cols), Clear DTCs (1 col)

                // Terminal-style Results Panel (spans 3 columns)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 1
                    Layout.column: 0
                    Layout.columnSpan: 3
                    radius: dpMin(12, 2)
                    color: "#1a1a1a"
                    border.color: "#333333"
                    border.width: 2
                    clip: true

                    // Scanline overlay effect
                    Rectangle {
                        anchors.fill: parent
                        radius: dpMin(12, 2)
                        color: "transparent"
                        z: 10

                        // CRT scanline effect
                        Canvas {
                            id: scanlineCanvas
                            anchors.fill: parent
                            opacity: 0.03

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = "#000000"
                                ctx.lineWidth = 1
                                for (var y = 0; y < height; y += 2) {
                                    ctx.beginPath()
                                    ctx.moveTo(0, y)
                                    ctx.lineTo(width, y)
                                    ctx.stroke()
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0

                        // Terminal header bar
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: dp(32)
                            color: "#2d2d2d"
                            radius: dpMin(12, 2)

                            // Bottom corners not rounded
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: dp(12)
                                color: parent.color
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: dp(12)
                                anchors.rightMargin: dp(12)

                                Text {
                                    text: "DIAGNOSTIC OUTPUT"
                                    color: "#888888"
                                    font.pixelSize: dp(18)
                                    font.family: "Consolas, Monaco, monospace"
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                BusyIndicator {
                                    Layout.preferredWidth: dp(16)
                                    Layout.preferredHeight: dp(16)
                                    running: isLoading
                                    visible: isLoading
                                }
                            }
                        }

                        // Terminal content
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Flickable {
                                id: terminalFlickable
                                anchors.fill: parent
                                anchors.margins: dp(8)
                                contentWidth: width
                                contentHeight: terminalContent.height
                                clip: true
                                flickableDirection: Flickable.VerticalFlick
                                boundsBehavior: Flickable.StopAtBounds

                                Column {
                                    id: terminalContent
                                    width: terminalFlickable.width
                                    spacing: dp(3)

                                    Repeater {
                                        model: terminalOutput.lines

                                        Text {
                                            width: terminalContent.width
                                            text: modelData.text
                                            color: modelData.color || "#00ff00"
                                            font.pixelSize: dp(18)
                                            font.family: "Consolas, Monaco, monospace"
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        }
                                    }
                                }

                                onContentHeightChanged: {
                                    if (contentHeight > height) {
                                        contentY = contentHeight - height
                                    }
                                }
                            }

                            // Empty state
                            Text {
                                anchors.centerIn: parent
                                visible: terminalOutput.lines.length === 0 && !isLoading
                                text: "> Awaiting command..."
                                font.pixelSize: dp(20)
                                font.family: "Consolas, Monaco, monospace"
                                color: "#00ff00"
                                opacity: 0.5
                            }

                            // Cursor blink effect
                            Rectangle {
                                visible: terminalOutput.lines.length === 0 && !isLoading
                                anchors.left: parent.horizontalCenter
                                anchors.leftMargin: dp(70)
                                anchors.verticalCenter: parent.verticalCenter
                                width: dp(8)
                                height: dp(14)
                                color: "#00ff00"
                                opacity: cursorTimer.blink ? 0.8 : 0

                                Timer {
                                    id: cursorTimer
                                    property bool blink: true
                                    interval: 530
                                    repeat: true
                                    running: terminalOutput.lines.length === 0
                                    onTriggered: blink = !blink
                                }
                            }
                        }
                    }

                    // Terminal output data handler
                    QtObject {
                        id: terminalOutput
                        property var lines: []
                        property int maxLines: 50

                        function appendLine(text) {
                            console.log("[Terminal] appendLine called with:", text)
                            var lineColor = "#00ff00"  // Default green

                            if (text.indexOf("[ERROR]") === 0) {
                                lineColor = "#ff4444"
                            } else if (text.indexOf("[DONE]") === 0 || text.indexOf("[SUCCESS]") === 0) {
                                lineColor = "#44ff44"
                            } else if (text.indexOf("[INFO]") === 0 || text.indexOf("[SCAN]") === 0) {
                                lineColor = "#4a9eff"
                            } else if (text.indexOf("[WARN]") === 0) {
                                lineColor = "#ffaa00"
                            } else if (text.indexOf("[FOUND]") === 0) {
                                lineColor = "#44ff44"
                            } else if (text.indexOf("[CLEAR]") === 0) {
                                lineColor = "#ff6600"
                            } else if (text.indexOf("[DTC]") === 0) {
                                lineColor = "#ff6600"
                            }

                            var now = new Date()
                            var timestamp = now.toLocaleTimeString(Qt.locale(), "hh:mm:ss")
                            var formattedText = "[" + timestamp + "] " + text

                            var newLines = lines.slice()
                            newLines.push({ text: formattedText, color: lineColor })

                            while (newLines.length > maxLines) {
                                newLines.shift()
                            }

                            lines = newLines
                            console.log("[Terminal] lines now has", lines.length, "items")
                        }

                        function clear() {
                            lines = []
                        }
                    }
                }

                // Clear DTCs Button (row 1, column 3 - bottom right)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: 1
                    Layout.column: 3
                    radius: dpMin(12, 2)
                    color: mouseAreaClearDTC.pressed ? Qt.darker("#CC0000", 1.2) : "#CC0000"

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: dp(8)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Clear DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Reset CEL & codes"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09)
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            opacity: 0.8
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: mouseAreaClearDTC
                        anchors.fill: parent
                        onClicked: {
                            clearConfirmDialog.open()
                        }
                    }
                }
            }
        }
    }

    // Clear DTCs Confirmation Dialog
    Dialog {
        id: clearConfirmDialog
        title: "Clear Diagnostic Trouble Codes"
        anchors.centerIn: parent
        width: dp(400)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        background: Rectangle {
            color: App.Style.backgroundColor
            radius: dpMin(8, 2)
            border.color: accentColor
            border.width: 2
        }

        contentItem: ColumnLayout {
            spacing: dp(15)

            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: dp(360)
                text: "Are you sure you want to clear all DTCs?"
                font.pixelSize: dp(16)
                font.family: obdDiagnosticsPage.globalFont
                color: obdDiagnosticsPage.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "This will also reset the Check Engine Light and clear freeze frame data."
                font.pixelSize: dp(14)
                font.family: obdDiagnosticsPage.globalFont
                color: obdDiagnosticsPage.textColor
                opacity: 0.7
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "Warning: This may affect emissions testing readiness."
                font.pixelSize: dp(13)
                font.family: obdDiagnosticsPage.globalFont
                color: "#FF6600"
                wrapMode: Text.WordWrap
            }
        }

        onAccepted: {
            isLoading = true
            statusMessage = "Clearing DTCs..."
            terminalOutput.appendLine("[CLEAR] Clearing all DTCs and resetting CEL...")
            obdManager.clear_dtc()
        }
    }

    // Enter diagnostic mode on component creation (pauses async polling)
    Component.onCompleted: {
        if (obdManager) {
            if (obdManager.is_connected()) {
                terminalOutput.appendLine("[INFO] Entering diagnostic mode...")
                obdManager.enter_diagnostic_mode()
                terminalOutput.appendLine("[INFO] Diagnostic mode initialized")
                terminalOutput.appendLine("[SCAN] Reading vehicle status...")
                obdManager.read_status()
            } else {
                terminalOutput.appendLine("[WARN] OBD not connected - diagnostic commands unavailable")
                terminalOutput.appendLine("[INFO] Connect to vehicle first, then return to this menu")
            }
        }
    }

    // Fallback: Exit diagnostic mode when component is destroyed
    // Note: onVisibleChanged handles the normal StackView navigation case,
    // but this catches the case when the component is actually destroyed
    Component.onDestruction: {
        if (obdManager && obdManager.is_diagnostic_mode()) {
            console.log("[OBD] OBDDiagnostics component being destroyed, exiting diagnostic mode")
            obdManager.exit_diagnostic_mode()
        }
    }
}
