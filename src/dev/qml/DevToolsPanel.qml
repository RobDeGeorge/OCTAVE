import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../../frontend" as App

/*
    Dev Tools Panel
    ===============
    Hidden developer settings panel accessible by clicking
    the DEV MODE banner. Houses screenshot tools, diagnostics,
    and other dev-only functionality.

    Screenshot scenarios run entirely in-process using QML navigation
    and Qt's grabWindow() — no xdotool needed, works on Wayland.
*/

Rectangle {
    id: devToolsPanel

    property bool expanded: false

    // These must be set by the parent (DevMain.qml)
    property var stackView: null
    property var mainWindow: null

    width: Math.min(parent.width * 0.85, 500)
    height: expanded ? contentColumn.implicitHeight + 40 : 0

    color: Qt.rgba(0.08, 0.08, 0.12, 0.95)
    radius: 12
    border.color: "#E91E63"
    border.width: expanded ? 1 : 0
    clip: true

    Behavior on height {
        NumberAnimation {
            duration: 300
            easing.type: Easing.OutCubic
        }
    }

    // ══════════════════════════════════════════
    // Scenario runner — navigates pages via stackView
    // and captures each with grabWindow()
    // ══════════════════════════════════════════

    property var scenarioSteps: []
    property int scenarioIndex: -1
    property bool scenarioRunning: false

    function runAllPagesScenario() {
        if (!stackView || !devToolsManager) return

        devToolsManager.beginCapture()
        devToolsManager.clearScreenshots()

        // Each step: pop to root first, then push the target page
        scenarioSteps = [
            { action: "home",        name: "main_menu",       desc: "Main Menu - Dashboard" },
            { action: "mediaRoom",   name: "media_room",      desc: "Media Room - Library" },
            { action: "mediaPlayer", name: "media_player",    desc: "Media Player - Now Playing" },
            { action: "obdMenu",     name: "obd_menu",        desc: "OBD Menu - Vehicle Data" },
            { action: "obdDiag",     name: "obd_diagnostics", desc: "OBD Diagnostics" },
            { action: "settings",    name: "settings_menu",   desc: "Settings Menu" },
            { action: "home",        name: "main_menu_final", desc: "Main Menu - Final" },
        ]
        scenarioIndex = -1
        scenarioRunning = true

        devToolsManager.hideOverlaysRequested()
        scenarioTimer.interval = 500
        scenarioTimer.start()
    }

    function runSettingsScenario() {
        if (!stackView || !devToolsManager) return

        devToolsManager.beginCapture()
        devToolsManager.clearScreenshots()

        scenarioSteps = [
            { action: "settings",    name: "settings_overview", desc: "Settings Overview" },
        ]
        scenarioIndex = -1
        scenarioRunning = true

        devToolsManager.hideOverlaysRequested()
        scenarioTimer.interval = 500
        scenarioTimer.start()
    }

    function runCurrentScenario() {
        if (!devToolsManager) return
        devToolsManager.beginCapture()
        devToolsManager.hideOverlaysRequested()
        singleShotTimer.start()
    }

    Timer {
        id: singleShotTimer
        interval: 300
        repeat: false
        onTriggered: {
            devToolsManager.captureWindow("current_view", "Current View")
            devToolsManager.showOverlaysRequested()
            devToolsManager.endCapture()
        }
    }

    // Step through scenario pages one at a time
    Timer {
        id: scenarioTimer
        interval: 800
        repeat: false
        onTriggered: {
            if (!scenarioRunning) return

            // Capture the current page (skip on first tick before any navigation)
            if (scenarioIndex >= 0 && scenarioIndex < scenarioSteps.length) {
                var step = scenarioSteps[scenarioIndex]
                devToolsManager.captureWindow(step.name, step.desc)
            }

            // Advance
            scenarioIndex++

            if (scenarioIndex >= scenarioSteps.length) {
                scenarioRunning = false
                devToolsManager.showOverlaysRequested()
                devToolsManager.endCapture()
                return
            }

            // Navigate to next page
            var nextStep = scenarioSteps[scenarioIndex]
            navigateToPage(nextStep.action)

            // Wait for page to fully render before capturing
            scenarioTimer.interval = 1500
            scenarioTimer.start()
        }
    }

    function navigateToPage(action) {
        if (!stackView) {
            console.log("[DevTools] ERROR: No stackView reference")
            return
        }

        // Always pop to root first for a clean slate
        while (stackView.depth > 1) {
            stackView.pop()
        }

        if (action === "home") {
            // Already at root after pop
            return
        }

        var component
        var props = {
            stackView: stackView,
            mainWindow: devToolsPanel.mainWindow
        }

        if (action === "mediaRoom") {
            component = Qt.createComponent("../frontend/MediaRoom.qml")
        } else if (action === "mediaPlayer") {
            component = Qt.createComponent("../frontend/MediaPlayer.qml")
        } else if (action === "obdMenu") {
            component = Qt.createComponent("../frontend/OBDMenu.qml")
        } else if (action === "obdDiag") {
            component = Qt.createComponent("../frontend/OBDDiagnostics.qml")
        } else if (action === "settings") {
            component = Qt.createComponent("../frontend/SettingsMenu.qml")
            props.initialSection = "displaySettings"
        } else {
            console.log("[DevTools] Unknown action:", action)
            return
        }

        if (!component) {
            console.log("[DevTools] ERROR: Failed to create component for:", action)
            return
        }

        if (component.status === Component.Error) {
            console.log("[DevTools] Component error for", action + ":", component.errorString())
            return
        }

        if (component.status === Component.Ready) {
            var page = component.createObject(stackView, props)
            if (page) {
                stackView.push(page)
                console.log("[DevTools] Pushed page:", action, "objectName:", page.objectName)
            } else {
                console.log("[DevTools] ERROR: createObject returned null for:", action)
            }
        } else {
            console.log("[DevTools] Component not ready for:", action, "status:", component.status)
        }
    }

    // ══════════════════════════════════════════
    // UI
    // ══════════════════════════════════════════

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: 12
        contentHeight: contentColumn.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: flickable.width
            spacing: 12
            visible: devToolsPanel.expanded

            // ── Header ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "DEV TOOLS"
                    color: "#E91E63"
                    font.pixelSize: 14
                    font.bold: true
                    font.letterSpacing: 2
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 24; height: 24; radius: 12
                    color: closeArea.containsMouse ? "#E91E63" : "transparent"
                    border.color: "#E91E63"; border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "✕"; color: "white"; font.pixelSize: 12
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: devToolsPanel.expanded = false
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // ── SCREENSHOTS ──

            Text {
                text: "SCREENSHOTS"
                color: "#888"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5
            }

            Text {
                text: devToolsManager ? devToolsManager.screenshotStatus : "idle"
                color: {
                    if (text.indexOf("Done") >= 0) return "#4CAF50"
                    if (text.indexOf("Failed") >= 0) return "#f44336"
                    if (text.indexOf("Capturing") >= 0) return "#FF9800"
                    return "#666"
                }
                font.pixelSize: 11; font.italic: true
            }

            // Capture Current View
            Rectangle {
                Layout.fillWidth: true; height: 40; radius: 6
                color: quickArea.pressed ? "#C2185B" : (quickArea.containsMouse ? "#AD1457" : "#E91E63")
                opacity: devToolsManager && devToolsManager.isCapturing ? 0.5 : 1.0

                Text {
                    anchors.centerIn: parent
                    text: "Capture Current View"
                    color: "white"; font.pixelSize: 13; font.bold: true
                }

                MouseArea {
                    id: quickArea
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (devToolsManager && !devToolsManager.isCapturing) {
                            runCurrentScenario()
                        }
                    }
                }
            }

            // Scenario buttons
            Text { text: "Run Scenario"; color: "#aaa"; font.pixelSize: 11 }

            RowLayout {
                Layout.fillWidth: true; spacing: 8

                Repeater {
                    model: [
                        { label: "All Pages",  scenarioId: "main_pages" },
                        { label: "Settings",   scenarioId: "settings" },
                        { label: "Current",    scenarioId: "current" }
                    ]

                    Rectangle {
                        Layout.fillWidth: true; height: 34; radius: 6
                        color: sArea.pressed ? "#333" : (sArea.containsMouse ? "#2a2a2a" : "#1e1e1e")
                        border.color: sArea.containsMouse ? "#E91E63" : "#444"; border.width: 1
                        opacity: devToolsManager && devToolsManager.isCapturing ? 0.5 : 1.0

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label; color: "white"; font.pixelSize: 11
                        }

                        MouseArea {
                            id: sArea
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (devToolsManager && !devToolsManager.isCapturing) {
                                    if (modelData.scenarioId === "main_pages") {
                                        runAllPagesScenario()
                                    } else if (modelData.scenarioId === "settings") {
                                        runSettingsScenario()
                                    } else {
                                        runCurrentScenario()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Count & actions row
            RowLayout {
                Layout.fillWidth: true; spacing: 8

                Text {
                    text: (devToolsManager ? devToolsManager.screenshotCount : 0) + " captured"
                    color: "#888"; font.pixelSize: 11; Layout.fillWidth: true
                }

                Rectangle {
                    width: 80; height: 26; radius: 4
                    color: openArea.containsMouse ? "#2a2a2a" : "transparent"
                    border.color: "#555"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Open Folder"; color: "#aaa"; font.pixelSize: 10 }
                    MouseArea {
                        id: openArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (devToolsManager) devToolsManager.openOutputFolder() }
                    }
                }

                Rectangle {
                    width: 60; height: 26; radius: 4
                    color: galleryArea.containsMouse ? "#2a2a2a" : "transparent"
                    border.color: "#555"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Gallery"; color: "#aaa"; font.pixelSize: 10 }
                    MouseArea {
                        id: galleryArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (devToolsManager) devToolsManager.generateGallery() }
                    }
                }

                Rectangle {
                    width: 50; height: 26; radius: 4
                    color: clearArea.containsMouse ? "#3a1515" : "transparent"
                    border.color: clearArea.containsMouse ? "#f44336" : "#555"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Clear"; color: clearArea.containsMouse ? "#f44336" : "#aaa"; font.pixelSize: 10 }
                    MouseArea {
                        id: clearArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (devToolsManager) devToolsManager.clearScreenshots() }
                    }
                }
            }

            // Output log
            Rectangle {
                Layout.fillWidth: true
                height: logVisible ? Math.min(logText.implicitHeight + 20, 150) : 0
                radius: 4; color: "#111"; clip: true; visible: logVisible
                property bool logVisible: devToolsManager && devToolsManager.screenshotOutput.length > 0

                Flickable {
                    anchors.fill: parent; anchors.margins: 8
                    contentHeight: logText.implicitHeight; clip: true
                    flickableDirection: Flickable.VerticalFlick

                    Text {
                        id: logText; width: parent.width
                        text: devToolsManager ? devToolsManager.screenshotOutput : ""
                        color: "#0f0"; font.pixelSize: 10; font.family: "monospace"; wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

            // ── DIAGNOSTICS ──

            Text {
                text: "DIAGNOSTICS"
                color: "#888"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5
            }

            GridLayout {
                Layout.fillWidth: true; columns: 2; columnSpacing: 16; rowSpacing: 4

                Text { text: "Window:"; color: "#666"; font.pixelSize: 10 }
                Text {
                    text: devToolsPanel.Window.window ?
                          devToolsPanel.Window.window.width + " x " + devToolsPanel.Window.window.height : "?"
                    color: "#aaa"; font.pixelSize: 10; font.family: "monospace"
                }

                Text { text: "Theme:"; color: "#666"; font.pixelSize: 10 }
                Text {
                    text: App.Style.currentTheme || "?"
                    color: "#aaa"; font.pixelSize: 10; font.family: "monospace"
                }

                Text { text: "Scale:"; color: "#666"; font.pixelSize: 10 }
                Text {
                    text: App.Spacing.globalScale ? App.Spacing.globalScale.toFixed(2) : "?"
                    color: "#aaa"; font.pixelSize: 10; font.family: "monospace"
                }

                Text { text: "Dev Mode:"; color: "#666"; font.pixelSize: 10 }
                Text {
                    text: typeof devMode !== "undefined" && devMode ? "Active" : "No"
                    color: "#4CAF50"; font.pixelSize: 10; font.family: "monospace"; font.bold: true
                }
            }

            Item { height: 4; Layout.fillWidth: true }
        }
    }
}
