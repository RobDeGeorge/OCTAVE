import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
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
    // Each entry: { cardId, title, summary, component } — see DisplaySettingsPage
    // for what summary() returns. Other layouts ignore
    // this and use the Repeater rendering below.
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    readonly property var _diag: typeof diagnosticsManager !== "undefined" ? diagnosticsManager : null
    // Pairing codes, paired devices and the service switch: head unit only
    // (src/service/servicepairing.h; not reachable over the service)
    readonly property var _pairing: typeof servicePairing !== "undefined" ? servicePairing : null
    // Installed frontends: other screens for OCTAVE (src/managers/frontendmanager.h)
    readonly property var _frontends: typeof frontendManager !== "undefined" ? frontendManager : null
    property string frontendStatus: ""

    function frontendName(id) {
        if (!id) return "Built-in"
        const list = _frontends ? _frontends.frontends : []
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i].name || id
        return id
    }
    // What a manifest's scopes let it do, in words (src/service/servicescopes.h)
    function scopeWords(scopes) {
        const words = { media: "music and volume", vehicle: "the car", phone: "the phone", settings: "settings",
                      files: "files", system: "system", full: "everything" }
        const changes = (scopes || []).filter(function(s) { return s !== "read" })
        if (changes.indexOf("full") >= 0) return "can change everything"
        if (!changes.length) return "can only read"
        return "can change " + changes.map(function(s) { return words[s] || s }).join(", ")
    }
    function finishFrontendInstall(id) {
        if (!_frontends) return
        frontendStatus = id ? "Installed " + frontendName(id) + "." : _frontends.lastError
    }

    FolderDialog {
        id: frontendFolderDialog
        title: "Install a frontend from a folder"
        onAccepted: pageRoot.finishFrontendInstall(pageRoot._frontends.installFromFolder(selectedFolder.toString()))
    }
    FileDialog {
        id: frontendZipDialog
        title: "Install a frontend from a zip"
        nameFilters: ["Frontends (*.zip)", "All files (*)"]
        onAccepted: pageRoot.finishFrontendInstall(pageRoot._frontends.installFromZip(selectedFile.toString()))
    }

    property var tileModel: [
        { cardId: "device_name", title: "Device", component: deviceContent,
          summary: function() {
              // The dot shows the network; the name alone fits the tile.
              var on = !!(networkManager && networkManager.isConnected)
              return { state: settingsManager ? (settingsManager.deviceName || "OCTAVE") : "OCTAVE",
                       status: on ? App.Style.statusConnected : App.Style.statusDisconnected }
          } },
        { cardId: "device_service", title: "Service", component: serviceContent,
          summary: function() {
              var sp = pageRoot._pairing
              if (!sp) return {}
              var n = sp.devices.length
              return { state: (sp.serviceEnabled ? "On" : "Off") + (n ? " \u2022 " + n + " paired" : ""),
                       status: sp.serviceEnabled ? App.Style.statusConnected : App.Style.statusDisconnected }
          } },
        { cardId: "device_frontends", title: "Frontends", component: frontendsContent,
          summary: function() {
              var f = pageRoot._frontends
              if (!f) return {}
              return { state: f.forcedQml ? "Built-in (forced)" : pageRoot.frontendName(f.activeId) }
          } },
        { cardId: "device_diagnostics", title: "Diagnostics", component: diagnosticsContent,
          summary: function() {
              var d = pageRoot._diag
              return d && d.lastExportPath ? { state: "Logs exported" } : {}
          } }
    ]

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    Component {
        id: deviceContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Name"

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

            SettingCategory {
                title: "Network"

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

            SettingCategory {
                title: "Power"
                description: "Shut down OCTAVE and power off the device."

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
    }

    // Read and get the logs out without a laptop
    Component {
        id: serviceContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingDescription {
                text: "Lets other programs on this device, and devices you pair on your network, read and control OCTAVE "
                      + "(the service protocol: other dashboards, phones, scripts). Anything paired can change settings, "
                      + "play music and see the mirrored phone. Changes to the switch take effect at the next start."
            }

            SettingsToggle {
                objectName: "serviceEnabledToggle"
                compact: true
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft
                visible: !!pageRoot._pairing
                text: "Service"
                checked: pageRoot._pairing ? pageRoot._pairing.serviceEnabled : false
                activeColor: App.Style.accent
                inactiveColor: App.Style.hoverColor
                onToggled: function(on) { if (pageRoot._pairing) pageRoot._pairing.setServiceEnabled(on) }
            }

            SettingCategory {
                title: "Pair a device"
                visible: !!pageRoot._pairing

                SettingDescription {
                    text: pageRoot._pairing && !pageRoot._pairing.loopbackOpen
                          ? "Every client must pair, including programs on this device."
                          : "Programs on this device need no pairing; a device on your network enters this code once."
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing
                    readonly property bool showing: !!pageRoot._pairing && pageRoot._pairing.pairingCode !== ""

                    SettingsButton {
                        objectName: "pairDeviceButton"
                        text: parent.showing ? "New code" : "Show a code"
                        height: pageRoot.dp(30)
                        onClicked: pageRoot._pairing.startPairing()
                    }
                    Text {
                        objectName: "pairingCode"
                        visible: parent.showing
                        text: parent.showing ? pageRoot._pairing.pairingCode.substr(0, 3) + " " + pageRoot._pairing.pairingCode.substr(3) : ""
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText * 1.8
                        font.family: "monospace"
                        font.bold: true
                    }
                    Text {
                        visible: parent.showing
                        text: parent.showing ? pageRoot._pairing.pairingSecondsLeft + " s" : ""
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                    }
                    SettingsButton {
                        visible: parent.showing
                        text: "Cancel"
                        height: pageRoot.dp(30)
                        onClicked: pageRoot._pairing.cancelPairing()
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingCategory {
                title: "Paired devices"
                visible: !!pageRoot._pairing

                Text {
                    visible: !!pageRoot._pairing && pageRoot._pairing.devices.length === 0
                    text: "None yet."
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.family: App.Style.fontFamily
                }

                Repeater {
                    model: pageRoot._pairing ? pageRoot._pairing.devices : []
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing
                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText   // a client chose this name
                            // What it may change (src/service/servicescopes.h): it can always read
                            text: modelData.name + "  \u2022  "
                                  + ((modelData.scopes || ["full"]).indexOf("full") >= 0 ? "full access"
                                     : (modelData.scopes.length <= 1 ? "read only" : "read, " + modelData.scopes.filter(s => s !== "read").join(", ")))
                                  + "  \u2022  last used "
                                  + (modelData.lastUsed ? new Date(modelData.lastUsed).toLocaleString(Qt.locale(), Locale.ShortFormat) : "never")
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                            elide: Text.ElideRight
                        }
                        SettingsButton {
                            readonly property bool readOnly: !!modelData.scopes && modelData.scopes.length === 1
                                                             && modelData.scopes[0] === "read"
                            objectName: "scopes_" + modelData.id
                            text: readOnly ? "Allow all" : "Read only"
                            tooltipText: readOnly ? "Let this device change anything, as when it first paired with full access"
                                                  : "Let this device only read: it can no longer play, change settings or delete"
                            height: pageRoot.dp(28)
                            onClicked: pageRoot._pairing.setDeviceScopes(modelData.id, readOnly ? ["full"] : ["read"])
                        }
                        SettingsButton {
                            objectName: "revoke_" + modelData.id
                            text: "Revoke"
                            tooltipText: "Forget this device; it is disconnected at once"
                            height: pageRoot.dp(28)
                            onClicked: pageRoot._pairing.revoke(modelData.id)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: frontendsContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing
            readonly property var fm: pageRoot._frontends

            SettingDescription {
                text: "Other screens for OCTAVE, made by anyone with the service API. A web frontend is served to "
                      + "browsers; a program frontend replaces this screen. The choice takes effect at the next start. "
                      + "To get this screen back: start with --qml, or put a file named USE_QML in the frontends folder."
            }

            Text {
                objectName: "frontendForcedQml"
                visible: !!fm && fm.forcedQml
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                text: fm && fm.forcedQml ? "Showing the built-in screen instead of " + pageRoot.frontendName(fm.activeId)
                                           + (fm.forcedQmlReason ? ": " + fm.forcedQmlReason : ".") : ""
                color: App.Style.statusDisconnected
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
            }
            SettingsButton {
                visible: !!fm && fm.forcedQml && fm.activeId !== ""
                text: "Try it again at the next start"
                height: pageRoot.dp(30)
                onClicked: fm.clearForcedQml()
            }
            Text {
                visible: !!fm && !fm.forcedQml && fm.activeId !== fm.runningId
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: fm ? "Showing " + pageRoot.frontendName(fm.runningId) + " now; " + pageRoot.frontendName(fm.activeId)
                           + " from the next start." : ""
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing
                visible: !!fm
                SettingsButton {
                    objectName: "installFrontendFolder"
                    text: "Install from folder"
                    height: pageRoot.dp(30)
                    onClicked: frontendFolderDialog.open()
                }
                SettingsButton {
                    objectName: "installFrontendZip"
                    text: "Install from zip"
                    height: pageRoot.dp(30)
                    onClicked: frontendZipDialog.open()
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                objectName: "frontendStatus"
                visible: pageRoot.frontendStatus !== ""
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                text: pageRoot.frontendStatus
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
            }

            SettingCategory {
                title: "Screens"
                visible: !!fm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing
                    Text {
                        Layout.fillWidth: true
                        text: "Built-in  \u2022  this screen"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.85
                        font.family: App.Style.fontFamily
                    }
                    SettingsButton {
                        objectName: "useBuiltInFrontend"
                        text: fm && fm.activeId === "" ? "In use" : "Use"
                        enabled: !!fm && fm.activeId !== ""
                        height: pageRoot.dp(28)
                        onClicked: if (!fm.setActive("")) pageRoot.frontendStatus = fm.lastError
                    }
                }

                Repeater {
                    model: fm ? fm.frontends : []
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText   // from the frontend's manifest
                                text: (modelData.name || modelData.id) + (modelData.version ? " " + modelData.version : "")
                                      + "  \u2022  " + (modelData.kind === "web" ? "web" : "program")
                                      + "  \u2022  " + pageRoot.scopeWords(modelData.scopes)
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.85
                                font.family: App.Style.fontFamily
                                elide: Text.ElideRight
                            }
                            SettingsButton {
                                objectName: "useFrontend_" + modelData.id
                                text: modelData.active ? "In use" : "Use"
                                enabled: !modelData.active && modelData.compatible
                                tooltipText: modelData.compatible ? "Make this the screen from the next start" : modelData.problem
                                height: pageRoot.dp(28)
                                onClicked: if (!fm.setActive(modelData.id)) pageRoot.frontendStatus = fm.lastError
                            }
                            SettingsButton {
                                objectName: "removeFrontend_" + modelData.id
                                text: "Remove"
                                enabled: modelData.id !== fm.runningId
                                height: pageRoot.dp(28)
                                onClicked: if (!fm.remove(modelData.id)) pageRoot.frontendStatus = fm.lastError
                            }
                        }
                        Text {
                            visible: !modelData.compatible || !!modelData.description
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            text: modelData.compatible ? (modelData.description || "") : "Can't be used: " + modelData.problem
                            color: modelData.compatible ? App.Style.secondaryTextColor : App.Style.statusDisconnected
                            font.pixelSize: App.Spacing.overallText * 0.75
                            font.family: App.Style.fontFamily
                        }
                    }
                }
            }
        }
    }

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
