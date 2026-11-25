import QtQuick 2.15
import QtQuick.Controls 2.15
import "." as App

// "Connect Vehicle" sheet, in the ConnectSheet look Connect Music shares: scans for Bluetooth adapters, then pairs,
// trusts and connects the one the user taps (obdAdapterFinder does the
// work; see src/managers/obdadapterfinder.h). Opened from the OBD page's
// empty state and from Settings > OBD > Adapters. Where the finder isn't
// supported yet (obdAdapterFinder.supported false) it lists
// obdManager.availableAdapters instead, the ports the OS already knows.
App.ConnectSheet {
    id: sheet
    objectName: "obdAdapterSheet"

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

    readonly property var savedAdapters: (typeof settingsManager !== "undefined" && settingsManager)
                                         ? settingsManager.obdSavedAdapters : []

    title: "Connect Vehicle"
    busy: finderSupported && (obdAdapterFinder.scanning || obdAdapterFinder.busyAddress !== "")

    // The browser's Bluetooth chooser is a modal of its own: open it from
    // the button, not over the list (which offers the simulated car there)
    readonly property bool browser: Qt.platform.os === "wasm"

    onAboutToShow: {
        showAll = false
        if (finderSupported && !browser)
            obdAdapterFinder.startScan()
        else if (typeof obdManager !== "undefined" && obdManager)
            obdManager.scan()
    }
    // Leave a pairing that's under way running; only the scan stops
    onClosed: if (finderSupported) obdAdapterFinder.stopScan()

    // Close shortly after the adapter comes up so "Connected" registers
    onObdConnectedChanged: if (obdConnected && opened) closeTimer.restart()
    Timer { id: closeTimer; interval: 1500; onTriggered: sheet.close() }

    // The outlined accent button of the footer (the Connect Music button's look)
    component SheetButton: Rectangle {
        id: btn
        property string label: ""
        property bool active: true
        signal clicked()
        width: Math.min(btnLabel.implicitWidth + sheet.dp(28), sheet.bodyWidth)
        height: sheet.dp(40)
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
            width: Math.min(implicitWidth, parent.width - sheet.dp(12))
            elide: Text.ElideRight
            text: btn.label
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText * 0.85
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

    // ── Status line ─────────────────────────────────────────────────────
    Text {
        width: parent.width
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: {
            if (typeof obdManager !== "undefined" && obdManager && obdManager.simulated)
                return "Simulated car · " + obdManager.connectionStatus + " — demo data"
            if (!sheet.finderSupported)
                return "Pair your adapter in the system's Bluetooth settings, then pick it below."
            if (!obdAdapterFinder.bluetoothOn)
                return "Bluetooth is off. You can still connect the simulated car."
            return obdAdapterFinder.status
        }
        color: sheet.finderSupported && !(typeof obdManager !== "undefined" && obdManager && obdManager.simulated)
               && (obdAdapterFinder.statusIsError || !obdAdapterFinder.bluetoothOn)
               ? App.Style.statusError : App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText * 0.8
        font.family: App.Style.fontFamily
    }

    SheetButton {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: sheet.finderSupported && !obdAdapterFinder.bluetoothOn
        label: "Turn Bluetooth On"
        onClicked: {
            obdAdapterFinder.powerOn()
            retryScanTimer.restart()
        }
        Timer { id: retryScanTimer; interval: 1500; onTriggered: obdAdapterFinder.startScan() }
    }

    // ── Saved adapters: one tap reconnects ──────────────────────────────
    Repeater {
        id: savedRepeater
        model: sheet.savedAdapters
        delegate: App.SheetRow {
            title: (modelData.name && modelData.name !== modelData.mac) ? modelData.name : (modelData.mac || "")
            detail: "Saved adapter"
            onPicked: if (modelData.mac && typeof obdManager !== "undefined" && obdManager)
                          obdManager.connectToAdapter(modelData.mac)
        }
    }

    // ── Device list (scrolls past half the window) ──────────────────────
    ListView {
        id: deviceList
        objectName: "obdAdapterList"
        width: parent.width
        height: count === 0 ? emptyHint.implicitHeight + sheet.dp(16)
                            : Math.min(contentHeight, Math.max(sheet.dp(120), (sheet.parent ? sheet.parent.height : 600) * 0.45))
        clip: true
        spacing: sheet.dp(12)
        boundsBehavior: Flickable.StopAtBounds
        model: sheet.finderSupported ? sheet.shownDevices
               : (typeof obdManager !== "undefined" && obdManager && obdManager.availableAdapters
                  ? obdManager.availableAdapters : [])

        delegate: App.SheetRow {
            id: deviceRow
            width: deviceList.width
            readonly property bool finderRow: sheet.finderSupported
            readonly property string address: deviceRow.finderRow ? modelData.address : (modelData.identifier || "")
            readonly property bool busy: deviceRow.finderRow && obdAdapterFinder.busyAddress === deviceRow.address
            objectName: modelData.kind === "sim" ? "simulatedObdAdapter" : ""
            title: modelData.name || deviceRow.address
            detail: {
                if (modelData.kind === "sim") return modelData.note
                if (!deviceRow.finderRow) return (modelData.kind || "").toUpperCase() + " · " + deviceRow.address
                // A browser serial port has no address worth showing
                var parts = modelData.kind === "serial" ? ["Serial port"] : [deviceRow.address]
                if (modelData.kind !== "serial")
                    parts.push(modelData.kind === "ble" ? "Bluetooth LE"
                               : modelData.kind === "dual" ? "Classic + LE" : "Classic")
                if (modelData.paired) parts.push("Paired")
                if (modelData.note) parts.push(modelData.note)
                return parts.join(" · ")
            }
            trailing: deviceRow.busy ? "Working…" : ""
            highlighted: deviceRow.busy
            // Signal strength, 0-4 bars (rssi 0 = not heard this scan)
            bars: {
                if (!deviceRow.finderRow || modelData.kind === "sim" || modelData.kind === "serial") return -1
                var r = modelData.rssi
                if (r === 0) return 0
                return r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1
            }
            active: (!deviceRow.finderRow || modelData.connectable) && !deviceRow.busy
            onPicked: {
                if (deviceRow.finderRow)
                    obdAdapterFinder.connectAdapter(deviceRow.address)
                else if (deviceRow.address)
                    obdManager.connectToAdapter(deviceRow.address)
            }
        }

        // Empty list hints
        Text {
            id: emptyHint
            anchors.centerIn: parent
            width: parent.width - sheet.dp(20)
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
            font.pixelSize: App.Spacing.overallText * 0.8
            font.family: App.Style.fontFamily
        }
    }

    // ── Footer ──────────────────────────────────────────────────────────
    Flow {
        width: parent.width
        spacing: sheet.dp(10)
        // centred: Flow has no alignment of its own
        leftPadding: Math.max(0, (width - footerWidth) / 2)
        readonly property real footerWidth: {
            var w = 0, n = 0
            for (var i = 0; i < children.length; ++i)
                if (children[i].visible) { w += children[i].width; ++n }
            return n > 0 ? w + (n - 1) * spacing : 0
        }

        SheetButton {
            // A browser without Web Bluetooth has only the simulated car
            visible: !sheet.finderSupported || obdAdapterFinder.canScan
            label: sheet.finderSupported && obdAdapterFinder.scanning ? "Scanning…"
                   : sheet.browser ? "Pick Bluetooth Adapter" : "Scan Again"
            active: !sheet.finderSupported || (!obdAdapterFinder.scanning && obdAdapterFinder.busyAddress === "")
            onClicked: {
                if (sheet.finderSupported) obdAdapterFinder.startScan()
                else obdManager.scan()
            }
        }
        SheetButton {
            // Browser with Web Serial: an adapter on a USB cable
            visible: sheet.finderSupported && obdAdapterFinder.canPickSerial
            label: "Pick USB Adapter"
            active: !obdAdapterFinder.scanning && obdAdapterFinder.busyAddress === ""
            onClicked: obdAdapterFinder.pickSerialPort()
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
