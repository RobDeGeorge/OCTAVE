import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    contentWidth: width
    contentHeight: settingsContent.implicitHeight
    flickableDirection: Flickable.VerticalFlick
    clip: true
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1200
    maximumFlickVelocity: 4000
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

    property var mainWindow: null

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // Each entry: { cardId, title, iconSource, component }. Other layouts ignore
    // this and use the Repeater rendering below.
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    property var tileModel: [
        { cardId: "device_name",    title: "Device Name", iconSource: App.Style.assetBase + "tile_device_name.svg", component: deviceNameContent },
        { cardId: "device_network", title: "Network",     iconSource: App.Style.assetBase + "tile_network.svg",     component: networkContent },
        { cardId: "device_power",   title: "Power",       iconSource: App.Style.assetBase + "tile_power.svg",       component: powerContent },
        { cardId: "device_diagnostics", title: "Diagnostics", iconSource: App.Style.assetBase + "tile_diagnostics.svg", component: diagnosticsContent }
    ]

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    Component {
        id: deviceNameContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingsTextField {
                id: deviceName
                Layout.fillWidth: true
                text: settingsManager ? settingsManager.deviceName : ""

                onEditingFinished: {
                    if (text.trim() !== "" && settingsManager && pageRoot.mainWindow) {
                        pageRoot.mainWindow.updateDeviceName(text)
                    }
                }
            }
        }
    }

    Component {
        id: networkContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingDescription {
                text: "Current network connection status"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing

                // Connection status dot
                Rectangle {
                    width: pageRoot.dp(10)
                    height: pageRoot.dp(10)
                    radius: width / 2
                    color: networkManager && networkManager.isConnected ? "#4CAF50" : "#F44336"
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing * 0.25

                    Text {
                        text: {
                            if (!networkManager) return "Unknown"
                            if (!networkManager.isConnected) return "Not Connected"
                            return networkManager.networkName || "Connected"
                        }
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                    }

                    Text {
                        text: {
                            if (!networkManager) return ""
                            if (!networkManager.isConnected) return "No internet access"
                            return "Internet connected"
                        }
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                    }
                }

                SettingsButton {
                    text: "Refresh"
                    height: pageRoot.dp(28)
                    onClicked: {
                        if (networkManager)
                            networkManager.refreshNetwork()
                    }
                }
            }
        }
    }

    Component {
        id: powerContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingDescription {
                text: "Shut down OCTAVE and power off the device."
            }

            RowLayout {
                id: shutdownRow
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing

                property bool confirmingShutdown: false

                // Reset confirmation after 5 seconds
                Timer {
                    id: shutdownResetTimer
                    interval: 5000
                    onTriggered: shutdownRow.confirmingShutdown = false
                }

                SettingsButton {
                    text: shutdownRow.confirmingShutdown ? "Confirm Shutdown" : "Shut Down"
                    buttonColor: shutdownRow.confirmingShutdown ? "#F44336" : App.Style.accent
                    height: pageRoot.dp(34)
                    onClicked: {
                        if (shutdownRow.confirmingShutdown) {
                            if (networkManager)
                                networkManager.shutdown_device()
                        } else {
                            shutdownRow.confirmingShutdown = true
                            shutdownResetTimer.restart()
                        }
                    }
                }

                Text {
                    text: "Press again to confirm"
                    color: "#F44336"
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.family: App.Style.fontFamily
                    visible: shutdownRow.confirmingShutdown
                    Layout.alignment: Qt.AlignVCenter

                    // Bound to shutdownRow by id: the previous parent.parent
                    // chain resolved to undefined and logged a binding warning
                    // every time the Device page loaded.
                    SequentialAnimation on opacity {
                        running: shutdownRow.confirmingShutdown && shutdownRow.visible  // visible includes hidden pages/sections
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.4; duration: 800 }
                        NumberAnimation { from: 0.4; to: 1.0; duration: 800 }
                    }
                }
            }
        }
    }

    // Read and get the logs out without a laptop
    Component {
        id: diagnosticsContent
        ColumnLayout {
            id: diagColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            property string status: ""

            SettingDescription {
                text: typeof diagnosticsManager !== "undefined"
                      ? "Recent log lines, and ways to get the full logs off the device."
                      : "Diagnostics are not available on this backend."
            }

            // Two-by-two grid: four labels side by side overflow the card
            // at dash scale, and a Layout cannot shrink a button below
            // its label. Each button takes half the card width.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: App.Spacing.overallSpacing
                rowSpacing: App.Spacing.overallSpacing * 0.5
                visible: typeof diagnosticsManager !== "undefined"

                SettingsButton {
                    text: "Refresh"
                    height: pageRoot.dp(30)
                    Layout.fillWidth: true
                    onClicked: logTail.text = diagnosticsManager.recentLogLines(200)
                }
                SettingsButton {
                    text: "Copy"
                    tooltipText: "Copy the recent log lines and device info to the clipboard"
                    height: pageRoot.dp(30)
                    Layout.fillWidth: true
                    onClicked: diagColumn.status = diagnosticsManager.copyLogsToClipboard(400)
                                                    ? "Copied to clipboard" : "Copy failed"
                }
                SettingsButton {
                    text: "Export logs"
                    tooltipText: "Copy every log file into your Downloads folder, ready to attach to an email"
                    height: pageRoot.dp(30)
                    Layout.fillWidth: true
                    onClicked: {
                        var p = diagnosticsManager.exportLogs()
                        diagColumn.status = p !== "" ? "Exported to " + p : "Export failed"
                    }
                }
                SettingsButton {
                    text: "Open folder"
                    height: pageRoot.dp(30)
                    Layout.fillWidth: true
                    visible: typeof diagnosticsManager !== "undefined" && !!diagnosticsManager
                             && diagnosticsManager.canOpenFolder
                    onClicked: diagnosticsManager.openLogFolder()
                }
            }

            Text {
                // Guarded: diagnosticsManager is gone during teardown while a
                // cached page can still re-evaluate this.
                text: typeof diagnosticsManager === "undefined" || !diagnosticsManager ? ""
                      : "OCTAVE " + diagnosticsManager.appVersion + " (" + diagnosticsManager.backendName + " backend)  \u2022  "
                        + diagnosticsManager.deviceInfo + "\nLogs: " + diagnosticsManager.logDir
                visible: text !== ""
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                Layout.preferredWidth: 0   // wrap to the card, never widen it
            }

            Text {
                text: diagColumn.status
                visible: text !== ""
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                Layout.preferredWidth: 0
            }

            // Last lines of the main log (plus the error log); newest at the bottom
            Flickable {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.preferredHeight: pageRoot.dp(180)
                visible: typeof diagnosticsManager !== "undefined"
                contentWidth: width
                contentHeight: logTail.implicitHeight
                clip: true
                flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Component.onCompleted: contentY = Math.max(0, contentHeight - height)

                TextEdit {
                    id: logTail
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: App.Style.primaryTextColor
                    font.family: "monospace"
                    font.pixelSize: App.Spacing.overallText * 0.7
                    text: typeof diagnosticsManager !== "undefined" && diagnosticsManager
                          ? diagnosticsManager.recentLogLines(200) : ""
                }
            }
        }
    }

    // ── Default rendering: stacked SettingsCards (Carousel/Hub/Dashboard layouts).
    //    Sidebar layout sets pageRoot.visible = false and renders the tile grid + popup instead.
    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: App.Spacing.sectionSpacing

        Repeater {
            model: pageRoot.renderCards ? pageRoot.tileModel : []

            SettingsCard {
                Layout.fillWidth: true
                objectName: modelData.title
                cardId: modelData.cardId
                title: modelData.title

                Loader {
                    Layout.fillWidth: true
                    sourceComponent: modelData.component
                }
            }
        }

        // Bottom spacer
        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
