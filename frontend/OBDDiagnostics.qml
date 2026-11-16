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

    // One request to the car. Not connected: say so and ask nothing (the
    // manager answers an unconnected read with an empty list, which read
    // here as "No trouble codes found", and other requests returned without
    // a word, leaving the spinner on).
    function request(status, line, send) {
        if (!obdManager || !obdManager.connected) {
            isLoading = false
            statusMessage = "Not connected"
            terminalOutput.appendLine("[WARN] OBD not connected - nothing was read")
            return
        }
        isLoading = true
        statusMessage = status
        terminalOutput.appendLine(line)
        send()
    }

    // No answer at all (link lost mid-request): the spinner does not turn forever
    Timer {
        interval: 20000
        running: obdDiagnosticsPage.isLoading
        onTriggered: {
            obdDiagnosticsPage.isLoading = false
            obdDiagnosticsPage.statusMessage = "No answer"
            terminalOutput.appendLine("[WARN] No answer from the vehicle")
        }
    }

    // Diagnostic mode pauses live polling, so it is entered only with the
    // page on screen: the page is also built ahead of time, hidden (prewarm),
    // and entering there stopped the gauges behind the user's back.
    property bool _statusRead: false
    function enterDiagnostics() {
        if (!obdManager)
            return
        if (!obdManager.connected) {
            terminalOutput.appendLine("[WARN] OBD not connected - diagnostic commands unavailable")
            terminalOutput.appendLine("[INFO] Connect to vehicle first, then return to this menu")
            return
        }
        if (!obdManager.isDiagnosticMode()) {
            terminalOutput.appendLine("[INFO] Entering diagnostic mode...")
            obdManager.enterDiagnosticMode()
        }
        if (!_statusRead) {
            _statusRead = true
            request("Reading vehicle status...", "[SCAN] Reading vehicle status...", function() { obdManager.readStatus() })
        }
    }

    // OBD Diagnostic Connections
    Connections {
        target: obdManager
        enabled: obdManager !== null

        function onDtcCodesChanged(codes) {
            dtcCodes = codes
            isLoading = false
            statusMessage = codes.length > 0 ? "Found " + codes.length + " DTC(s)" : "No DTCs found"

            // Output to terminal
            if (codes && codes.length > 0) {
                terminalOutput.appendLine("[FOUND] " + codes.length + " DTC(s) detected:")
                for (var i = 0; i < codes.length; i++) {
                    var dtc = codes[i]
                    terminalOutput.appendLine("[DTC] " + dtc.code + " - " + dtc.description)
                }
            } else if (obdManager && obdManager.connected) {
                terminalOutput.appendLine("[DONE] No trouble codes found")
            } else {
                statusMessage = "Not connected"
                terminalOutput.appendLine("[WARN] OBD not connected - no codes were read")
            }
        }

        function onDtcCountChanged(count) {
            dtcCount = count
            terminalOutput.appendLine("[INFO] DTC count: " + count)
        }

        function onMilStatusChanged(status) {
            milStatus = status
            isLoading = false
            if (status) {
                terminalOutput.appendLine("[WARN] Check Engine Light is ON")
            } else {
                terminalOutput.appendLine("[DONE] Check Engine Light is OFF")
            }
        }

        function onDtcClearResult(success, message) {
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
            enterDiagnostics()
        } else if (obdManager && obdManager.isDiagnosticMode()) {
            // Leaving the page (it stays alive in the page cache)
            obdManager.exitDiagnosticMode()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: App.Spacing.overallMargin
            spacing: dp(15)


            // 4x2 Grid - 4 buttons on top, terminal + clear on bottom.
            // Portrait: the buttons 2x2, Clear DTCs across, the terminal below.
            GridLayout {
                id: diagGrid
                readonly property bool portrait: App.Spacing.isPortrait
                // Height of a button row in portrait; the terminal takes the rest
                readonly property real buttonRowHeight: Math.max(dp(64), height * 0.13)
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: portrait ? 2 : 4
                rows: portrait ? 4 : 2
                rowSpacing: dp(15)
                columnSpacing: dp(15)

                // Row 1: Read DTCs, Current DTCs, Read Status, Freeze Frame

                // Read DTCs Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: !diagGrid.portrait
                    Layout.preferredHeight: diagGrid.portrait ? diagGrid.buttonRowHeight : -1
                    Layout.row: diagGrid.portrait ? 0 : 0
                    Layout.column: diagGrid.portrait ? 0 : 0
                    radius: dpMin(12, 2)
                    color: mouseAreaReadDTC.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: Math.min(dp(8), parent.height * 0.06)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Read DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16, parent.parent.height * 0.34)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Stored trouble codes"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09, parent.parent.height * 0.19)
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
                            obdDiagnosticsPage.request("Reading DTCs...", "[SCAN] Reading stored DTCs...", function() { obdManager.readDtc() })
                        }
                    }
                }

                // Read Current DTCs Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: !diagGrid.portrait
                    Layout.preferredHeight: diagGrid.portrait ? diagGrid.buttonRowHeight : -1
                    Layout.row: diagGrid.portrait ? 0 : 0
                    Layout.column: diagGrid.portrait ? 1 : 1
                    radius: dpMin(12, 2)
                    color: mouseAreaReadCurrentDTC.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: Math.min(dp(8), parent.height * 0.06)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Current DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16, parent.parent.height * 0.34)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Active this drive cycle"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09, parent.parent.height * 0.19)
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
                            obdDiagnosticsPage.request("Reading current DTCs...", "[SCAN] Reading current drive cycle DTCs...", function() { obdManager.readCurrentDtc() })
                        }
                    }
                }

                // Read Status Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: !diagGrid.portrait
                    Layout.preferredHeight: diagGrid.portrait ? diagGrid.buttonRowHeight : -1
                    Layout.row: diagGrid.portrait ? 1 : 0
                    Layout.column: diagGrid.portrait ? 0 : 2
                    radius: dpMin(12, 2)
                    color: mouseAreaReadStatus.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: Math.min(dp(8), parent.height * 0.06)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Read Status"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16, parent.parent.height * 0.34)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "CEL & DTC count"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09, parent.parent.height * 0.19)
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
                            obdDiagnosticsPage.request("Reading vehicle status...", "[SCAN] Reading vehicle status...", function() { obdManager.readStatus() })
                        }
                    }
                }

                // Freeze Frame Button (row 0, column 3)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: !diagGrid.portrait
                    Layout.preferredHeight: diagGrid.portrait ? diagGrid.buttonRowHeight : -1
                    Layout.row: diagGrid.portrait ? 1 : 0
                    Layout.column: diagGrid.portrait ? 1 : 3
                    radius: dpMin(12, 2)
                    color: mouseAreaFreezeFrame.pressed ? Qt.darker(accentColor, 1.2) : accentColor

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: Math.min(dp(8), parent.height * 0.06)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Freeze Frame"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16, parent.parent.height * 0.34)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Snapshot at fault"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09, parent.parent.height * 0.19)
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
                            obdDiagnosticsPage.request("Reading freeze frame...", "[SCAN] Reading freeze frame data...", function() { obdManager.readFreezeFrame() })
                        }
                    }
                }

                // Row 2: Terminal Results Panel (3 cols), Clear DTCs (1 col)

                // Terminal-style Results Panel (spans 3 columns)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.row: diagGrid.portrait ? 3 : 1
                    Layout.column: 0
                    Layout.columnSpan: diagGrid.portrait ? 2 : 3
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
                        }

                        function clear() {
                            lines = []
                        }
                    }
                }

                // Clear DTCs Button (row 1, column 3 - bottom right; portrait: full width under the buttons)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: !diagGrid.portrait
                    Layout.preferredHeight: diagGrid.portrait ? diagGrid.buttonRowHeight : -1
                    Layout.row: diagGrid.portrait ? 2 : 1
                    Layout.column: diagGrid.portrait ? 0 : 3
                    Layout.columnSpan: diagGrid.portrait ? 2 : 1
                    radius: dpMin(12, 2)
                    color: mouseAreaClearDTC.pressed ? Qt.darker("#CC0000", 1.2) : "#CC0000"

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - dp(16)
                        spacing: Math.min(dp(8), parent.height * 0.06)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Clear DTCs"
                            font.pixelSize: Math.min(dp(32), parent.width * 0.16, parent.parent.height * 0.34)
                            font.bold: true
                            font.family: obdDiagnosticsPage.globalFont
                            color: "white"
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "Reset CEL & codes"
                            font.pixelSize: Math.min(dp(18), parent.width * 0.09, parent.parent.height * 0.19)
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
            obdDiagnosticsPage.request("Clearing DTCs...", "[CLEAR] Clearing all DTCs and resetting CEL...", function() { obdManager.clearDtc() })
        }
    }

    // Shown as soon as it is built (not prewarmed): onVisibleChanged does not fire
    Component.onCompleted: if (visible) enterDiagnostics()

    // Fallback: Exit diagnostic mode when component is destroyed
    // Note: onVisibleChanged handles the normal StackView navigation case,
    // but this catches the case when the component is actually destroyed
    Component.onDestruction: {
        if (obdManager && obdManager.isDiagnosticMode()) {
            obdManager.exitDiagnosticMode()
        }
    }
}
