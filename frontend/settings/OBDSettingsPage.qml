import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: pageRoot

    // Expose Flickable properties for SettingsMenu.qml scroll rail
    property alias contentHeight: rootFlickable.contentHeight
    property alias contentY: rootFlickable.contentY

    // ── Drag state ───────────────────────────────────────────────────
    property string draggedParam: ""
    property string dragSource: "" // "left" or "right"
    property int dragSourceIndex: -1
    property int dropPreviewIndex: -1

    // ── Live connection log (mirrors what OBDHome.qml shows) ─────────
    // Surfaces every BLE state transition so the user can debug from this
    // page without needing adb logcat. Capped at 60 lines.
    property var connectionLog: []
    Connections {
        target: obdManager
        enabled: obdManager !== null
        function onConnectionLogLineAppended(line) {
            var arr = pageRoot.connectionLog.slice()
            arr.push(line)
            if (arr.length > 60) arr = arr.slice(arr.length - 60)
            pageRoot.connectionLog = arr
        }
    }

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // Each entry: { cardId, title, iconSource, component }. Other layouts ignore
    // this and use the Repeater rendering below.
    //
    // The old single "Connection" card bundled four unrelated concerns in ~650
    // lines; it is split here so the page reads as a 2x2 tile grid like every
    // other settings page.
    property var tileModel: [
        { cardId: "obd_connection", title: "Connection", iconSource: App.Style.assetBase + "tile_connection.svg", component: connectionContent },
        { cardId: "obd_adapters",   title: "Adapters",   iconSource: App.Style.assetBase + "tile_adapters.svg",   component: adaptersContent },
        { cardId: "obd_tuning",     title: "Tuning",     iconSource: App.Style.assetBase + "tile_tuning.svg",     component: tuningContent },
        { cardId: "obd_parameters", title: "Parameters", iconSource: App.Style.assetBase + "tile_parameters.svg", component: parametersContent }
    ]

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    Component {
        id: connectionContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingLabel {
                text: "Connection Status"
            }

            Rectangle {
                id: connectionStatusRect
                Layout.fillWidth: true
                Layout.preferredHeight: dp(40)

                // Use more diverse status colors
                property var statusColors: {
                    "Connected": App.Style.accent,
                    "Connecting": App.Style.statusWarning,
                    "Device Not Found": App.Style.statusError,
                    "Error": App.Style.statusError,
                    "Disconnected": App.Style.statusDisconnected,
                    "Device Lost": App.Style.statusWarning,
                    "No Vehicle": App.Style.statusInfo
                }

                // Default to error color if status not in our map
                color: obdManager ?
                    (statusColors[obdManager.get_connection_status()] || App.Style.statusError) :
                    App.Style.statusError
                radius: 4

                // Properties for animations
                property bool connecting: obdManager ?
                    (obdManager.get_connection_status() === "Connecting") : false
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
                    running: connectionStatusRect.connecting
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
                                    running: connectionStatusRect.connecting
                                }
                            }
                        }

                        // Status text
                        Text {
                            id: statusText
                            anchors.verticalCenter: parent.verticalCenter
                            text: obdManager ? obdManager.get_connection_status() : "Not Connected"
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
                                obdManager.reset_connection()
                            }
                        }
                    }
                }

                // Connections to OBD manager
                Connections {
                    target: obdManager

                    function onConnectionStatusChanged(status) {
                        connectionStatusRect.connecting = (status === "Connecting")
                    }

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

            // ── Live OBD log ──────────────────────────────────
            SettingLabel {
                text: "Connection Log"
                visible: pageRoot.connectionLog.length > 0
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: dp(180)
                visible: pageRoot.connectionLog.length > 0
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

            SettingDescription {
                text: "Tap status bar to reconnect"
            }
        }
    }

    Component {
        id: adaptersContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            SettingLabel {
                text: "OBD Adapter"
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
                        ? Qt.darker(App.Style.cardBackground, 1.2)
                        : App.Style.cardBackground
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
                            color: App.Style.textColor
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
                            var ident = modelData.identifier || ""
                            if (!ident) return
                            manualMacField.text = ident
                            if (obdManager
                                && typeof obdManager.connect_to_adapter === "function") {
                                obdManager.connect_to_adapter(ident)
                            } else if (obdManager) {
                                if (typeof obdManager.set_target_address === "function")
                                    obdManager.set_target_address(ident)
                                obdManager.force_connect()
                            }
                        }
                    }
                }
            }

            // Manual entry — universal escape hatch. Accepts a MAC, a
            // /dev/... path, or a COM port. The manager classifies and
            // dispatches accordingly.
            RowLayout {
                Layout.fillWidth: true
                spacing: dp(8)

                SettingsTextField {
                    id: manualMacField
                    Layout.fillWidth: true
                    placeholderText: "MAC address, /dev path, or COM port"
                    text: settingsManager ? settingsManager.obdBluetoothPort : ""
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
                                && typeof obdManager.connect_to_adapter === "function") {
                                obdManager.connect_to_adapter(entered)
                            } else if (obdManager) {
                                // Fallback for any backend that hasn't been
                                // updated yet — keeps the UI working.
                                if (settingsManager)
                                    settingsManager.save_obd_bluetooth_port(entered)
                                if (typeof obdManager.set_target_address === "function")
                                    obdManager.set_target_address(entered)
                                obdManager.force_connect()
                            }
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
                    model: {
                        if (!settingsManager) return []
                        try {
                            var raw = settingsManager.obdSavedAdapters || "[]"
                            var arr = JSON.parse(raw)
                            return Array.isArray(arr) ? arr : []
                        } catch (e) {
                            return []
                        }
                    }

                    delegate: Rectangle {
                        height: dp(32)
                        width: chipLayout.implicitWidth + dp(20)
                        radius: dpMin(16, 2)
                        color: chipMouse.pressed
                            ? Qt.darker(App.Style.cardBackground, 1.3)
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
                                text: "✕"
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.family: App.Style.fontFamily

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -dp(4)
                                    onClicked: {
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
                            onClicked: {
                                var mac = modelData.mac || ""
                                if (!mac) return
                                manualMacField.text = mac
                                if (obdManager
                                    && typeof obdManager.connect_to_adapter === "function") {
                                    obdManager.connect_to_adapter(mac)
                                } else if (obdManager) {
                                    if (settingsManager)
                                        settingsManager.save_obd_bluetooth_port(mac)
                                    if (typeof obdManager.set_target_address === "function")
                                        obdManager.set_target_address(mac)
                                    obdManager.force_connect()
                                }
                            }
                        }
                    }
                }
            }

            // Per-OS hint — the *only* place this page knows about
            // platform variation. Drives copy, never structure.
            SettingDescription {
                text: {
                    var hint = (obdManager && typeof obdManager.platform_hint === "function")
                        ? obdManager.platform_hint() : ""
                    if (hint === "android")
                        return "Pair your ELM327 in Android Settings first, then enter its MAC here and press Go."
                    if (hint === "linux")
                        return "Enter MAC (auto-binds rfcomm) or /dev/rfcomm0. " +
                               "USB ELM327 also works (/dev/ttyUSB0). See Connection Log for rfcomm errors."
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

    Component {
        id: tuningContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            // Fast Mode Toggle
            ColumnLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.rowSpacing

                SettingsToggle {
                    text: "Fast mode"
                    Layout.fillWidth: true
                    checked: settingsManager ? settingsManager.obdFastMode : true

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_obd_fast_mode(checked)
                        }
                    }
                }

                SettingDescription {
                    text: "Faster polling, may not work with all vehicles"
                }
            }

            SettingsDivider {}

            // Auto-Reconnect Attempts
            ColumnLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.rowSpacing

                SettingLabel {
                    text: "Reconnect Retries"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: autoReconnectSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 10
                        stepSize: 1
                        value: settingsManager ? settingsManager.obdAutoReconnectAttempts : 0

                        valueDisplay: value === 0 ? "Off" : value.toString()

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_obd_auto_reconnect_attempts(Math.round(value))
                            }
                        }
                    }

                    Text {
                        text: autoReconnectSlider.value === 0 ? "Off" : autoReconnectSlider.value.toString()
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.bold: true
                        Layout.preferredWidth: dp(30)
                        horizontalAlignment: Text.AlignRight
                    }
                }

                SettingDescription {
                    text: "Retry attempts after disconnect (0 = off)"
                }
            }

            SettingsDivider {}

            // OBD Card Style
            ColumnLayout {
                Layout.fillWidth: true
                spacing: App.Spacing.rowSpacing

                SettingsToggle {
                    id: obdCardStyleToggle
                    Layout.fillWidth: true
                    text: "Circular gauges"
                    checked: settingsManager ? settingsManager.get_setting_with_default("obdCardStyleCircular", false) : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_setting("obdCardStyleCircular", checked)
                        }
                    }
                }

                SettingDescription {
                    text: "Square cards or circular gauges (OBD page only)"
                }
            }
        }
    }

    Component {
        id: parametersContent
        ColumnLayout {
            id: parametersCard
            width: parent ? parent.width : 0
            spacing: App.Spacing.rowSpacing

            // Version counter to force re-evaluation when scan results change
            property int supportedParamsVersion: 0

            // Visible parameters: vehicle-supported if scanned, else original 18
            property var visibleParameters: {
                var _v = supportedParamsVersion;
                if (!settingsManager) return App.OBDParameterModel.allParameters;
                var supported = settingsManager.get_supported_obd_parameters();
                if (supported.length > 0) {
                    return App.OBDParameterModel.allParameters.filter(function(p) {
                        return supported.indexOf(p.id) !== -1;
                    });
                } else {
                    return App.OBDParameterModel.allParameters.filter(function(p) {
                        return App.OBDParameterModel.originalParameters.indexOf(p.id) !== -1;
                    });
                }
            }

            // Current home parameters list (reactive)
            property var homeParams: settingsManager ? settingsManager.get_home_obd_parameters() : []

            function refreshHomeParams() {
                homeParams = settingsManager ? settingsManager.get_home_obd_parameters() : [];
            }

            function isOnHomeGrid(paramId) {
                return homeParams.indexOf(paramId) !== -1;
            }


            // ── Controls row ─────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: dp(6)

                // Select All button
                Button {
                    id: selectAllButton
                    text: "Select All"
                    implicitHeight: App.Spacing.overallSpacing * 2
                    implicitWidth: selectAllButtonText.implicitWidth + App.Spacing.overallSpacing * 1.5

                    scale: selectAllMouseArea.pressed ? 0.95 : 1.0
                    opacity: selectAllMouseArea.pressed ? 0.8 : 1.0

                    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutBack } }
                    Behavior on opacity { NumberAnimation { duration: 100 } }

                    background: Rectangle {
                        color: App.Style.accent
                        radius: 4
                    }

                    contentItem: Text {
                        id: selectAllButtonText
                        text: selectAllButton.text
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        id: selectAllMouseArea
                        anchors.fill: parent
                        onClicked: {
                            if (settingsManager) {
                                parametersCard.visibleParameters.forEach(function(p) {
                                    settingsManager.save_obd_parameter_enabled(p.id, true);
                                });
                            }
                        }
                    }
                }

                // Deselect All button
                Button {
                    id: deselectAllButton
                    text: "Deselect All"
                    implicitHeight: App.Spacing.overallSpacing * 2
                    implicitWidth: deselectAllButtonText.implicitWidth + App.Spacing.overallSpacing * 1.5

                    scale: deselectAllMouseArea.pressed ? 0.95 : 1.0
                    opacity: deselectAllMouseArea.pressed ? 0.8 : 1.0

                    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutBack } }
                    Behavior on opacity { NumberAnimation { duration: 100 } }

                    background: Rectangle {
                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.5)
                        radius: 4
                    }

                    contentItem: Text {
                        id: deselectAllButtonText
                        text: deselectAllButton.text
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        id: deselectAllMouseArea
                        anchors.fill: parent
                        onClicked: {
                            if (settingsManager) {
                                parametersCard.visibleParameters.forEach(function(p) {
                                    settingsManager.save_obd_parameter_enabled(p.id, false);
                                });
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Parameter counter
                Text {
                    id: enabledCount
                    text: "0 of 0 enabled"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: App.Style.fontFamily

                    function updateEnabledCount() {
                        if (!settingsManager) return;
                        var visible = parametersCard.visibleParameters;
                        var count = 0;
                        visible.forEach(function(p) {
                            var isOrig = App.OBDParameterModel.isOriginalParameter(p.id);
                            if (settingsManager.get_obd_parameter_enabled(p.id, isOrig)) {
                                count++;
                            }
                        });
                        enabledCount.text = count + " of " + visible.length + " enabled";
                    }

                    Component.onCompleted: updateEnabledCount()
                }
            }

            // ── Auto-scan progress (shown during scan) ───────────
            RowLayout {
                id: scanProgressRow
                Layout.fillWidth: true
                visible: scanProgressRow.isScanning || scanProgressRow.scanStatus !== ""
                spacing: dp(10)

                property bool isScanning: false
                property string scanStatus: ""
                property int scanProgress: 0

                Rectangle {
                    Layout.fillWidth: true
                    height: 6
                    color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.2)
                    radius: 3

                    Rectangle {
                        width: parent.width * (scanProgressRow.scanProgress / 100)
                        height: parent.height
                        color: App.Style.statusSuccess
                        radius: 3
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                Text {
                    text: scanProgressRow.scanStatus
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.9
                    font.family: App.Style.fontFamily
                }

                Connections {
                    target: obdManager
                    function onScanProgressChanged(progress, message) {
                        scanProgressRow.scanProgress = progress;
                        scanProgressRow.scanStatus = message;
                        scanProgressRow.isScanning = progress < 100;
                    }
                    function onScanCompleteChanged(supportedParams) {
                        scanProgressRow.isScanning = false;
                        // Auto-enable all supported parameters
                        if (supportedParams.length > 0 && settingsManager) {
                            supportedParams.forEach(function(param) {
                                settingsManager.save_obd_parameter_enabled(param, true);
                            });
                        }
                    }
                }
            }

            // ── Two-panel layout ─────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: dp(500)
                spacing: dp(12)

                // ── LEFT PANEL: Available parameters ─────────────
                Rectangle {
                    id: leftPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1 // Equal weight
                    color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.3)
                    radius: 4
                    border.width: 1
                    border.color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.1)

                    // Drop area for returning cards from the right panel
                    DropArea {
                        id: leftDropArea
                        anchors.fill: parent
                        keys: ["obdParam"]

                        onDropped: function(drop) {
                            if (pageRoot.dragSource === "right" && pageRoot.draggedParam !== "") {
                                // Remove from home grid — defer save so delegate isn't
                                // destroyed while the MouseArea onReleased is still running
                                var hp = settingsManager.get_home_obd_parameters();
                                var idx = hp.indexOf(pageRoot.draggedParam);
                                if (idx !== -1) {
                                    hp.splice(idx, 1);
                                    var hpCopy = hp.slice();
                                    Qt.callLater(function() {
                                        settingsManager.save_home_obd_parameters(hpCopy);
                                    });
                                }
                            }
                        }

                        // Highlight when dragging from right
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: 2
                            border.color: App.Style.accent
                            radius: 4
                            opacity: leftDropArea.containsDrag && pageRoot.dragSource === "right" ? 0.8 : 0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: dp(8)
                        spacing: 0

                        Text {
                            text: "Available Parameters"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                            font.bold: true
                            Layout.bottomMargin: dp(6)
                        }

                        MouseArea {
                            id: leftScrollContainer
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            acceptedButtons: Qt.NoButton

                            property real scrollY: 0
                            property real maxScroll: Math.max(0, leftColumn.implicitHeight - leftScrollContainer.height)

                            onWheel: function(wheel) {
                                var delta = wheel.angleDelta.y / 120 * dp(40);
                                scrollY = Math.max(0, Math.min(maxScroll, scrollY - delta));
                                wheel.accepted = true;
                            }

                            Column {
                                id: leftColumn
                                width: parent.width
                                spacing: dp(6)
                                y: -leftScrollContainer.scrollY
                                Behavior on y { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                                Repeater {
                                    id: leftRepeater
                                    model: parametersCard.visibleParameters

                                    Rectangle {
                                        id: leftCard
                                        width: leftColumn.width
                                        height: dp(56)
                                        radius: 4

                                        property string paramId: modelData.id
                                        property bool isOriginal: App.OBDParameterModel.isOriginalParameter(paramId)
                                        property bool isEnabled: settingsManager ?
                                            settingsManager.get_obd_parameter_enabled(paramId, isOriginal) : isOriginal
                                        property bool onHome: parametersCard.isOnHomeGrid(paramId)
                                        property var info: App.OBDParameterModel.getParamInfo(paramId)
                                        property real liveValue: App.OBDParameterModel.paramValues[paramId] || 0

                                        color: onHome ?
                                            Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.12) :
                                            (isEnabled ?
                                                Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.06) :
                                                Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.5))
                                        border.width: 1
                                        border.color: onHome ?
                                            App.Style.accent :
                                            (isEnabled ?
                                                Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                                Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.15))
                                        opacity: (pageRoot.draggedParam === paramId) ? 0.3 : 1.0

                                        Behavior on opacity { NumberAnimation { duration: 150 } }
                                        Behavior on color { ColorAnimation { duration: 150 } }
                                        Behavior on border.color { ColorAnimation { duration: 150 } }

                                        // Update when settings change
                                        Connections {
                                            target: settingsManager
                                            function onObdParametersChanged() {
                                                leftCard.isEnabled = settingsManager.get_obd_parameter_enabled(leftCard.paramId, leftCard.isOriginal);
                                            }
                                            function onHomeOBDParametersChanged() {
                                                parametersCard.refreshHomeParams();
                                                leftCard.onHome = parametersCard.isOnHomeGrid(leftCard.paramId);
                                            }
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: dp(10)
                                            anchors.rightMargin: dp(10)
                                            spacing: dp(8)

                                            // Param name
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Text {
                                                    text: modelData.title
                                                    color: leftCard.isEnabled ? App.Style.primaryTextColor :
                                                        Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.5)
                                                    font.pixelSize: App.Spacing.overallText * 0.9
                                                    font.family: App.Style.fontFamily
                                                    font.bold: leftCard.onHome
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                // Mini progress bar
                                                Rectangle {
                                                    Layout.fillWidth: true
                                                    height: 3
                                                    color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.1)
                                                    radius: 1.5
                                                    visible: leftCard.isEnabled

                                                    Rectangle {
                                                        width: {
                                                            var range = leftCard.info.maxValue - leftCard.info.minValue;
                                                            if (range <= 0) return 0;
                                                            return Math.max(2, parent.width * Math.min(1, Math.max(0, (leftCard.liveValue - leftCard.info.minValue) / range)));
                                                        }
                                                        height: parent.height
                                                        color: App.Style.accent
                                                        radius: 1.5
                                                    }
                                                }
                                            }

                                            // Live value display
                                            Text {
                                                text: leftCard.liveValue.toFixed(1) + " " + leftCard.info.unit
                                                color: leftCard.isEnabled ?
                                                    App.Style.primaryTextColor :
                                                    Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.4)
                                                font.pixelSize: App.Spacing.overallText * 0.85
                                                font.family: App.Style.fontFamily
                                                font.bold: leftCard.isEnabled
                                                horizontalAlignment: Text.AlignRight
                                                Layout.preferredWidth: dp(80)
                                            }

                                            // Home badge
                                            Rectangle {
                                                width: dp(8)
                                                height: dp(8)
                                                radius: 4
                                                color: App.Style.accent
                                                visible: leftCard.onHome
                                            }
                                        }

                                        // Interaction: click to toggle, click-and-drag to move
                                        // No parent Flickable — events come straight to us
                                        MouseArea {
                                            id: leftCardMouse
                                            anchors.fill: parent
                                            preventStealing: true

                                            property bool dragging: false
                                            property bool scrolling: false
                                            property real startX: 0
                                            property real startY: 0
                                            property real lastY: 0
                                            property real dragThreshold: dp(8)

                                            onPressed: function(mouse) {
                                                dragging = false;
                                                scrolling = false;
                                                startX = mouse.x;
                                                startY = mouse.y;
                                                lastY = mouse.y;
                                                rootFlickable.interactive = false;
                                            }

                                            onPositionChanged: function(mouse) {
                                                if (dragging) {
                                                    // Already dragging — update proxy position
                                                    var globalPos = leftCardMouse.mapToItem(pageRoot, mouse.x, mouse.y);
                                                    dragProxy.x = globalPos.x - dragProxy.width / 2;
                                                    dragProxy.y = globalPos.y - dragProxy.height / 2;
                                                } else if (scrolling) {
                                                    // Vertical scroll — manually scroll the list
                                                    var dy = mouse.y - lastY;
                                                    leftScrollContainer.scrollY = Math.max(0, Math.min(leftScrollContainer.maxScroll, leftScrollContainer.scrollY - dy));
                                                    lastY = mouse.y;
                                                } else {
                                                    var dx = mouse.x - startX;
                                                    var dy2 = mouse.y - startY;
                                                    var dist = Math.sqrt(dx * dx + dy2 * dy2);

                                                    if (dist > dragThreshold) {
                                                        if (Math.abs(dy2) > Math.abs(dx) * 1.5) {
                                                            // Vertical gesture → scroll the list
                                                            scrolling = true;
                                                            lastY = mouse.y;
                                                        } else {
                                                            // Horizontal/diagonal → drag-and-drop
                                                            if (!leftCard.isEnabled) return;
                                                            if (parametersCard.homeParams.length >= 8 && !leftCard.onHome) return;

                                                            dragging = true;
                                                            pageRoot.draggedParam = leftCard.paramId;
                                                            pageRoot.dragSource = "left";
                                                            pageRoot.dragSourceIndex = -1;

                                                            var globalPos2 = leftCard.mapToItem(pageRoot, 0, 0);
                                                            dragProxy.x = globalPos2.x;
                                                            dragProxy.y = globalPos2.y;
                                                            dragProxy.width = leftCard.width;
                                                            dragProxy.height = leftCard.height;
                                                            dragProxy.paramId = leftCard.paramId;
                                                            dragProxy.paramTitle = modelData.title;
                                                            dragProxy.paramValue = leftCard.liveValue.toFixed(1) + " " + leftCard.info.unit;
                                                            dragProxy.visible = true;
                                                            dragProxy.Drag.active = true;
                                                        }
                                                    }
                                                }
                                            }

                                            onReleased: {
                                                rootFlickable.interactive = true;
                                                if (dragging && dragProxy.visible) {
                                                    dragProxy.Drag.drop();
                                                    dragProxy.visible = false;
                                                    dragProxy.Drag.active = false;
                                                } else if (!dragging && !scrolling) {
                                                    // Simple click — toggle enabled
                                                    if (settingsManager) {
                                                        settingsManager.save_obd_parameter_enabled(leftCard.paramId, !leftCard.isEnabled);
                                                        updateCountTimer.restart();
                                                    }
                                                }
                                                dragging = false;
                                                scrolling = false;
                                                pageRoot.draggedParam = "";
                                                pageRoot.dragSource = "";
                                                pageRoot.dragSourceIndex = -1;
                                                pageRoot.dropPreviewIndex = -1;
                                            }

                                            onCanceled: {
                                                rootFlickable.interactive = true;
                                                if (dragProxy.visible) {
                                                    dragProxy.visible = false;
                                                    dragProxy.Drag.active = false;
                                                }
                                                dragging = false;
                                                scrolling = false;
                                                pageRoot.draggedParam = "";
                                                pageRoot.dragSource = "";
                                                pageRoot.dropPreviewIndex = -1;
                                            }
                                        }
                                    }
                                }
                            }

                        }
                    }
                }

                // ── RIGHT PANEL: Home grid preview ───────────────
                Rectangle {
                    id: rightPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1 // Equal weight
                    color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.3)
                    radius: 4
                    border.width: 1
                    border.color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.1)

                    property int homeCount: parametersCard.homeParams.length
                    property int gridColumns: homeCount <= 5 ? 1 : 2

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: dp(8)
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: dp(6)

                            Text {
                                text: "Home Grid"
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.85
                                font.family: App.Style.fontFamily
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: rightPanel.homeCount + " / 8"
                                color: rightPanel.homeCount >= 8 ? App.Style.statusWarning : App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.family: App.Style.fontFamily
                            }
                        }

                        // The grid itself with a DropArea
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            // Drop area for adding new cards (covers entire grid area)
                            DropArea {
                                id: rightDropArea
                                anchors.fill: parent
                                keys: ["obdParam"]

                                onEntered: function(drag) {
                                    pageRoot.dropPreviewIndex = calculateDropIndex(drag.x, drag.y);
                                }

                                onPositionChanged: function(drag) {
                                    pageRoot.dropPreviewIndex = calculateDropIndex(drag.x, drag.y);
                                }

                                onExited: {
                                    pageRoot.dropPreviewIndex = -1;
                                }

                                onDropped: function(drop) {
                                    pageRoot.dropPreviewIndex = -1;
                                    // Defer all saves — the drop is triggered from a card
                                    // MouseArea's onReleased. Saving synchronously would
                                    // destroy/recreate Repeater delegates mid-handler,
                                    // preventing cleanup code from running (floating proxy bug).
                                    if (pageRoot.dragSource === "left" && pageRoot.draggedParam !== "") {
                                        // Add to home grid at drop position
                                        var hp = settingsManager.get_home_obd_parameters();
                                        if (hp.indexOf(pageRoot.draggedParam) === -1 && hp.length < 8) {
                                            var targetIdx = calculateDropIndex(drop.x, drop.y);
                                            hp.splice(targetIdx, 0, pageRoot.draggedParam);
                                            var hpCopy = hp.slice();
                                            Qt.callLater(function() {
                                                settingsManager.save_home_obd_parameters(hpCopy);
                                            });
                                        }
                                    } else if (pageRoot.dragSource === "right" && pageRoot.draggedParam !== "") {
                                        // Swap positions within grid
                                        var hp2 = settingsManager.get_home_obd_parameters();
                                        var fromIdx = pageRoot.dragSourceIndex;
                                        if (fromIdx >= 0 && fromIdx < hp2.length) {
                                            var targetIdx = calculateDropIndex(drop.x, drop.y);
                                            if (targetIdx !== fromIdx && targetIdx >= 0 && targetIdx < hp2.length) {
                                                var temp = hp2[fromIdx];
                                                hp2[fromIdx] = hp2[targetIdx];
                                                hp2[targetIdx] = temp;
                                                var hp2Copy = hp2.slice();
                                                Qt.callLater(function() {
                                                    settingsManager.save_home_obd_parameters(hp2Copy);
                                                });
                                            }
                                        }
                                    }
                                }

                                function calculateDropIndex(dropX, dropY) {
                                    // Map drop coords to a grid cell index
                                    var cols = rightPanel.gridColumns;
                                    var totalItems = parametersCard.homeParams.length;
                                    var rows = Math.ceil(totalItems / cols);
                                    if (rows <= 0 || cols <= 0) return 0;

                                    var cellW = homeGrid.width / cols;
                                    var cellH = homeGrid.height / rows;

                                    var col = Math.floor(dropX / cellW);
                                    var row = Math.floor(dropY / cellH);

                                    col = Math.max(0, Math.min(col, cols - 1));
                                    row = Math.max(0, Math.min(row, rows - 1));

                                    var idx = row * cols + col;
                                    return Math.min(idx, totalItems);
                                }

                                // Highlight when dragging over
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: 2
                                    border.color: App.Style.accent
                                    radius: 4
                                    opacity: rightDropArea.containsDrag ? 0.8 : 0
                                    Behavior on opacity { NumberAnimation { duration: 150 } }
                                }
                            }

                            // Empty state placeholder
                            Text {
                                anchors.centerIn: parent
                                text: "Drag parameters here"
                                color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.3)
                                font.pixelSize: App.Spacing.overallText
                                font.family: App.Style.fontFamily
                                visible: rightPanel.homeCount === 0
                            }

                            // Home grid
                            GridLayout {
                                id: homeGrid
                                anchors.fill: parent
                                columns: rightPanel.gridColumns
                                columnSpacing: dp(6)
                                rowSpacing: dp(6)

                                Repeater {
                                    id: homeRepeater
                                    model: parametersCard.homeParams

                                    Rectangle {
                                        id: homeCard
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.columnSpan: (index === 0 && rightPanel.gridColumns === 2 && parametersCard.homeParams.length % 2 !== 0) ? 2 : 1
                                        color: App.Style.backgroundColor
                                        border.color: App.Style.accent
                                        border.width: 1
                                        radius: 3

                                        property string paramId: modelData
                                        property var info: App.OBDParameterModel.getParamInfo(paramId)
                                        property real liveValue: App.OBDParameterModel.paramValues[paramId] || 0
                                        property int cardIndex: index

                                        opacity: (pageRoot.draggedParam === paramId && pageRoot.dragSource === "right") ? 0.3 : 1.0
                                        Behavior on opacity { NumberAnimation { duration: 150 } }

                                        // Drop target highlight
                                        Rectangle {
                                            id: dropHighlight
                                            anchors.fill: parent
                                            z: 2
                                            radius: homeCard.radius
                                            color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                                            border.color: App.Style.accent
                                            border.width: 2
                                            opacity: (pageRoot.dropPreviewIndex === homeCard.cardIndex &&
                                                      pageRoot.draggedParam !== homeCard.paramId) ? 1 : 0
                                            visible: opacity > 0

                                            Behavior on opacity { NumberAnimation { duration: 150 } }

                                            // Subtle pulse on the border
                                            SequentialAnimation on border.width {
                                                running: dropHighlight.visible
                                                loops: Animation.Infinite
                                                NumberAnimation { to: 3; duration: 600; easing.type: Easing.InOutQuad }
                                                NumberAnimation { to: 2; duration: 600; easing.type: Easing.InOutQuad }
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                text: pageRoot.draggedParam !== "" ?
                                                    (App.OBDParameterModel.getParamInfo(pageRoot.draggedParam).title || "") : ""
                                                color: App.Style.accent
                                                font.pixelSize: App.Spacing.overallText * 0.75
                                                font.family: App.Style.fontFamily
                                                font.bold: true
                                                opacity: 0.6
                                            }
                                        }

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: dp(5)
                                            spacing: dp(3)

                                            Text {
                                                text: homeCard.info.title.toUpperCase()
                                                font.pixelSize: App.Spacing.overallText * 0.7
                                                font.family: App.Style.fontFamily
                                                color: App.Style.secondaryTextColor
                                                Layout.alignment: Qt.AlignLeft
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: homeCard.liveValue.toFixed(1) + " " + homeCard.info.unit
                                                font.pixelSize: App.Spacing.overallText * 1.1
                                                font.bold: true
                                                font.family: App.Style.fontFamily
                                                color: App.Style.primaryTextColor
                                                Layout.alignment: Qt.AlignLeft
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                height: dp(4)
                                                color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.1)
                                                radius: 2

                                                Rectangle {
                                                    width: {
                                                        var range = homeCard.info.maxValue - homeCard.info.minValue;
                                                        if (range <= 0) return 0;
                                                        return Math.max(2, parent.width * Math.min(1, Math.max(0, (homeCard.liveValue - homeCard.info.minValue) / range)));
                                                    }
                                                    height: parent.height
                                                    color: App.Style.accent
                                                    radius: 2
                                                    Behavior on width { NumberAnimation { duration: 200 } }
                                                }
                                            }
                                        }

                                        // Click-and-drag for reorder or remove
                                        MouseArea {
                                            id: homeCardMouse
                                            anchors.fill: parent
                                            preventStealing: true

                                            property bool dragging: false
                                            property real startX: 0
                                            property real startY: 0
                                            property real dragThreshold: dp(8)

                                            onPressed: function(mouse) {
                                                dragging = false;
                                                startX = mouse.x;
                                                startY = mouse.y;
                                                rootFlickable.interactive = false;
                                            }

                                            onPositionChanged: function(mouse) {
                                                if (dragging) {
                                                    var globalPos = homeCardMouse.mapToItem(pageRoot, mouse.x, mouse.y);
                                                    dragProxy.x = globalPos.x - dragProxy.width / 2;
                                                    dragProxy.y = globalPos.y - dragProxy.height / 2;
                                                } else {
                                                    var dx = mouse.x - startX;
                                                    var dy = mouse.y - startY;
                                                    if (Math.sqrt(dx * dx + dy * dy) > dragThreshold) {
                                                        dragging = true;
                                                        pageRoot.draggedParam = homeCard.paramId;
                                                        pageRoot.dragSource = "right";
                                                        pageRoot.dragSourceIndex = homeCard.cardIndex;

                                                        var globalPos2 = homeCard.mapToItem(pageRoot, 0, 0);
                                                        dragProxy.x = globalPos2.x;
                                                        dragProxy.y = globalPos2.y;
                                                        dragProxy.width = homeCard.width;
                                                        dragProxy.height = homeCard.height;
                                                        dragProxy.paramId = homeCard.paramId;
                                                        dragProxy.paramTitle = homeCard.info.title;
                                                        dragProxy.paramValue = homeCard.liveValue.toFixed(1) + " " + homeCard.info.unit;
                                                        dragProxy.visible = true;
                                                        dragProxy.Drag.active = true;
                                                    }
                                                }
                                            }

                                            onReleased: {
                                                rootFlickable.interactive = true;
                                                if (dragging && dragProxy.visible) {
                                                    dragProxy.Drag.drop();
                                                    dragProxy.visible = false;
                                                    dragProxy.Drag.active = false;
                                                }
                                                dragging = false;
                                                pageRoot.draggedParam = "";
                                                pageRoot.dragSource = "";
                                                pageRoot.dragSourceIndex = -1;
                                                pageRoot.dropPreviewIndex = -1;
                                            }

                                            onCanceled: {
                                                rootFlickable.interactive = true;
                                                if (dragProxy.visible) {
                                                    dragProxy.visible = false;
                                                    dragProxy.Drag.active = false;
                                                }
                                                dragging = false;
                                                pageRoot.draggedParam = "";
                                                pageRoot.dragSource = "";
                                                pageRoot.dropPreviewIndex = -1;
                                            }
                                        }
                                    }
                                }
                            }

                            // Ghost placeholder for left→right append position
                            Rectangle {
                                id: appendGhost
                                z: 5

                                property int _cols: rightPanel.gridColumns
                                property int _count: parametersCard.homeParams.length
                                property int _rows: Math.max(1, Math.ceil(_count / _cols))
                                property real _cellW: _cols > 0 ?
                                    (homeGrid.width - Math.max(0, _cols - 1) * homeGrid.columnSpacing) / _cols :
                                    homeGrid.width
                                property real _cellH: _rows > 0 ?
                                    (homeGrid.height - Math.max(0, _rows - 1) * homeGrid.rowSpacing) / _rows :
                                    homeGrid.height

                                property int _nextCol: _count % _cols
                                property int _nextRow: Math.floor(_count / _cols)

                                visible: rightDropArea.containsDrag &&
                                         pageRoot.dragSource === "left" &&
                                         _count < 8 &&
                                         (_count === 0 || _nextRow < _rows) &&
                                         pageRoot.dropPreviewIndex >= _count

                                x: _nextCol * (_cellW + homeGrid.columnSpacing)
                                y: _nextRow * (_cellH + homeGrid.rowSpacing)
                                width: _cellW
                                height: _cellH

                                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.12)
                                border.color: App.Style.accent
                                border.width: 2
                                radius: 3

                                Behavior on opacity { NumberAnimation { duration: 150 } }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: dp(5)
                                    spacing: dp(3)

                                    Text {
                                        text: dragProxy.paramTitle ? dragProxy.paramTitle.toUpperCase() : ""
                                        font.pixelSize: App.Spacing.overallText * 0.7
                                        font.family: App.Style.fontFamily
                                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.7)
                                        Layout.alignment: Qt.AlignLeft
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: dragProxy.paramValue || ""
                                        font.pixelSize: App.Spacing.overallText * 1.1
                                        font.bold: true
                                        font.family: App.Style.fontFamily
                                        color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.4)
                                        Layout.alignment: Qt.AlignLeft
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }
            }

            // ── Hint text ────────────────────────────────────────
            Text {
                Layout.fillWidth: true
                Layout.topMargin: dp(4)
                text: {
                    var _v = parametersCard.supportedParamsVersion;
                    var supported = settingsManager ? settingsManager.get_supported_obd_parameters() : [];
                    if (supported.length === 0) {
                        return "Showing core parameters. Connect OBD to auto-detect vehicle-supported parameters.";
                    } else {
                        return "Showing " + supported.length + " vehicle-supported parameters from last scan.";
                    }
                }
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "Tap to enable/disable. Drag sideways to add to home grid. Reorder by dragging within the grid."
                color: Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.7)
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
                wrapMode: Text.WordWrap
            }

            // ── Settings change connections ───────────────────────
            Timer {
                id: updateCountTimer
                interval: 10
                repeat: false
                onTriggered: enabledCount.updateEnabledCount()
            }

            Connections {
                target: settingsManager
                function onObdParametersChanged() {
                    updateCountTimer.restart();
                }
                function onSupportedOBDParametersChanged() {
                    parametersCard.supportedParamsVersion++;
                    updateCountTimer.restart();
                }
                function onHomeOBDParametersChanged() {
                    parametersCard.refreshHomeParams();
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
                model: pageRoot.tileModel

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
