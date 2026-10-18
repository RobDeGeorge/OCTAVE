import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15

/*
    Dev Overlay
    ===========
    Reparented into the production root window in dev mode: by
    src/dev/devruntime.cpp (`octave --dev`) or dev/dev_runtime.py
    (`python -m dev.main_dev`). Production frontend/Main.qml has zero
    knowledge of this file; without dev mode the overlay never loads.

    Provides:
      - DEV MODE banner (click to expand DevToolsPanel)
      - Live simulation HUD (RPM / speed / throttle / coolant / fuel)
      - Global keyboard handler routed into devKeyboardController
      - Tab to toggle the HUD
*/

Item {
    id: devOverlay
    objectName: "devOverlay"  // so the command server can toggle it for clean screenshots
    anchors.fill: parent
    z: 99998  // below the production global shift-light flash (z=999999)

    property bool overlayVisible: true

    // Window.window resolves to the ApplicationWindow once this item is
    // attached. Bound (not assigned) so it updates after reparenting —
    // Component.onCompleted runs *before* the dev runtime calls setParentItem, so an assignment there would resolve to null.
    property var rootWindow: Window.window
    property var stackView: rootWindow && rootWindow.contentItem
                            ? findStackView(rootWindow.contentItem)
                            : null

    onRootWindowChanged: {
        if (rootWindow) keyHandler.forceActiveFocus()
    }

    function findStackView(item) {
        if (!item) return null
        if (item.objectName === "stackView") return item
        for (var i = 0; i < item.children.length; i++) {
            var found = findStackView(item.children[i])
            if (found) return found
        }
        return null
    }

    // ── Global keyboard handler ──────────────────────────────────────
    Item {
        id: keyHandler
        anchors.fill: parent
        focus: true

        onActiveFocusChanged: {
            if (!activeFocus) focusRestoreTimer.restart()
        }

        Keys.onPressed: function(event) {
            if (typeof devKeyboardController !== "undefined") {
                devKeyboardController.handle_key(event.key, true)
            }
            if (event.key === Qt.Key_Tab) {
                devOverlay.overlayVisible = !devOverlay.overlayVisible
                event.accepted = true
            }
        }

        Keys.onReleased: function(event) {
            if (typeof devKeyboardController !== "undefined") {
                devKeyboardController.handle_key(event.key, false)
            }
        }
    }

    Timer {
        id: focusRestoreTimer
        interval: 100
        repeat: false
        onTriggered: {
            if (!keyHandler.activeFocus) keyHandler.forceActiveFocus()
        }
    }

    // ── DEV MODE banner ──────────────────────────────────────────────
    Rectangle {
        id: devBanner
        visible: devOverlay.overlayVisible
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: devBannerText.width + 20
        height: 24
        color: devBannerArea.pressed ? "#AD1457" : "#E91E63"
        z: 200

        Text {
            id: devBannerText
            anchors.centerIn: parent
            text: (devToolsPanel.item && devToolsPanel.item.expanded)
                  ? "DEV TOOLS OPEN"
                  : "DEV MODE - Click for tools"
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }

        MouseArea {
            id: devBannerArea
            anchors.fill: parent
            onClicked: {
                if (devToolsPanel.item) {
                    devToolsPanel.item.expanded = !devToolsPanel.item.expanded
                }
            }
        }
    }

    // ── Dev tools panel (loaded from sibling QML) ────────────────────
    Loader {
        id: devToolsPanel
        anchors.top: devBanner.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        z: 199
        source: "DevToolsPanel.qml"
        onLoaded: {
            item.stackView = devOverlay.stackView
            item.mainWindow = devOverlay.rootWindow
        }
    }

    // ── Hide / show overlays for clean screenshot capture ────────────
    Connections {
        target: typeof devToolsManager !== "undefined" ? devToolsManager : null

        function onHideOverlaysRequested() {
            devBanner.visible = false
            simHud.visible = false
            if (devToolsPanel.item) devToolsPanel.item.visible = false
        }

        function onShowOverlaysRequested() {
            devBanner.visible = devOverlay.overlayVisible
            simHud.visible = devOverlay.overlayVisible
            if (devToolsPanel.item) devToolsPanel.item.visible = true
        }
    }

    // ── Simulation HUD ───────────────────────────────────────────────
    Rectangle {
        id: simHud
        visible: devOverlay.overlayVisible
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 30
        anchors.rightMargin: 10
        width: 200
        height: hudColumn.height + 20
        color: Qt.rgba(0, 0, 0, 0.8)
        radius: 8
        z: 100

        Column {
            id: hudColumn
            anchors.centerIn: parent
            spacing: 4

            Text {
                text: "SIMULATION DATA"
                color: "#E91E63"
                font.bold: true
                font.pixelSize: 12
            }

            Rectangle { width: 180; height: 1; color: "#444" }

            // RPM
            Row {
                spacing: 8
                Text { text: "RPM:";   color: "#888"; font.pixelSize: 11; width: 50 }
                Text {
                    text: devOverlay.obd("rpm").toFixed(0)
                    color: "white"
                    font.pixelSize: 11
                    font.bold: true
                    width: 50
                }
                Rectangle {
                    width: 60; height: 8
                    color: "#333"
                    radius: 2
                    Rectangle {
                        width: parent.width * Math.min(1,
                            devOverlay.obd("rpm") / 7000)
                        height: parent.height
                        color: (devOverlay.obd("rpm") > 6000)
                               ? "#f44" : "#4f4"
                        radius: 2
                    }
                }
            }

            // Speed
            Row {
                spacing: 8
                Text { text: "Speed:"; color: "#888"; font.pixelSize: 11; width: 50 }
                Text {
                    text: devOverlay.obd("speedMPH").toFixed(0) + " mph"
                    color: "white"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // Throttle
            Row {
                spacing: 8
                Text { text: "Throttle:"; color: "#888"; font.pixelSize: 11; width: 50 }
                Text {
                    text: devOverlay.obd("throttlePosition").toFixed(0) + "%"
                    color: "white"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // Coolant
            Row {
                spacing: 8
                Text { text: "Coolant:"; color: "#888"; font.pixelSize: 11; width: 50 }
                Text {
                    text: devOverlay.obd("coolantTemp").toFixed(0) + " C"
                    color: (devOverlay.obd("coolantTemp") > 105)
                           ? "#f44" : "white"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // Fuel
            Row {
                spacing: 8
                Text { text: "Fuel:"; color: "#888"; font.pixelSize: 11; width: 50 }
                Text {
                    text: devOverlay.obd("fuelLevel").toFixed(0) + "%"
                    color: (devOverlay.obd("fuelLevel") < 15)
                           ? "#fa0" : "white"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            Rectangle { width: 180; height: 1; color: "#444" }

            Text { text: "CONTROLS"; color: "#E91E63"; font.bold: true; font.pixelSize: 10 }
            Text { text: "W/S = Gas/Brake";   color: "#888"; font.pixelSize: 10 }
            Text { text: "Space = Rev";       color: "#888"; font.pixelSize: 10 }
            Text { text: "Q/R = Shift";       color: "#888"; font.pixelSize: 10 }
            Text { text: "F1-F4 = Scenarios"; color: "#888"; font.pixelSize: 10 }
            Text { text: "Tab = Toggle HUD";  color: "#888"; font.pixelSize: 10 }
        }
    }

    // The getters (obdManager.rpm() ...) have no change signal, so the HUD
    // re-reads them through obd(), which depends on this counter
    property int refresh: 0
    function obd(getter) {
        refresh
        return (typeof obdManager !== "undefined" && obdManager) ? obdManager[getter]() : 0
    }
    Timer {
        interval: 100
        running: devOverlay.overlayVisible
        repeat: true
        onTriggered: devOverlay.refresh++
    }
}
