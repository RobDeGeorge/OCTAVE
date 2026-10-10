import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

// "Connect OBD2 adapter" sheet: scans for Bluetooth adapters, then pairs,
// trusts and connects the one the user taps (obdAdapterFinder does the
// work; see src/managers/obdadapterfinder.h). Opened from the OBD page's
// empty state and from Settings > OBD > Adapters. Where the finder isn't
// supported yet (obdAdapterFinder.supported false) it lists
// obdManager.availableAdapters instead, the ports the OS already knows.
Popup {
    id: sheet
    objectName: "obdAdapterSheet"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    // "OBD Settings" tapped (fallback mode); the button shows when connected
    signal settingsRequested()
    property bool offerSettings: false

    readonly property bool hasFinder: typeof obdAdapterFinder !== "undefined" && obdAdapterFinder !== null
    readonly property bool finderSupported: hasFinder && obdAdapterFinder.supported
    readonly property bool obdConnected: typeof obdManager !== "undefined" && obdManager
                                         && obdManager.connected === true
    property bool showAll: false

    readonly property var allDevices: finderSupported ? obdAdapterFinder.devices : []
    readonly property var obdDevices: allDevices.filter(function(d) { return d.likelyObd })
    readonly property var shownDevices: showAll ? allDevices : obdDevices

    readonly property var savedAdapters: {
        if (typeof settingsManager === "undefined" || !settingsManager) return []
        try {
            var arr = JSON.parse(settingsManager.obdSavedAdapters || "[]")
            return Array.isArray(arr) ? arr : []
        } catch (e) {
            return []
        }
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width * 0.92, dp(720))
    height: Math.min(parent.height * 0.9, dp(620))
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: {
        showAll = false
        if (finderSupported)
            obdAdapterFinder.startScan()
        else if (typeof obdManager !== "undefined" && obdManager)
            obdManager.scan()
    }
    // Leave a pairing that's under way running; only the scan stops
    onClosed: if (finderSupported) obdAdapterFinder.stopScan()

    // Close shortly after the adapter comes up so "Connected" registers
    onObdConnectedChanged: if (obdConnected && opened) closeTimer.restart()
    Timer { id: closeTimer; interval: 1500; onTriggered: sheet.close() }

    background: Rectangle {
        color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.97)
        radius: App.Spacing.overallMargin
        border.color: App.Style.accent
        border.width: 2
    }

    // Outlined accent button used across the sheet
    component SheetButton: Rectangle {
        id: btn
        property string label: ""
        property bool active: true
        signal clicked()
        implicitWidth: btnLabel.implicitWidth + sheet.dp(28)
        implicitHeight: sheet.dp(44)
        radius: sheet.dpMin(8, 2)
        color: "transparent"
        border.color: App.Style.accent
        border.width: 1
        opacity: active ? 1.0 : 0.45
        scale: btnMouse.pressed ? 0.92 : 1.0
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
        Text {
            id: btnLabel
            anchors.centerIn: parent
            text: btn.label
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText * 0.9
            font.bold: true
            font.family: App.Style.fontFamily
        }
        MouseArea {
            id: btnMouse
            anchors.fill: parent
            enabled: btn.active
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }

    contentItem: ColumnLayout {
        spacing: sheet.dp(10)

        // ── Header ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: sheet.dp(16)
            Layout.leftMargin: sheet.dp(20)
            Layout.rightMargin: sheet.dp(12)
            spacing: sheet.dp(10)

            Text {
                Layout.fillWidth: true
                text: "Connect OBD2 Adapter"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                font.family: App.Style.fontFamily
                elide: Text.ElideRight
            }
            BusyIndicator {
                Layout.preferredWidth: sheet.dp(28)
                Layout.preferredHeight: sheet.dp(28)
                running: sheet.finderSupported && (obdAdapterFinder.scanning || obdAdapterFinder.busyAddress !== "")
                visible: running
            }
            Text {
                text: "✕"
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.family: App.Style.fontFamily
                Layout.preferredWidth: sheet.dp(36)
                horizontalAlignment: Text.AlignHCenter
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -sheet.dp(8)
                    onClicked: sheet.close()
                }
            }
        }

        // ── Status line ─────────────────────────────────────────────────
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: sheet.dp(20)
            Layout.rightMargin: sheet.dp(20)
            visible: text !== ""
            wrapMode: Text.WordWrap
            text: {
                if (!sheet.finderSupported)
                    return "Pair your adapter in the system's Bluetooth settings, then pick it below."
                if (!obdAdapterFinder.bluetoothOn)
                    return "Bluetooth is off."
                return obdAdapterFinder.status
            }
            color: sheet.finderSupported && (obdAdapterFinder.statusIsError || !obdAdapterFinder.bluetoothOn)
                   ? App.Style.statusError : App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.overallText * 0.85
            font.family: App.Style.fontFamily
        }

        SheetButton {
            Layout.leftMargin: sheet.dp(20)
            visible: sheet.finderSupported && !obdAdapterFinder.bluetoothOn
            label: "Turn Bluetooth On"
            onClicked: {
                obdAdapterFinder.powerOn()
                retryScanTimer.restart()
            }
            Timer { id: retryScanTimer; interval: 1500; onTriggered: obdAdapterFinder.startScan() }
        }

        // ── Saved adapters: one tap reconnects ──────────────────────────
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: sheet.dp(20)
            Layout.rightMargin: sheet.dp(20)
            spacing: sheet.dp(6)
            visible: savedRepeater.count > 0

            Text {
                text: "Saved:"
                height: sheet.dp(32)
                verticalAlignment: Text.AlignVCenter
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
            }
            Repeater {
                id: savedRepeater
                model: sheet.savedAdapters
                delegate: Rectangle {
                    height: sheet.dp(32)
                    width: chipText.implicitWidth + sheet.dp(20)
                    radius: sheet.dpMin(16, 2)
                    color: chipMouse.pressed ? Qt.darker(App.Style.hoverColor, 1.3)
                                             : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
                    border.width: 1
                    border.color: App.Style.accent
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: (modelData.name && modelData.name !== modelData.mac) ? modelData.name : (modelData.mac || "")
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                    }
                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        onClicked: if (modelData.mac && typeof obdManager !== "undefined" && obdManager)
                                       obdManager.connect_to_adapter(modelData.mac)
                    }
                }
            }
        }

        // ── Device list ─────────────────────────────────────────────────
        ListView {
            id: deviceList
            objectName: "obdAdapterList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: sheet.dp(16)
            Layout.rightMargin: sheet.dp(16)
            clip: true
            spacing: sheet.dp(6)
            boundsBehavior: Flickable.StopAtBounds
            model: sheet.finderSupported ? sheet.shownDevices
                   : (typeof obdManager !== "undefined" && obdManager && obdManager.availableAdapters
                      ? obdManager.availableAdapters : [])

            delegate: Rectangle {
                id: row
                width: deviceList.width
                height: sheet.dp(60)
                radius: sheet.dpMin(6, 2)
                readonly property bool finderRow: sheet.finderSupported
                readonly property string address: finderRow ? modelData.address : (modelData.identifier || "")
                readonly property bool busy: finderRow && obdAdapterFinder.busyAddress === address
                readonly property bool connectable: !finderRow || modelData.connectable
                color: rowMouse.pressed ? Qt.darker(App.Style.hoverColor, 1.2) : App.Style.hoverColor
                border.width: 1
                border.color: busy ? App.Style.accent
                                   : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.35)
                opacity: connectable ? 1.0 : 0.5

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: sheet.dp(12)
                    anchors.rightMargin: sheet.dp(12)
                    spacing: sheet.dp(10)

                    // Signal strength, 0-4 bars (rssi 0 = not heard this scan)
                    Row {
                        visible: row.finderRow
                        spacing: sheet.dp(2)
                        Layout.alignment: Qt.AlignVCenter
                        readonly property int bars: {
                            var r = row.finderRow ? modelData.rssi : 0
                            if (r === 0) return 0
                            return r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1
                        }
                        Repeater {
                            model: 4
                            Rectangle {
                                width: sheet.dp(4)
                                height: sheet.dp(6 + index * 4)
                                anchors.bottom: parent.bottom
                                radius: 1
                                color: index < parent.bars ? App.Style.accent
                                                           : Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g,
                                                                     App.Style.secondaryTextColor.b, 0.3)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: sheet.dp(2)
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || row.address
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.95
                            font.bold: true
                            font.family: App.Style.fontFamily
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                if (!row.finderRow) return (modelData.kind || "").toUpperCase() + " · " + row.address
                                var parts = [row.address]
                                parts.push(modelData.kind === "ble" ? "Bluetooth LE"
                                           : modelData.kind === "dual" ? "Classic + LE" : "Classic")
                                if (modelData.paired) parts.push("Paired")
                                if (modelData.note) parts.push(modelData.note)
                                return parts.join(" · ")
                            }
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.72
                            font.family: App.Style.fontFamily
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        visible: row.connectable
                        text: row.busy ? "Working…" : "Connect"
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText * 0.85
                        font.bold: true
                        font.family: App.Style.fontFamily
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    enabled: row.connectable && !row.busy
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (row.finderRow)
                            obdAdapterFinder.connectAdapter(row.address)
                        else if (row.address)
                            obdManager.connect_to_adapter(row.address)
                    }
                }
            }

            // Empty list hints
            Text {
                anchors.centerIn: parent
                width: parent.width - sheet.dp(40)
                visible: deviceList.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    if (!sheet.finderSupported)
                        return "No paired adapters found."
                    if (obdAdapterFinder.scanning)
                        return "Looking for OBD2 adapters…"
                    if (sheet.allDevices.length > 0)
                        return "No OBD2 adapter recognised. If yours has an unusual name, show all devices."
                    return "Nothing found. Plug the adapter in, turn the ignition on, and scan again."
                }
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
            }
        }

        // ── Footer ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: sheet.dp(20)
            Layout.rightMargin: sheet.dp(20)
            Layout.bottomMargin: sheet.dp(16)
            spacing: sheet.dp(10)

            SheetButton {
                label: sheet.finderSupported && obdAdapterFinder.scanning ? "Scanning…" : "Scan Again"
                active: !sheet.finderSupported || (!obdAdapterFinder.scanning && obdAdapterFinder.busyAddress === "")
                onClicked: {
                    if (sheet.finderSupported) obdAdapterFinder.startScan()
                    else obdManager.scan()
                }
            }
            SheetButton {
                visible: sheet.finderSupported && sheet.allDevices.length > sheet.obdDevices.length
                label: sheet.showAll ? "OBD Adapters Only"
                                     : "Show All Devices (" + sheet.allDevices.length + ")"
                onClicked: sheet.showAll = !sheet.showAll
            }
            SheetButton {
                visible: sheet.finderSupported && obdAdapterFinder.busyAddress !== ""
                label: "Cancel"
                onClicked: obdAdapterFinder.cancel()
            }
            Item { Layout.fillWidth: true }
            SheetButton {
                visible: !sheet.finderSupported && sheet.offerSettings
                label: "OBD Settings"
                onClicked: {
                    sheet.close()
                    sheet.settingsRequested()
                }
            }
        }
    }
}
