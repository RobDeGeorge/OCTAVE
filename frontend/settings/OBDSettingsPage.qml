import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App
import OCTAVE.OBD 1.0

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: pageRoot
    // Set by the Sidebar layout; other layouts leave it null and the Black
    // Box card falls back to the attached ApplicationWindow
    property var mainWindow: null

    // Expose Flickable properties for SettingsMenu.qml scroll rail
    property alias contentHeight: rootFlickable.contentHeight
    property alias contentY: rootFlickable.contentY

    // ── Drag state ───────────────────────────────────────────────────
    property string draggedParam: ""
    property string dragSource: "" // "left" or "right"
    property int dragSourceIndex: -1
    property int dropPreviewIndex: -1

    // ── Live connection log ──────────────────────────────────────────
    // Surfaces every BLE state transition so the user can debug from this
    // page without needing adb logcat. The manager keeps the last 60 lines.
    readonly property var connectionLog: obdManager ? obdManager.connectionLog : []

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // Each entry: { cardId, title, summary, component } — see DisplaySettingsPage
    // for what summary() returns. Other layouts ignore
    // this and use the Repeater rendering below.
    //
    // The old single "Connection" card bundled four unrelated concerns in ~650
    // lines; it is split here so the page reads as a 2x2 tile grid like every
    // other settings page.
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    // The manager's connection state as one word for the tile
    function _shortStatus(state) {
        if (state === OBDManager.Connecting) return "Connecting"
        if (state === OBDManager.Connected) return "Connected"
        return "Offline"
    }
    // One colour per connection state
    function _stateColor(state) {
        switch (state) {
        case OBDManager.Connected: return App.Style.accent
        case OBDManager.Connecting: return App.Style.statusWarning
        case OBDManager.NoVehicle: return App.Style.statusInfo
        case OBDManager.Disconnected: return App.Style.statusDisconnected
        default: return App.Style.statusError
        }
    }

    property var tileModel: [
        { cardId: "obd_connection", title: "Connection", component: connectionContent,
          summary: function() {
              if (!obdManager) return {}
              var brief = pageRoot._shortStatus(obdManager.connectionState)
              var vehicle = obdManager.vehicleModelYear > 0
                  ? obdManager.vehicleModelYear + " " + obdManager.vehicleMake : obdManager.vehicleMake
              return { state: brief === "Connected" && vehicle ? vehicle : brief,
                       active: brief === "Connected",
                       status: brief === "Connected" ? App.Style.statusConnected
                             : brief === "Connecting" ? App.Style.statusWarning : App.Style.statusDisconnected }
          } },
        { cardId: "obd_black_box", title: "Black Box", component: blackBoxContent,
          summary: function() {
              if (typeof tripRecorder === "undefined" || !tripRecorder || !settingsManager) return {}
              if (!settingsManager.blackBoxEnabled) return { state: "Off" }
              if (tripRecorder.recording)
                  return { state: "Recording", active: true, status: App.Style.statusConnected }
              var n = tripRecorder.trips.length
              return { state: n + (n === 1 ? " drive" : " drives") }
          } },
        { cardId: "obd_engine", title: "Engine & Tank", component: engineContent,
          summary: function() {
              if (!settingsManager) return {}
              var l = settingsManager.engineDisplacement
              return l > 0 ? { state: l.toFixed(1) + " L" } : {}
          } },
        { cardId: "obd_advanced", title: "Advanced", component: advancedContent,
          summary: function() {
              if (typeof busMonitor !== "undefined" && busMonitor && busMonitor.active)
                  return { state: "Listening", active: true, status: App.Style.statusConnected }
              if (typeof customPids === "undefined" || !customPids) return {}
              var n = customPids.pids.length
              if (customPids.errors.length > 0)
                  return { state: n + " custom, " + customPids.errors.length + " skipped", status: App.Style.statusWarning }
              return n === 0 ? {} : { state: n + (n === 1 ? " custom reading" : " custom readings") }
          } }
    ]

    // ── Engine & Tank: what the calculated readings can't ask the car
    Component {
        id: engineContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingLabel { text: "Engine size" }
            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing
                SettingsSlider {
                    id: engineSizeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 8
                    stepSize: 0.1
                    value: settingsManager ? settingsManager.engineDisplacement : 0
                    valueDisplay: value > 0 ? value.toFixed(1) + " L" : "Not set"
                    onMoved: if (settingsManager) settingsManager.engineDisplacement = Math.round(value * 10) / 10
                }
                Text {
                    text: engineSizeSlider.value > 0 ? engineSizeSlider.value.toFixed(1) + " L" : "Not set"
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: App.Style.fontFamily
                    font.bold: true
                    Layout.preferredWidth: dp(80)
                    horizontalAlignment: Text.AlignRight
                }
            }
            SettingDescription {
                text: "Horsepower and MPG on a car with no air flow sensor"
            }

            SettingsDivider {}

            SettingLabel { text: "Fuel tank" }
            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing
                SettingsSlider {
                    id: fuelTankSlider
                    Layout.fillWidth: true
                    from: 5
                    to: 40
                    stepSize: 0.5
                    value: settingsManager ? settingsManager.fuelTankCapacity : 15
                    valueDisplay: value.toFixed(1) + " gal"
                    onMoved: if (settingsManager) settingsManager.fuelTankCapacity = Math.round(value * 2) / 2
                }
                Text {
                    text: fuelTankSlider.value.toFixed(1) + " gal"
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: App.Style.fontFamily
                    font.bold: true
                    Layout.preferredWidth: dp(80)
                    horizontalAlignment: Text.AlignRight
                }
            }
            SettingDescription {
                text: "For the range reading"
            }
        }
    }

    // ── Advanced: the two tools few people need, one card under the other
    Component {
        id: advancedContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingsCard {
                // A card with no cardId is always open and draws no header
                SettingLabel { text: "Custom PIDs"; font.pixelSize: App.Spacing.overallText * 1.5; font.bold: true }
                Loader { Layout.fillWidth: true; sourceComponent: customPidsContent }
            }
            SettingsCard {
                // A card with no cardId is always open and draws no header
                SettingLabel { text: "Bus Monitor"; font.pixelSize: App.Spacing.overallText * 1.5; font.bold: true }
                Loader { Layout.fillWidth: true; sourceComponent: busMonitorContent }
            }
        }
    }

    // ── Bus monitor card ──────────────────────────────────────────────
    // What the adapter hears on the vehicle bus while it only listens
    // (busMonitor): one row per message id with its latest bytes, the ones
    // that just changed in the accent colour. Nothing here sends anything.
    Component {
        id: busMonitorContent
        ColumnLayout {
            id: busColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            readonly property bool ready: typeof busMonitor !== "undefined" && busMonitor !== null
            readonly property var rows: ready ? busMonitor.ids : []
            readonly property real cell: App.Spacing.overallText * 1.7

            SettingDescription {
                text: "The raw messages on the car's network. OCTAVE only listens; gauges and drive recording pause meanwhile."
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: dp(8)
                SettingsTextField {
                    id: busFilterField
                    Layout.fillWidth: true
                    enabled: busColumn.ready && !busMonitor.active
                    placeholderText: "Message id (7E8, 0CX), or empty for all"
                }
                SettingsButton {
                    Layout.preferredHeight: dp(44)
                    text: busColumn.ready && busMonitor.active ? "Stop" : "Listen"
                    enabled: busColumn.ready && (busMonitor.active || busMonitor.available)
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: {
                        if (!enabled) return
                        if (busMonitor.active) busMonitor.stop()
                        else busMonitor.start(busFilterField.text)
                    }
                }
            }

            SettingDescription {
                visible: busColumn.ready && !busMonitor.available && !busMonitor.active && busMonitor.note === ""
                text: "Connect to a vehicle to listen."
            }
            SettingDescription {
                visible: busColumn.ready && busMonitor.note !== ""
                text: busColumn.ready ? busMonitor.note : ""
                color: App.Style.statusWarning
                wrapMode: Text.WordWrap
            }
            SettingDescription {
                visible: busColumn.ready && (busMonitor.active || busMonitor.frameCount > 0)
                text: !busColumn.ready ? ""
                      : busColumn.rows.length + (busColumn.rows.length === 1 ? " id · " : " ids · ")
                        + busMonitor.frameCount + " messages"
                        + (busMonitor.active ? " · " + busMonitor.framesPerSecond + " per second" : "")
                        + (busMonitor.filter !== "" ? " · only " + busMonitor.filter : "")
            }
            SettingDescription {
                visible: busColumn.ready && busMonitor.overruns > 0
                text: busColumn.ready
                      ? "The adapter could not keep up " + busMonitor.overruns + (busMonitor.overruns === 1 ? " time" : " times")
                        + " and lost messages. Give a message id above to follow just that one."
                      : ""
                color: App.Style.statusWarning
                wrapMode: Text.WordWrap
            }
            SettingDescription {
                visible: busColumn.ready && busMonitor.active && busMonitor.frameCount === 0
                text: "Nothing heard yet. Many newer cars keep their internal traffic off the OBD port."
                wrapMode: Text.WordWrap
            }

            // Delegates are kept across refreshes: the model is the row count
            ListView {
                id: busList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, dp(360))
                visible: count > 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: busColumn.rows.length
                delegate: Row {
                    id: busRow
                    readonly property var entry: busColumn.rows[index] || ({ id: "", count: 0, periodMs: 0, bytes: [], changed: [] })
                    height: App.Spacing.overallText * 1.8
                    spacing: dp(6)
                    Text {
                        width: busColumn.cell * 3.2
                        anchors.verticalCenter: parent.verticalCenter
                        text: busRow.entry.id
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                    }
                    Text {
                        width: busColumn.cell * 2.6
                        anchors.verticalCenter: parent.verticalCenter
                        text: busRow.entry.periodMs > 0 ? busRow.entry.periodMs + " ms" : ""
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                    }
                    Repeater {
                        model: busRow.entry.bytes.length
                        delegate: Text {
                            width: busColumn.cell
                            anchors.verticalCenter: parent.verticalCenter
                            text: busRow.entry.bytes[index] || ""
                            color: busRow.entry.changed[index] ? App.Style.accent : App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                            font.bold: busRow.entry.changed[index] === true
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: busColumn.ready && busMonitor.canRecord && (busMonitor.active || busMonitor.logFile !== "")
                spacing: dp(8)
                SettingsButton {
                    Layout.preferredHeight: dp(44)
                    visible: busColumn.ready && busMonitor.active
                    text: busColumn.ready && busMonitor.recording ? "Stop recording" : "Record to file"
                    onClicked: {
                        if (busMonitor.recording) busMonitor.stopRecording()
                        else busMonitor.startRecording()
                    }
                }
                SettingDescription {
                    Layout.fillWidth: true
                    visible: busColumn.ready && busMonitor.logFile !== ""
                    text: !busColumn.ready ? "" : (busMonitor.recording ? "Recording to " : "Saved: ") + busMonitor.logFile
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    // ── Custom PIDs card ──────────────────────────────────────────────
    // What <config>/custom-pids.json gave at startup (customPids): the
    // manufacturer-specific readings, what was wrong with the file, where
    // it is. Editing is done in the file; a restart reads it again.
    Component {
        id: customPidsContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            readonly property bool ready: typeof customPids !== "undefined" && customPids !== null
            property bool created: false

            SettingDescription {
                text: "Readings beyond standard OBD2, such as transmission temperature, from a file you edit. Read only. They show under Custom in the gauge picker."
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: parent.ready ? customPids.pids : []
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    SettingLabel { text: modelData.title + (modelData.unit ? " (" + modelData.unit + ")" : "") }
                    SettingDescription {
                        text: modelData.id + " · mode " + modelData.mode + " id " + modelData.pid
                              + (modelData.header ? " · ECU " + modelData.header : "") + " · " + modelData.formula
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            Repeater {
                model: parent.ready ? customPids.errors : []
                delegate: SettingDescription {
                    text: "Skipped: " + modelData
                    color: App.Style.statusWarning
                    wrapMode: Text.WordWrap
                }
            }

            SettingsDivider {}

            SettingDescription {
                visible: parent.ready
                text: (parent.created ? "Starter file written. Restart OCTAVE to load it: "
                       : parent.ready && customPids.fileExists ? "Edit this file, then restart OCTAVE: "
                       : "No file yet. It goes here: ") + (parent.ready ? customPids.file : "")
                wrapMode: Text.WrapAnywhere
            }
            SettingsButton {
                Layout.preferredHeight: dp(44)
                text: "Create starter file"
                visible: parent.ready && !customPids.fileExists
                onClicked: parent.created = customPids.createExample()
            }
        }
    }

    // ── Black box (TripRecorder) card ─────────────────────────────────
    // Recording on/off, the background PID sweep, the on-disk cap, what is
    // stored where, and the way to the Trip History page / the files.
    Component {
        id: blackBoxContent
        ColumnLayout {
            id: blackBoxColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            readonly property bool recorderReady: typeof tripRecorder !== "undefined" && tripRecorder !== null
            function fmtBytes(b) {
                b = b || 0
                if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(1) + " MB"
                if (b >= 1024) return Math.round(b / 1024) + " KB"
                return b + " B"
            }

            SettingsToggle {
                text: "Record every drive"
                compact: true
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft
                checked: settingsManager ? settingsManager.blackBoxEnabled : true
                onToggled: function(checked) { if (settingsManager) settingsManager.save_black_box_enabled(checked) }
            }
            SettingDescription {
                text: "Every reading, kept in the black box and as one CSV per drive"
            }

            SettingsDivider {}

            SettingsToggle {
                text: "Record everything the car reports"
                compact: true
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft
                checked: settingsManager ? settingsManager.blackBoxSweepAll : true
                onToggled: function(checked) { if (settingsManager) settingsManager.save_black_box_sweep_all(checked) }
            }
            SettingDescription {
                text: "Also reads what is not on screen, without slowing the gauges"
            }

            SettingsDivider {}

            SettingLabel { text: "Storage cap" }
            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing
                SettingsSlider {
                    id: blackBoxCapSlider
                    Layout.fillWidth: true
                    from: 50
                    to: 5000
                    stepSize: 50
                    value: settingsManager ? settingsManager.blackBoxMaxMB : 500
                    valueDisplay: Math.round(value) + " MB"
                    onMoved: if (settingsManager) settingsManager.save_black_box_max_mb(Math.round(value))
                }
                Text {
                    text: Math.round(blackBoxCapSlider.value) + " MB"
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: App.Style.fontFamily
                    font.bold: true
                    Layout.preferredWidth: dp(80)
                    horizontalAlignment: Text.AlignRight
                }
            }
            SettingDescription {
                text: "The oldest drives are deleted first"
            }

            SettingsDivider {}

            SettingLabel { text: "Status" }
            Text {
                Layout.fillWidth: true
                text: !parent.recorderReady ? "Trip recorder unavailable"
                    : !tripRecorder.storeReady ? "Black box unavailable: the database could not be opened (nothing is recorded)"
                    : tripRecorder.recording
                        ? "Recording · " + tripRecorder.currentSampleCount + " samples this drive"
                        : (settingsManager && settingsManager.blackBoxEnabled ? "Waiting for the engine" : "Off")
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: parent.recorderReady
                text: parent.recorderReady
                      ? tripRecorder.trips.length + (tripRecorder.trips.length === 1 ? " drive · " : " drives · ")
                        + parent.fmtBytes(tripRecorder.storageBytes) + " used"
                      : ""
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
            }
            Text {
                Layout.fillWidth: true
                // Open folder says where, when there is a file manager to open
                visible: parent.recorderReady && !tripRecorder.canOpenFolder
                text: parent.recorderReady ? tripRecorder.tripsDir : ""
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.7
                font.family: App.Style.fontFamily
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                visible: parent.recorderReady && tripRecorder.lastError !== ""
                text: parent.recorderReady ? tripRecorder.lastError : ""
                color: App.Style.statusDisconnected
                font.pixelSize: App.Spacing.overallText * 0.75
                font.family: App.Style.fontFamily
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.overallSpacing
                SettingsButton {
                    Layout.preferredHeight: dp(44)
                    text: "Trip history"
                    enabled: parent.parent.recorderReady
                    onClicked: {
                        var win = pageRoot.mainWindow || pageRoot.ApplicationWindow.window
                        if (win && typeof win.navigateTo === "function") win.navigateTo("TripHistory.qml")
                    }
                }
                SettingsButton {
                    Layout.preferredHeight: dp(44)
                    text: "Open folder"
                    visible: parent.parent.recorderReady && tripRecorder.canOpenFolder
                    onClicked: tripRecorder.openHistoryFolder()
                }
                ConfirmButton {
                    Layout.preferredHeight: dp(44)
                    label: "Clear history"
                    confirmLabel: "Confirm clear"
                    enabled: parent.parent.recorderReady && tripRecorder.trips.length > 0
                    onConfirmed: tripRecorder.clearHistory()
                }
            }
        }
    }

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    Component {
        id: connectionContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            Rectangle {
                id: connectionStatusRect
                Layout.fillWidth: true
                Layout.preferredHeight: dp(40)

                // The text is the label; the state picks the colour
                readonly property string status: obdManager ? obdManager.connectionStatus : "Disconnected"
                readonly property int connState: obdManager ? obdManager.connectionState : OBDManager.Disconnected
                color: pageRoot._stateColor(connState)
                radius: 4

                // Properties for animations
                property bool connecting: connState === OBDManager.Connecting
                property real pulseOpacity: 0.7
                property real connectionProgress: obdManager ?
                    (obdManager.connectionProgress || 0) : 0

                // Progress indicator
                Rectangle {
                    anchors {
                        left: parent.left
                        top: parent.top
                        bottom: parent.bottom
                    }
                    width: parent.width * (connectionStatusRect.connectionProgress / 100)
                    color: Qt.rgba(1, 1, 1, 0.2)
                    radius: parent.radius
                    visible: connectionStatusRect.connecting
                }

                // Pulse animation
                SequentialAnimation {
                    id: pulseAnimation
                    running: connectionStatusRect.connecting && connectionStatusRect.visible  // visible includes hidden pages/sections
                    loops: Animation.Infinite

                    NumberAnimation {
                        target: connectionStatusRect
                        property: "pulseOpacity"
                        from: 0.7
                        to: 1.0
                        duration: 500
                        easing.type: Easing.InOutQuad
                    }

                    NumberAnimation {
                        target: connectionStatusRect
                        property: "pulseOpacity"
                        from: 1.0
                        to: 0.7
                        duration: 500
                        easing.type: Easing.InOutQuad
                    }
                }

                // Click animation
                SequentialAnimation {
                    id: clickAnimation

                    NumberAnimation {
                        target: connectionStatusRect
                        property: "scale"
                        from: 1.0
                        to: 0.95
                        duration: 100
                        easing.type: Easing.InOutQuad
                    }

                    NumberAnimation {
                        target: connectionStatusRect
                        property: "scale"
                        from: 0.95
                        to: 1.0
                        duration: 100
                        easing.type: Easing.InOutQuad
                    }
                }

                // Text and spinner layout
                Column {
                    anchors.centerIn: parent
                    spacing: dp(2)

                    // Main status row
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: dp(10)

                        // Simple spinner using Rectangle animation
                        Rectangle {
                            id: spinner
                            width: dp(20)
                            height: dp(20)
                            radius: dpMin(10, 2)
                            color: "transparent"
                            border.width: 2
                            border.color: "white"
                            visible: connectionStatusRect.connecting

                            // Spinner dot that rotates around
                            Rectangle {
                                id: spinnerDot
                                width: dp(6)
                                height: dp(6)
                                radius: 3
                                color: "white"
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: -3

                                // Animation
                                RotationAnimation {
                                    target: spinner
                                    property: "rotation"
                                    from: 0
                                    to: 360
                                    duration: 1200
                                    loops: Animation.Infinite
                                    running: connectionStatusRect.connecting && connectionStatusRect.visible  // visible includes hidden pages/sections
                                }
                            }
                        }

                        // Status text
                        Text {
                            id: statusText
                            anchors.verticalCenter: parent.verticalCenter
                            text: (obdManager && obdManager.simulated ? "Simulated · " : "") + connectionStatusRect.status
                            color: "white"
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                            font.bold: true
                            opacity: connectionStatusRect.connecting ? connectionStatusRect.pulseOpacity : 1.0
                        }
                    }

                    // Detailed status text - only shown when available
                    Text {
                        id: detailText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: obdManager && obdManager.connectionDetail ?
                            obdManager.connectionDetail : ""
                        color: "white"
                        font.pixelSize: App.Spacing.overallText * 0.7
                        font.family: App.Style.fontFamily
                        visible: text !== ""
                        opacity: 0.9
                    }
                }

                // Click handling
                MouseArea {
                    id: connectionClickArea
                    anchors.fill: parent
                    hoverEnabled: true

                    // Change cursor on hover
                    cursorShape: Qt.PointingHandCursor

                    onClicked: {
                        if (obdManager) {
                            // Pop up the connection menu
                            connectionMenu.popup()
                        }
                    }
                }

                // Connection menu with options
                Menu {
                    id: connectionMenu

                    MenuItem {
                        text: "Reset Connection"
                        onTriggered: {
                            if (obdManager) {
                                clickAnimation.start()
                                obdManager.resetConnection()
                            }
                        }
                    }
                }

                // Connections to OBD manager
                Connections {
                    target: obdManager

                    function onConnectionProgressChanged(progress) {
                        connectionStatusRect.connectionProgress = progress
                    }

                    function onDevicePresenceChanged(present) {
                        if (!present) {
                            deviceNotFoundNotification.open()
                        }
                    }
                }
            }

            SettingsButton {
                text: "Reconnect"
                Layout.preferredHeight: dp(44)
                enabled: !!obdManager
                onClicked: obdManager.resetConnection()
            }

            // ── Vehicle identity (VIN read at connect, Mode 09 PID 02) ──
            SettingLabel {
                text: "Vehicle"
                visible: vehicleText.visible
            }
            Text {
                id: vehicleText
                Layout.fillWidth: true
                readonly property string vin: typeof obdManager !== "undefined" && obdManager && obdManager.vin
                                              ? obdManager.vin : ""
                readonly property string summary: {
                    if (!vin) return ""
                    var parts = []
                    if (obdManager.vehicleModelYear > 0) parts.push(obdManager.vehicleModelYear)
                    if (obdManager.vehicleMake) parts.push(obdManager.vehicleMake)
                    return parts.join(" ")
                }
                visible: vin !== ""
                text: (summary ? summary + "  ·  " : "") + "VIN " + vin
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
                wrapMode: Text.WrapAnywhere
            }

            SettingsDivider {}

            // The adapter: find one, pick a saved one, or type an address
            Loader {
                Layout.fillWidth: true
                sourceComponent: adaptersContent
            }

            SettingsDivider {}

            SettingCategory {
                title: "Fast Polling"
                description: "Not every vehicle keeps up."

                inlineContent: SettingsToggle {
                    compact: true
                    checked: settingsManager ? settingsManager.obdFastMode : true

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_obd_fast_mode(checked)
                        }
                    }
                }

            }

            // Reconnect after a drop. The setting is a count of attempts
            // (0 = off); a slider that appears while it is on picks it.
            SettingCategory {
                title: "Auto Reconnect"

                inlineContent: SettingsToggle {
                    compact: true
                    checked: settingsManager ? settingsManager.obdAutoReconnectAttempts > 0 : false
                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_obd_auto_reconnect_attempts(checked ? 3 : 0)
                    }
                }

                RowLayout {
                    visible: settingsManager ? settingsManager.obdAutoReconnectAttempts > 0 : false
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: autoReconnectSlider
                        Layout.fillWidth: true
                        from: 1
                        to: 10
                        stepSize: 1
                        value: settingsManager ? Math.max(1, settingsManager.obdAutoReconnectAttempts) : 3

                        valueDisplay: value.toString()

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_obd_auto_reconnect_attempts(Math.round(value))
                            }
                        }
                    }

                    Text {
                        text: autoReconnectSlider.value.toString() + (autoReconnectSlider.value === 1 ? " try" : " tries")
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                        Layout.preferredWidth: dp(60)
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            // ── Live OBD log, closed until asked for ──────────
            SettingsButton {
                id: logButton
                property bool open: false
                text: open ? "Hide Log" : "Show Log"
                Layout.preferredHeight: dp(44)
                visible: pageRoot.connectionLog.length > 0
                onClicked: open = !open
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: dp(180)
                visible: logButton.open && pageRoot.connectionLog.length > 0
                radius: dpMin(6, 2)
                color: "#0d0d10"
                border.color: "#22ff88"
                border.width: 1

                ListView {
                    id: settingsLogList
                    anchors.fill: parent
                    anchors.margins: dp(8)
                    clip: true
                    model: pageRoot.connectionLog
                    delegate: Text {
                        text: modelData
                        color: "#88ffaa"
                        font.pixelSize: App.Spacing.overallText * 0.7
                        font.family: "Monospace"
                        wrapMode: Text.Wrap
                        width: settingsLogList.width
                    }
                    onCountChanged: positionViewAtEnd()
                }
            }

            // Add notification popups
            Popup {
                id: deviceNotFoundNotification
                x: (parent.width - width) / 2
                y: parent.height - height - dp(20)
                width: dp(300)
                height: dp(60)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: App.Style.statusError
                    radius: 4
                }

                contentItem: Text {
                    text: "OBD device not found. Check connections."
                    color: "white"
                    wrapMode: Text.WordWrap
                    font.pixelSize: App.Spacing.overallText
                    font.family: App.Style.fontFamily
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                enter: Transition {
                    NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 200 }
                }

                exit: Transition {
                    NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 200 }
                }

                Timer {
                    interval: 3000
                    running: deviceNotFoundNotification.visible
                    onTriggered: deviceNotFoundNotification.close()
                }
            }
        }
    }

    Component {
        id: adaptersContent
        ColumnLayout {
            id: adaptersColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            // The adapter rows open right under the finger that tapped the
            // Adapters tile; a tap that lands here in the first moment is
            // that same press, not a choice (it once re-pointed the OBD port
            // on the Pi). Rows and chips ignore clicks until this passes.
            property real tapsAllowedAt: 0
            function tapsAllowed() { return Date.now() >= tapsAllowedAt }
            Component.onCompleted: tapsAllowedAt = Date.now() + 600
            onVisibleChanged: if (visible) tapsAllowedAt = Date.now() + 600

            SettingLabel {
                text: "Adapter"
            }

            // Typing an address is the fallback where there is nothing to
            // scan with; it stays closed while the finder can do the job
            property bool manualOpen: !(typeof obdAdapterFinder !== "undefined" && obdAdapterFinder
                                        && obdAdapterFinder.canScan)

            // Scan, pair and connect without knowing the MAC (OBDAdapterSheet)
            Rectangle {
                objectName: "findAdaptersButton"
                Layout.fillWidth: true
                Layout.preferredHeight: dp(44)
                radius: dpMin(6, 2)
                color: findAdaptersMouse.pressed ? Qt.darker(App.Style.hoverColor, 1.2) : "transparent"
                border.width: 1
                border.color: App.Style.accent

                Text {
                    anchors.centerIn: parent
                    text: "Find Bluetooth Adapters"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText * 0.9
                    font.family: App.Style.fontFamily
                    font.bold: true
                }

                MouseArea {
                    id: findAdaptersMouse
                    anchors.fill: parent
                    onClicked: if (adaptersColumn.tapsAllowed()) adapterSheet.open()
                }
            }

            App.OBDAdapterSheet {
                id: adapterSheet
            }

            // Discovered adapters — bound to obdManager.availableAdapters
            // ({name, identifier, kind} on every OS). The "kind" tag tells
            // the user what flavour each row is so they can pick.
            Repeater {
                id: adaptersRepeater
                model: obdManager && obdManager.availableAdapters
                    ? obdManager.availableAdapters
                    : []
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(44)
                    radius: dpMin(4, 2)
                    color: adapterItemMouse.pressed
                        ? Qt.darker(App.Style.hoverColor, 1.2)
                        : App.Style.hoverColor
                    border.width: 1
                    border.color: App.Style.accent

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: dp(10)
                        anchors.rightMargin: dp(10)
                        spacing: dp(8)

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || ""
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.family: App.Style.fontFamily
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            Layout.preferredWidth: kindLabel.implicitWidth + dp(10)
                            Layout.preferredHeight: dp(20)
                            radius: dpMin(10, 2)
                            color: Qt.rgba(App.Style.accent.r,
                                           App.Style.accent.g,
                                           App.Style.accent.b, 0.22)
                            Text {
                                id: kindLabel
                                anchors.centerIn: parent
                                text: (modelData.kind || "").toUpperCase()
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.7
                                font.family: App.Style.fontFamily
                                font.bold: true
                            }
                        }
                    }

                    MouseArea {
                        id: adapterItemMouse
                        anchors.fill: parent
                        onClicked: {
                            if (!adaptersColumn.tapsAllowed()) return
                            var ident = modelData.identifier || ""
                            if (!ident) return
                            manualMacField.text = ident
                            if (obdManager
                                && typeof obdManager.connectToAdapter === "function") {
                                obdManager.connectToAdapter(ident)
                            } else if (obdManager) {
                                if (typeof obdManager.setTargetAddress === "function")
                                    obdManager.setTargetAddress(ident)
                                obdManager.forceConnect()
                            }
                        }
                    }
                }
            }

            // Manual entry — universal escape hatch. Accepts a MAC, a
            // /dev/... path, or a COM port. The manager classifies and
            // dispatches accordingly. Not in the browser, which can only
            // reach the adapter through its own Bluetooth chooser.
            SettingsButton {
                text: "Enter Address Manually"
                Layout.preferredHeight: dp(44)
                visible: Qt.platform.os !== "wasm" && !adaptersColumn.manualOpen
                onClicked: adaptersColumn.manualOpen = true
            }

            RowLayout {
                Layout.fillWidth: true
                visible: Qt.platform.os !== "wasm" && adaptersColumn.manualOpen
                spacing: dp(8)

                SettingsTextField {
                    id: manualMacField
                    Layout.fillWidth: true
                    placeholderText: "MAC address, /dev path or COM port"
                    text: settingsManager ? settingsManager.obdBluetoothPort : ""

                    // Typing (and the taps above) replace the binding; keep
                    // following the saved port whenever the field is not
                    // being edited, so Go never sends a stale address
                    Connections {
                        target: settingsManager
                        function onObdBluetoothPortChanged() {
                            if (!manualMacField.activeFocus)
                                manualMacField.text = settingsManager.obdBluetoothPort
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: dp(80)
                    Layout.preferredHeight: dp(40)
                    radius: dpMin(6, 2)
                    color: manualConnectMouse.pressed ? Qt.darker("#27ae60", 1.3) : "#27ae60"

                    Text {
                        anchors.centerIn: parent
                        text: "Go"
                        color: "white"
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                    }

                    MouseArea {
                        id: manualConnectMouse
                        anchors.fill: parent
                        onClicked: {
                            var entered = manualMacField.text.trim()
                            if (!entered) return
                            if (obdManager
                                && typeof obdManager.connectToAdapter === "function") {
                                obdManager.connectToAdapter(entered)
                            } else if (obdManager) {
                                // Fallback for any backend that hasn't been
                                // updated yet — keeps the UI working.
                                if (settingsManager)
                                    settingsManager.save_obd_bluetooth_port(entered)
                                if (typeof obdManager.setTargetAddress === "function")
                                    obdManager.setTargetAddress(entered)
                                obdManager.forceConnect()
                            }
                        }
                    }
                }

                // Linux: open a Bluetooth socket straight to the MAC instead
                // of going through an rfcomm-bound /dev node. Saved as
                // rfcomm://<MAC>, so reconnects and restarts reuse it.
                Rectangle {
                    visible: typeof obdManager !== "undefined" && obdManager
                             && typeof obdManager.connectDirect === "function"
                             && typeof obdManager.platformHint === "function"
                             && obdManager.platformHint() === "linux"
                    Layout.preferredWidth: dp(80)
                    Layout.preferredHeight: dp(40)
                    radius: dpMin(6, 2)
                    color: directConnectMouse.pressed ? Qt.darker("#2980b9", 1.3) : "#2980b9"

                    Text {
                        anchors.centerIn: parent
                        text: "Direct"
                        color: "white"
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                    }

                    MouseArea {
                        id: directConnectMouse
                        anchors.fill: parent
                        onClicked: {
                            var entered = manualMacField.text.trim()
                            if (entered)
                                obdManager.connectDirect(entered)
                        }
                    }
                }
            }

            // Saved-adapter chips — tap to reuse a previous connection.
            Flow {
                Layout.fillWidth: true
                spacing: dp(6)
                visible: savedAdaptersRepeater.count > 0

                Repeater {
                    id: savedAdaptersRepeater
                    model: settingsManager ? settingsManager.obdSavedAdapters : []

                    delegate: Rectangle {
                        id: savedChip
                        // Removing a saved adapter takes two taps on the X
                        property bool removeArmed: false
                        onVisibleChanged: if (!visible) removeArmed = false
                        Timer { id: savedChipArmTimer; interval: 4000; onTriggered: savedChip.removeArmed = false }
                        height: dp(32)
                        width: chipLayout.implicitWidth + dp(20)
                        radius: dpMin(16, 2)
                        color: chipMouse.pressed
                            ? Qt.darker(App.Style.hoverColor, 1.3)
                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
                        border.width: 1
                        border.color: App.Style.accent

                        RowLayout {
                            id: chipLayout
                            anchors.centerIn: parent
                            spacing: dp(6)

                            Text {
                                text: (modelData.name && modelData.name !== modelData.mac)
                                    ? (modelData.name + " · " + modelData.mac)
                                    : (modelData.mac || "")
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.family: App.Style.fontFamily
                            }

                            Text {
                                text: savedChip.removeArmed ? "Remove?" : "✕"
                                color: savedChip.removeArmed ? App.Style.statusDanger : App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.bold: savedChip.removeArmed
                                font.family: App.Style.fontFamily

                                MouseArea {
                                    // Reaches past the glyph to a dp(40) target
                                    anchors.fill: parent
                                    anchors.margins: -Math.max(dp(4), (dp(40) - parent.height) / 2)
                                    onClicked: {
                                        if (!savedChip.removeArmed) {
                                            savedChip.removeArmed = true
                                            savedChipArmTimer.restart()
                                            return
                                        }
                                        savedChip.removeArmed = false
                                        savedChipArmTimer.stop()
                                        if (settingsManager && modelData.mac) {
                                            settingsManager.remove_obd_saved_adapter(modelData.mac)
                                        }
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            // Under the row: declared after it, this area sat
                            // on top of the X and a tap there connected
                            // instead of removing
                            z: -1
                            onClicked: {
                                if (!adaptersColumn.tapsAllowed()) return
                                var mac = modelData.mac || ""
                                if (!mac) return
                                manualMacField.text = mac
                                if (obdManager
                                    && typeof obdManager.connectToAdapter === "function") {
                                    obdManager.connectToAdapter(mac)
                                } else if (obdManager) {
                                    if (settingsManager)
                                        settingsManager.save_obd_bluetooth_port(mac)
                                    if (typeof obdManager.setTargetAddress === "function")
                                        obdManager.setTargetAddress(mac)
                                    obdManager.forceConnect()
                                }
                            }
                        }
                    }
                }
            }

            // Per-OS hint — the *only* place this page knows about
            // platform variation. Drives copy, never structure.
            SettingDescription {
                visible: Qt.platform.os === "wasm" || adaptersColumn.manualOpen
                text: {
                    if (Qt.platform.os === "wasm")
                        return "In the browser, Find Bluetooth Adapters connects a Bluetooth LE OBD2 adapter "
                               + "(Chrome or Edge) or the simulated car."
                    var hint = (obdManager && typeof obdManager.platformHint === "function")
                        ? obdManager.platformHint() : ""
                    if (hint === "android")
                        return "Pair your ELM327 in Android Settings first, then enter its MAC here and press Go."
                    if (hint === "linux")
                        return "Direct connects to a MAC over Bluetooth and is the most reliable. Go uses /dev/rfcomm0. " +
                               "A USB adapter is /dev/ttyUSB0."
                    if (hint === "windows")
                        return "Enter the COM port assigned to your paired ELM327 (e.g. COM5), or its MAC."
                    if (hint === "macos")
                        return "Enter MAC for Classic BT, or the /dev/tty.* path."
                    if (hint === "ios")
                        return "iOS only exposes BLE devices by UUID — MAC entry is not supported by iOS."
                    return "Enter your adapter's MAC, /dev path, or COM port and press Go."
                }
            }
        }
    }

    Flickable {
        id: rootFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: settingsContent.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        clip: true
        interactive: true  // Card MouseAreas disable this on press to prevent steal
        boundsBehavior: Flickable.DragAndOvershootBounds
        flickDeceleration: 1200
        maximumFlickVelocity: 4000
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: settingsContent
            width: parent.width
            spacing: App.Spacing.sectionSpacing

            // ── Default rendering: stacked SettingsCards (Carousel/Hub/Dashboard).
            //    Sidebar layout hides this and renders the tile grid + popup.
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

    } // end Flickable

    // ── Floating drag proxy (viewport space, outside Flickable) ─────
    Rectangle {
        id: dragProxy
        // In the window's overlay, not the page: the grid layout keeps the
        // page itself in a hidden Loader and shows its cards in a popup, so
        // a proxy parented to the page was never visible and the drop
        // (gated on that) never happened
        parent: Overlay.overlay
        visible: false
        z: 100
        color: App.Style.backgroundColor
        border.color: App.Style.accent
        border.width: 2
        radius: 4
        opacity: 0.9

        property string paramId: ""
        property string paramTitle: ""
        property string paramValue: ""

        Drag.keys: ["obdParam"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: dp(6)
            spacing: 2

            Text {
                text: dragProxy.paramTitle
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.9
                font.family: App.Style.fontFamily
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                text: dragProxy.paramValue
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.8
                font.family: App.Style.fontFamily
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
