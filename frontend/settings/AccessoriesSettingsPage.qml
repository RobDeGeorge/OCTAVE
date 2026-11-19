import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects
import ".." as App

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // ── Volume Knob serial ports ({port, label, description, isEsp32S3},
    // ESP32-S3 first; updated by every scan, including the refresh button) ──
    readonly property var portsModel: (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager)
                                      ? esp32VolumeManager.availablePorts : []

    function refreshPorts() {
        if (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager)
            esp32VolumeManager.refreshPorts()
    }

    Component.onCompleted: refreshPorts()

    // ── Gesture mapping model ──────────────────────────────────
    // The vocabulary is GestureManager's (gesturevocabulary.h)
    readonly property var _actions: (typeof gestureSensor !== "undefined" && gestureSensor) ? gestureSensor.actions : []
    readonly property var actionOptions: _actions.map(function(a) { return a.key })
    readonly property var actionLabels: {
        var labels = {}
        for (var i = 0; i < _actions.length; i++) labels[_actions[i].key] = _actions[i].label
        return labels
    }
    readonly property var gestureNames: (typeof gestureSensor !== "undefined" && gestureSensor) ? gestureSensor.gestures : []

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    // Shared state line for the three serial / I²C accessories.
    function _device(enabled, connected) {
        if (!enabled) return { state: "Off" }
        return { state: connected ? "Connected" : "Not found", active: connected,
                 status: connected ? App.Style.statusConnected : App.Style.statusDisconnected }
    }

    property var tileModel: [
        { cardId: "accessories_volume_knob", title: "Volume Knob", component: volumeKnobContent,
          summary: function() {
              return pageRoot._device(settingsManager && settingsManager.esp32VolumeEnabled,
                  typeof esp32VolumeManager !== "undefined" && esp32VolumeManager ? esp32VolumeManager.connected : false)
          } },
        { cardId: "accessories_imu", title: "IMU Sensor", component: imuContent,
          summary: function() {
              return pageRoot._device(settingsManager && settingsManager.imuEnabled,
                  typeof berryIMU !== "undefined" && berryIMU ? berryIMU.connected : false)
          } },
        { cardId: "accessories_gesture", title: "Gesture Sensor", component: gestureContent,
          summary: function() {
              return pageRoot._device(settingsManager && settingsManager.gestureSensorEnabled,
                  typeof gestureSensor !== "undefined" && gestureSensor ? gestureSensor.connectionStatus === "Connected" : false)
          } }
    ]

    contentWidth: width
    contentHeight: settingsContent.implicitHeight
    flickableDirection: Flickable.VerticalFlick
    clip: true
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1200
    maximumFlickVelocity: 4000
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    // ─────────────────────────────────────────── Volume Knob
    Component {
        id: volumeKnobContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            // ── Connection ──
            SettingCategory {
                title: "Connection"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    Rectangle {
                        id: esp32StatusDot
                        width: pageRoot.dp(14)
                        height: pageRoot.dp(14)
                        radius: pageRoot.dpMin(7, 2)
                        color: (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager && esp32VolumeManager.isConnected()) ? App.Style.statusConnected : App.Style.statusDisconnected

                        Connections {
                            target: typeof esp32VolumeManager !== "undefined" ? esp32VolumeManager : null
                            function onConnectionStatusChanged(status) {
                                esp32StatusDot.color = esp32VolumeManager.isConnected() ? App.Style.statusConnected : App.Style.statusDisconnected
                            }
                        }
                    }

                    Text {
                        id: esp32StatusText
                        text: (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager) ? esp32VolumeManager.getConnectionDetail() : "Not initialized"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        Layout.fillWidth: true

                        Connections {
                            target: typeof esp32VolumeManager !== "undefined" ? esp32VolumeManager : null
                            function onConnectionDetailChanged(detail) {
                                esp32StatusText.text = esp32VolumeManager.getConnectionDetail()
                            }
                        }
                    }
                }

                // Serial Port Selection with chips
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        SettingLabel {
                            text: "USB Serial Port"
                            Layout.fillWidth: true
                        }

                        // Refresh button chip
                        Rectangle {
                            width: refreshChipText.width + App.Spacing.overallSpacing * 2
                            height: App.Spacing.formElementHeight * 0.7
                            radius: App.EnvironmentTheme.active.chipRadius === -1
                                ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)
                            color: refreshChipArea.containsMouse ? App.Style.hoverColor : "transparent"
                            border.width: 1
                            border.color: App.EnvironmentTheme.active.chipAccentBorder
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                : App.Style.hoverColor

                            scale: refreshChipArea.pressed ? 0.97 : (refreshChipArea.containsMouse ? 1.03 : 1.0)
                            Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }

                            Text {
                                id: refreshChipText
                                text: "⟳ Refresh"
                                anchors.centerIn: parent
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.family: App.Style.fontFamily
                            }

                            MouseArea {
                                id: refreshChipArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: pageRoot.refreshPorts()
                            }
                        }
                    }

                    // One chip per port, an ESP32-S3 (the knob's receiver) starred
                    SettingsChips {
                        id: portChips
                        function label(p) { return (p.isEsp32S3 ? "\u2605 " : "") + p.port }
                        options: pageRoot.portsModel.map(function(p) { return portChips.label(p) })
                        currentValue: {
                            var port = settingsManager ? settingsManager.esp32VolumePort : ""
                            for (var i = 0; i < pageRoot.portsModel.length; i++)
                                if (pageRoot.portsModel[i].port === port) return portChips.label(pageRoot.portsModel[i])
                            return ""
                        }
                        onSelected: function(value) {
                            if (!settingsManager) return
                            var port = value.replace("\u2605 ", "")
                            settingsManager.save_esp32_volume_port(port)
                            if (!settingsManager.esp32VolumeEnabled)
                                settingsManager.save_esp32_volume_enabled(true)
                            if (esp32VolumeManager) {
                                esp32VolumeManager.resetReconnectAttempts()
                                esp32VolumeManager.connectDevice()
                            }
                        }
                    }

                    SettingDescription {
                        visible: {
                            if (!settingsManager || !settingsManager.esp32VolumePort) return false
                            for (var i = 0; i < pageRoot.portsModel.length; i++) {
                                if (pageRoot.portsModel[i].port === settingsManager.esp32VolumePort) {
                                    return true
                                }
                            }
                            return false
                        }
                        text: {
                            if (!settingsManager || !settingsManager.esp32VolumePort) return ""
                            for (var i = 0; i < pageRoot.portsModel.length; i++) {
                                if (pageRoot.portsModel[i].port === settingsManager.esp32VolumePort) {
                                    return pageRoot.portsModel[i].description
                                }
                            }
                            return ""
                        }
                    }
                }
            }

            // ── Configuration ──
            SettingCategory {
                title: "Volume Step"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Change per Click"
                            Layout.fillWidth: true
                        }

                        ValueDisplay {
                            text: settingsManager ? settingsManager.esp32VolumeStepSize.toFixed(2) + "%" : "1.00%"
                        }
                    }

                    SettingsChips {
                        options: ["0.25%", "0.5%", "1%", "2%", "5%"]
                        currentValue: settingsManager ? settingsManager.esp32VolumeStepSize + "%" : "1%"
                        onSelected: function(value) {
                            if (settingsManager) settingsManager.save_esp32_volume_step_size(parseFloat(value))
                        }
                    }
                }
            }

            // ── LED Indicator ──
            SettingCategory {
                title: "LED"

                SettingsToggle {
                    id: ledSleepToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: "Keep LEDs on"
                    checked: settingsManager ? !settingsManager.esp32LedSleepEnabled : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_esp32_led_sleep_enabled(!checked)
                        }
                    }
                }

                SettingDescription {
                    text: "Off: the LEDs sleep after 5 seconds idle"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingLabel {
                        text: "Color Mode"
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Item {
                            id: themeModeChipWrapper
                            width: themeModeChipRect.width
                            height: themeModeChipRect.height
                            property bool isSelected: settingsManager && settingsManager.esp32LedColorMode === "theme"

                            Rectangle {
                                anchors.centerIn: themeModeChipRect
                                width: themeModeChipRect.width + 4
                                height: themeModeChipRect.height + 4
                                radius: themeModeChipRect.radius + 2
                                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                visible: App.EnvironmentTheme.active.chipAccentBorder && themeModeChipWrapper.isSelected
                            }

                            Rectangle {
                                id: themeModeChipRect
                                width: themeModeContent.width + App.Spacing.overallSpacing * 3
                                height: App.Spacing.formElementHeight * 0.9
                                radius: App.EnvironmentTheme.active.chipRadius === -1
                                    ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)
                                color: themeModeChipWrapper.isSelected ? App.Style.accent : App.Style.hoverColor

                                border.width: App.EnvironmentTheme.active.chipAccentBorder
                                    ? 1
                                    : (themeModeChipWrapper.isSelected ? 0 : 1)
                                border.color: App.EnvironmentTheme.active.chipAccentBorder
                                    ? (themeModeChipWrapper.isSelected
                                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                                    : Qt.rgba(App.Style.primaryTextColor.r,
                                              App.Style.primaryTextColor.g,
                                              App.Style.primaryTextColor.b, 0.1)

                                Row {
                                    id: themeModeContent
                                    anchors.centerIn: parent
                                    spacing: pageRoot.dp(6)

                                    Text {
                                        text: "✨"
                                        font.pixelSize: App.Spacing.overallText
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: "Theme Colors"
                                        color: themeModeChipWrapper.isSelected ? "white" : App.Style.primaryTextColor
                                        font.pixelSize: App.Spacing.overallText
                                        font.family: App.Style.fontFamily
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                MouseArea {
                                    id: themeModeArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (settingsManager) {
                                            settingsManager.save_esp32_led_color_mode("theme")
                                        }
                                    }
                                    onEntered: themeModeChipRect.scale = 1.03
                                    onExited: themeModeChipRect.scale = 1.0
                                }

                                layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && themeModeChipWrapper.isSelected
                                layer.effect: DropShadow {
                                    horizontalOffset: 0
                                    verticalOffset: 2
                                    radius: 4.0
                                    samples: 9
                                    color: Qt.rgba(0, 0, 0, 0.2)
                                }

                                Behavior on scale {
                                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                                }
                            }
                        }

                        Item {
                            id: staticModeChipWrapper
                            width: staticModeChipRect.width
                            height: staticModeChipRect.height
                            property bool isSelected: settingsManager && settingsManager.esp32LedColorMode === "static"

                            Rectangle {
                                anchors.centerIn: staticModeChipRect
                                width: staticModeChipRect.width + 4
                                height: staticModeChipRect.height + 4
                                radius: staticModeChipRect.radius + 2
                                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                visible: App.EnvironmentTheme.active.chipAccentBorder && staticModeChipWrapper.isSelected
                            }

                            Rectangle {
                                id: staticModeChipRect
                                width: staticModeContent.width + App.Spacing.overallSpacing * 3
                                height: App.Spacing.formElementHeight * 0.9
                                radius: App.EnvironmentTheme.active.chipRadius === -1
                                    ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)
                                color: staticModeChipWrapper.isSelected ? App.Style.accent : App.Style.hoverColor

                                border.width: App.EnvironmentTheme.active.chipAccentBorder
                                    ? 1
                                    : (staticModeChipWrapper.isSelected ? 0 : 1)
                                border.color: App.EnvironmentTheme.active.chipAccentBorder
                                    ? (staticModeChipWrapper.isSelected
                                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                                    : Qt.rgba(App.Style.primaryTextColor.r,
                                              App.Style.primaryTextColor.g,
                                              App.Style.primaryTextColor.b, 0.1)

                                Row {
                                    id: staticModeContent
                                    anchors.centerIn: parent
                                    spacing: pageRoot.dp(6)

                                    Rectangle {
                                        width: App.Spacing.overallText
                                        height: App.Spacing.overallText
                                        radius: width / 2
                                        color: settingsManager ? settingsManager.esp32LedStaticColor : "#00FFFF"
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.3)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: "Static Color"
                                        color: staticModeChipWrapper.isSelected ? "white" : App.Style.primaryTextColor
                                        font.pixelSize: App.Spacing.overallText
                                        font.family: App.Style.fontFamily
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                MouseArea {
                                    id: staticModeArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (settingsManager) {
                                            settingsManager.save_esp32_led_color_mode("static")
                                        }
                                    }
                                    onEntered: staticModeChipRect.scale = 1.03
                                    onExited: staticModeChipRect.scale = 1.0
                                }

                                layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && staticModeChipWrapper.isSelected
                                layer.effect: DropShadow {
                                    horizontalOffset: 0
                                    verticalOffset: 2
                                    radius: 4.0
                                    samples: 9
                                    color: Qt.rgba(0, 0, 0, 0.2)
                                }

                                Behavior on scale {
                                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                                }
                            }
                        }
                    }
                }

                // Static Color Picker
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing
                    visible: settingsManager && settingsManager.esp32LedColorMode === "static"

                    SettingLabel {
                        text: "Static Color"
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing * 0.8

                        Repeater {
                            model: [
                                { color: "#00FFFF", name: "Cyan" },
                                { color: "#FF0080", name: "Pink" },
                                { color: "#00FF00", name: "Green" },
                                { color: "#FF8000", name: "Orange" },
                                { color: "#8000FF", name: "Purple" },
                                { color: "#FFFF00", name: "Yellow" },
                                { color: "#0080FF", name: "Blue" },
                                { color: "#FF0000", name: "Red" },
                                { color: "#FFFFFF", name: "White" }
                            ]

                            Item {
                                id: colorChipWrapper
                                width: colorChipRect.width
                                height: colorChipRect.height
                                property bool isSelected: settingsManager && settingsManager.esp32LedStaticColor.toUpperCase() === modelData.color

                                Rectangle {
                                    anchors.centerIn: colorChipRect
                                    width: colorChipRect.width + 4
                                    height: colorChipRect.height + 4
                                    radius: colorChipRect.radius + 2
                                    color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                    visible: App.EnvironmentTheme.active.chipAccentBorder && colorChipWrapper.isSelected
                                }

                                Rectangle {
                                    id: colorChipRect
                                    width: colorChipRow.width + App.Spacing.overallSpacing * 2
                                    height: App.Spacing.formElementHeight * 0.8
                                    radius: App.EnvironmentTheme.active.chipRadius === -1
                                        ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)

                                    color: colorChipWrapper.isSelected ? modelData.color : App.Style.hoverColor
                                    border.width: App.EnvironmentTheme.active.chipAccentBorder ? 1 : 2
                                    border.color: App.EnvironmentTheme.active.chipAccentBorder
                                        ? (colorChipWrapper.isSelected
                                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                            : modelData.color)
                                        : modelData.color

                                    Row {
                                        id: colorChipRow
                                        anchors.centerIn: parent
                                        spacing: pageRoot.dp(6)

                                        Rectangle {
                                            width: pageRoot.dp(12)
                                            height: pageRoot.dp(12)
                                            radius: pageRoot.dpMin(6, 2)
                                            color: modelData.color
                                            border.width: 1
                                            border.color: Qt.rgba(0, 0, 0, 0.2)
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: !colorChipWrapper.isSelected
                                        }

                                        Text {
                                            text: modelData.name
                                            color: colorChipWrapper.isSelected ?
                                                   (modelData.color === "#FFFFFF" || modelData.color === "#FFFF00" || modelData.color === "#00FF00" ? "#000000" : "#FFFFFF") :
                                                   App.Style.primaryTextColor
                                            font.pixelSize: App.Spacing.overallText * 0.9
                                            font.family: App.Style.fontFamily
                                            font.bold: colorChipWrapper.isSelected
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        id: colorChipArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            if (settingsManager) {
                                                settingsManager.save_esp32_led_static_color(modelData.color)
                                            }
                                        }
                                        onEntered: colorChipRect.scale = 1.05
                                        onExited: colorChipRect.scale = 1.0
                                    }

                                    layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && colorChipWrapper.isSelected
                                    layer.effect: DropShadow {
                                        horizontalOffset: 0
                                        verticalOffset: 2
                                        radius: 4.0
                                        samples: 9
                                        color: Qt.rgba(0, 0, 0, 0.2)
                                    }

                                    Behavior on scale {
                                        NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Text {
                            text: "Custom:"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                        }

                        Rectangle {
                            width: pageRoot.dp(32)
                            height: pageRoot.dp(32)
                            radius: pageRoot.dpMin(App.EnvironmentTheme.active.textFieldRadius, 2)
                            color: settingsManager ? settingsManager.esp32LedStaticColor : "#00FFFF"
                            border.width: 2
                            border.color: App.Style.hoverColor
                        }

                        SettingsTextField {
                            id: customColorField
                            Layout.preferredWidth: pageRoot.dp(120)
                            text: settingsManager ? settingsManager.esp32LedStaticColor : "#00FFFF"
                            placeholderText: "#RRGGBB"

                            onEditingFinished: {
                                if (settingsManager && /^#[0-9A-Fa-f]{6}$/.test(text)) {
                                    settingsManager.save_esp32_led_static_color(text.toUpperCase())
                                }
                            }

                            Connections {
                                target: settingsManager
                                function onEsp32LedStaticColorChanged(color) {
                                    customColorField.text = color
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── IMU Sensor
    Component {
        id: imuContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            // ── Connection ──
            SettingCategory {
                title: "Connection"
                description: "BerryIMU v3 on I2C bus 2"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    Rectangle {
                        id: imuStatusDot
                        width: pageRoot.dp(14)
                        height: pageRoot.dp(14)
                        radius: pageRoot.dpMin(7, 2)

                        property string currentStatus: (typeof berryIMU !== "undefined" && berryIMU) ? berryIMU.getConnectionStatus() : "Disconnected"
                        color: currentStatus === "Connected" ? App.Style.statusConnected : App.Style.statusDisconnected

                        Connections {
                            target: typeof berryIMU !== "undefined" ? berryIMU : null
                            function onConnectionStatusChanged(status) {
                                imuStatusDot.currentStatus = status
                                imuStatusDot.color = (status === "Connected") ? App.Style.statusConnected : App.Style.statusDisconnected
                                imuStatusText.text = status
                            }
                        }
                    }

                    Text {
                        id: imuStatusText
                        text: imuStatusDot.currentStatus
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        Layout.fillWidth: true
                    }
                }

                SettingsToggle {
                    id: imuEnabledToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: "IMU Sensor"
                    checked: settingsManager ? settingsManager.imuEnabled : true
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    // BerryIMUManager follows the setting
                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_imu_enabled(checked)
                        }
                    }
                }
            }

            // ── Display ──
            SettingCategory {
                title: "Sensors Page"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Decimal Places"
                            Layout.fillWidth: true
                        }

                        ValueDisplay {
                            id: imuDecimalsValue
                            text: settingsManager ? settingsManager.sensorDecimalPlaces : "1"
                        }
                    }

                    SettingsSlider {
                        id: imuDecimalsSlider
                        from: 0; to: 4; stepSize: 1
                        value: settingsManager ? settingsManager.sensorDecimalPlaces : 1
                        onMoved: {
                            if (settingsManager)
                                settingsManager.save_sensor_decimal_places(value)
                        }
                    }

                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Update Rate"
                            Layout.fillWidth: true
                        }

                        ValueDisplay {
                            id: imuEmitRateValue
                            text: (settingsManager ? settingsManager.sensorEmitRate : 60) + " Hz"
                        }
                    }

                    SettingDescription {
                        text: "Higher is smoother and uses more CPU"
                    }

                    SettingsSlider {
                        id: imuEmitRateSlider
                        from: 10; to: 120; stepSize: 5
                        value: settingsManager ? settingsManager.sensorEmitRate : 60
                        onMoved: {
                            imuEmitRateValue.text = value + " Hz"
                            imuEmitRateApply.restart()
                        }
                    }

                    Timer {
                        id: imuEmitRateApply
                        interval: 200
                        onTriggered: {
                            // BerryIMUManager follows the setting
                            if (settingsManager) {
                                settingsManager.save_sensor_emit_rate(imuEmitRateSlider.value)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingLabel {
                        text: "Visible Cards"
                    }


                    Repeater {
                        model: [
                            { card: "Pitch",       label: "Pitch" },
                            { card: "Roll",        label: "Roll" },
                            { card: "Heading",     label: "Heading" },
                            { card: "GForce",      label: "G-Force" },
                            { card: "Altitude",    label: "Altitude" },
                            { card: "Temperature", label: "Temperature" }
                        ]
                        delegate: SettingsToggle {
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: modelData.label
                            checked: settingsManager ? settingsManager.sensorCards[modelData.card] !== false : true
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(checked) {
                                if (settingsManager) {
                                    settingsManager.save_sensor_card_visible(modelData.card, checked)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── Gesture Sensor
    Component {
        id: gestureContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            // ── Connection ──
            SettingCategory {
                title: "Connection"
                description: "PAJ7620U2 on I2C bus 2"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    Rectangle {
                        id: gestureStatusDot
                        width: pageRoot.dp(14)
                        height: pageRoot.dp(14)
                        radius: pageRoot.dpMin(7, 2)

                        property string currentStatus: (typeof gestureSensor !== "undefined" && gestureSensor) ? gestureSensor.getConnectionStatus() : "Disconnected"
                        color: currentStatus === "Connected" ? App.Style.statusConnected : App.Style.statusDisconnected

                        Connections {
                            target: typeof gestureSensor !== "undefined" ? gestureSensor : null
                            function onConnectionStatusChanged(status) {
                                gestureStatusDot.currentStatus = status
                                gestureStatusDot.color = (status === "Connected") ? App.Style.statusConnected : App.Style.statusDisconnected
                                gestureStatusText.text = status
                            }
                        }
                    }

                    Text {
                        id: gestureStatusText
                        text: gestureStatusDot.currentStatus
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        Layout.fillWidth: true
                    }
                }

                SettingsToggle {
                    id: gestureEnabledToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: "Gesture Sensor"
                    checked: settingsManager ? settingsManager.gestureSensorEnabled : true
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    // GestureManager follows the setting
                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_gesture_sensor_enabled(checked)
                        }
                    }
                }
            }

            // ── Settings ──
            SettingCategory {
                title: "Volume and Timing"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Volume per Swipe"
                            Layout.fillWidth: true
                        }

                        Text {
                            text: settingsManager ? settingsManager.gestureVolumeStep + "%" : "5%"
                            color: App.Style.accent
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.family: App.Style.fontFamily
                            font.bold: true
                        }
                    }

                    Slider {
                        id: gsVolumeStepSlider
                        Layout.fillWidth: true
                        from: 1
                        to: 100
                        stepSize: 1
                        value: settingsManager ? settingsManager.gestureVolumeStep : 5

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_gesture_volume_step(value)
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onGestureVolumeStepChanged(step) {
                                gsVolumeStepSlider.value = step
                            }
                        }

                        background: Rectangle {
                            x: gsVolumeStepSlider.leftPadding
                            y: gsVolumeStepSlider.topPadding + gsVolumeStepSlider.availableHeight / 2 - height / 2
                            implicitWidth: pageRoot.dp(200)
                            implicitHeight: pageRoot.dp(6)
                            width: gsVolumeStepSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            // As SettingsSlider draws it: hoverColor is nearly the card colour in the dark themes
                            color: Qt.rgba(App.Style.settingsSliderColor.r, App.Style.settingsSliderColor.g, App.Style.settingsSliderColor.b, 0.25)

                            Rectangle {
                                // Ends at the handle's center so the rounded end stays under it
                                width: gsVolumeStepSlider.visualPosition * (parent.width - gsVolumeStepSlider.handle.width) + gsVolumeStepSlider.handle.width / 2
                                height: parent.height
                                color: App.Style.accent
                                radius: 3
                            }
                        }

                        handle: Rectangle {
                            x: gsVolumeStepSlider.leftPadding + gsVolumeStepSlider.visualPosition * (gsVolumeStepSlider.availableWidth - width)
                            y: gsVolumeStepSlider.topPadding + gsVolumeStepSlider.availableHeight / 2 - height / 2
                            implicitWidth: pageRoot.dp(20)
                            implicitHeight: pageRoot.dp(20)
                            radius: pageRoot.dpMin(10, 2)
                            color: gsVolumeStepSlider.pressed ? Qt.darker(App.Style.accent, 1.1) : App.Style.accent
                            border.color: "white"
                            border.width: 2

                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: 10
                            onPressed: function(mouse) {
                                var hx = gsVolumeStepSlider.leftPadding + gsVolumeStepSlider.visualPosition * (gsVolumeStepSlider.availableWidth - gsVolumeStepSlider.handle.width)
                                var hy = gsVolumeStepSlider.topPadding + gsVolumeStepSlider.availableHeight / 2 - gsVolumeStepSlider.handle.height / 2
                                if (mouse.x >= hx && mouse.x <= hx + gsVolumeStepSlider.handle.width &&
                                    mouse.y >= hy && mouse.y <= hy + gsVolumeStepSlider.handle.height) {
                                    mouse.accepted = false
                                } else {
                                    mouse.accepted = true
                                }
                            }
                        }
                    }

                    SettingsChips {
                        options: ["1%", "2%", "5%", "10%", "15%", "25%", "50%"]
                        currentValue: settingsManager ? settingsManager.gestureVolumeStep + "%" : "5%"
                        onSelected: function(value) {
                            if (settingsManager) {
                                settingsManager.save_gesture_volume_step(parseInt(value))
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Pause Between Gestures"
                            Layout.fillWidth: true
                        }

                        Text {
                            text: settingsManager ? settingsManager.gestureCooldown + " ms" : "300 ms"
                            color: App.Style.accent
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.family: App.Style.fontFamily
                            font.bold: true
                        }
                    }

                    Slider {
                        id: cooldownSlider
                        Layout.fillWidth: true
                        from: 100
                        to: 2000
                        stepSize: 50
                        value: settingsManager ? settingsManager.gestureCooldown : 300

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_gesture_cooldown(value)   // GestureManager follows
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onGestureCooldownChanged(ms) {
                                cooldownSlider.value = ms
                            }
                        }

                        background: Rectangle {
                            x: cooldownSlider.leftPadding
                            y: cooldownSlider.topPadding + cooldownSlider.availableHeight / 2 - height / 2
                            implicitWidth: pageRoot.dp(200)
                            implicitHeight: pageRoot.dp(6)
                            width: cooldownSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            // As SettingsSlider draws it: hoverColor is nearly the card colour in the dark themes
                            color: Qt.rgba(App.Style.settingsSliderColor.r, App.Style.settingsSliderColor.g, App.Style.settingsSliderColor.b, 0.25)

                            Rectangle {
                                // Ends at the handle's center so the rounded end stays under it
                                width: cooldownSlider.visualPosition * (parent.width - cooldownSlider.handle.width) + cooldownSlider.handle.width / 2
                                height: parent.height
                                color: App.Style.accent
                                radius: 3
                            }
                        }

                        handle: Rectangle {
                            x: cooldownSlider.leftPadding + cooldownSlider.visualPosition * (cooldownSlider.availableWidth - width)
                            y: cooldownSlider.topPadding + cooldownSlider.availableHeight / 2 - height / 2
                            implicitWidth: pageRoot.dp(20)
                            implicitHeight: pageRoot.dp(20)
                            radius: pageRoot.dpMin(10, 2)
                            color: cooldownSlider.pressed ? Qt.darker(App.Style.accent, 1.1) : App.Style.accent
                            border.color: "white"
                            border.width: 2

                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: 10
                            onPressed: function(mouse) {
                                var hx = cooldownSlider.leftPadding + cooldownSlider.visualPosition * (cooldownSlider.availableWidth - cooldownSlider.handle.width)
                                var hy = cooldownSlider.topPadding + cooldownSlider.availableHeight / 2 - cooldownSlider.handle.height / 2
                                if (mouse.x >= hx && mouse.x <= hx + cooldownSlider.handle.width &&
                                    mouse.y >= hy && mouse.y <= hy + cooldownSlider.handle.height) {
                                    mouse.accepted = false
                                } else {
                                    mouse.accepted = true
                                }
                            }
                        }
                    }

                    SettingsChips {
                        options: ["100ms", "200ms", "300ms", "500ms", "1000ms"]
                        currentValue: settingsManager ? settingsManager.gestureCooldown + "ms" : "300ms"
                        onSelected: function(value) {
                            var ms = parseInt(value)
                            if (settingsManager) {
                                settingsManager.save_gesture_cooldown(ms)   // GestureManager follows
                            }
                        }
                    }
                }
            }

            // ── Mapping ──
            SettingCategory {
                title: "Mapping"

                Repeater {
                    model: pageRoot.gestureNames

                    ColumnLayout {
                        id: gestureRow
                        Layout.fillWidth: true
                        spacing: App.Spacing.rowSpacing

                        required property string modelData
                        property string gestureName: modelData

                        property string currentAction: {
                            if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                return gestureSensor.getGestureAction(gestureName)
                            }
                            return "none"
                        }

                        Connections {
                            target: settingsManager
                            function onGestureMappingChanged() {
                                if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                    gestureRow.currentAction = gestureSensor.getGestureAction(gestureRow.gestureName)
                                }
                            }
                        }

                        SettingLabel {
                            text: gestureRow.gestureName
                        }

                        // The chips show the actions' labels; the mapping stores their keys
                        SettingsChips {
                            options: pageRoot.actionOptions.map(function(k) { return pageRoot.actionLabels[k] || k })
                            currentValue: pageRoot.actionLabels[gestureRow.currentAction] || gestureRow.currentAction
                            onSelected: function(label) {
                                if (typeof gestureSensor === "undefined" || !gestureSensor) return
                                var keys = pageRoot.actionOptions
                                for (var i = 0; i < keys.length; i++) {
                                    if ((pageRoot.actionLabels[keys[i]] || keys[i]) === label) {
                                        gestureSensor.setGestureAction(gestureRow.gestureName, keys[i])
                                        return
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: resetContent.width + App.Spacing.overallSpacing * 3
                    Layout.preferredHeight: App.Spacing.formElementHeight * 0.9
                    radius: App.EnvironmentTheme.active.chipRadius === -1
                        ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)

                    color: resetArea.containsMouse ? App.Style.hoverColor : "transparent"

                    border.width: 1
                    border.color: App.EnvironmentTheme.active.chipAccentBorder
                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                        : App.Style.hoverColor

                    scale: resetArea.pressed ? 0.97 : (resetArea.containsMouse ? 1.03 : 1.0)
                    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }

                    Row {
                        id: resetContent
                        anchors.centerIn: parent
                        spacing: pageRoot.dp(6)

                        Text {
                            text: resetArea.armed ? "Confirm reset" : "Reset to Defaults"
                            color: resetArea.armed ? App.Style.statusDanger : App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: resetArea
                        anchors.fill: parent
                        hoverEnabled: true
                        // Every gesture's assignment goes: two taps, as
                        // ConfirmButton does it
                        property bool armed: false
                        onVisibleChanged: if (!visible) armed = false
                        Timer { id: resetArmTimer; interval: 4000; onTriggered: resetArea.armed = false }
                        onClicked: {
                            if (!armed) {
                                armed = true
                                resetArmTimer.restart()
                                return
                            }
                            armed = false
                            resetArmTimer.stop()
                            if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                gestureSensor.resetMappingToDefaults()
                            }
                            if (settingsManager) {
                                settingsManager.reset_gesture_mapping()
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Stacked SettingsCards, for a host that leaves renderCards on (none does now).
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

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
