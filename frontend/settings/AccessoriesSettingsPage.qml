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

    // ── Volume Knob serial ports model ─────────────────────────
    property var portsModel: []

    function refreshPorts() {
        if (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager) {
            try {
                var jsonStr = esp32VolumeManager.get_ports_with_descriptions()
                portsModel = JSON.parse(jsonStr)
            } catch (e) {
                portsModel = []
            }
        }
    }

    Component.onCompleted: refreshPorts()

    // ── Gesture mapping model ──────────────────────────────────
    property var actionOptions: ["next_track", "previous_track", "volume_up", "volume_down", "play_pause", "mute_toggle", "none"]
    property var actionLabels: {
        "next_track": "Next Track",
        "previous_track": "Previous Track",
        "volume_up": "Volume Up",
        "volume_down": "Volume Down",
        "play_pause": "Play / Pause",
        "mute_toggle": "Mute Toggle",
        "none": "Disabled"
    }
    property var gestureNames: ["RIGHT", "LEFT", "UP", "DOWN", "FORWARD", "BACKWARD", "CLOCKWISE", "COUNTER-CLOCKWISE", "WAVE"]

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    property var tileModel: [
        { cardId: "accessories_volume_knob", title: "Volume Knob",    iconSource: App.Style.assetBase + "tile_volume_knob.svg", component: volumeKnobContent },
        { cardId: "accessories_imu",         title: "IMU Sensor",     iconSource: App.Style.assetBase + "tile_imu.svg",         component: imuContent },
        { cardId: "accessories_gesture",     title: "Gesture Sensor", iconSource: App.Style.assetBase + "tile_gesture.svg",     component: gestureContent },
        { cardId: "accessories_phone_dock",  title: "Phone Dock",     iconSource: App.Style.assetBase + "tile_phone_dock.svg",  component: phoneDockContent }
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
                        color: (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager && esp32VolumeManager.is_connected()) ? App.Style.statusConnected : App.Style.statusDisconnected

                        Connections {
                            target: typeof esp32VolumeManager !== "undefined" ? esp32VolumeManager : null
                            function onConnectionStatusChanged(status) {
                                esp32StatusDot.color = esp32VolumeManager.is_connected() ? App.Style.statusConnected : App.Style.statusDisconnected
                            }
                        }
                    }

                    Text {
                        id: esp32StatusText
                        text: (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager) ? esp32VolumeManager.get_connection_detail() : "Not initialized"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        Layout.fillWidth: true

                        Connections {
                            target: typeof esp32VolumeManager !== "undefined" ? esp32VolumeManager : null
                            function onConnectionDetailChanged(detail) {
                                esp32StatusText.text = esp32VolumeManager.get_connection_detail()
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

                    // Port chips using Flow layout
                    Flow {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Repeater {
                            model: pageRoot.portsModel

                            Item {
                                id: portChipWrapper
                                width: portChipRect.width
                                height: portChipRect.height

                                property bool isSelected: settingsManager && settingsManager.esp32VolumePort === modelData.port

                                Rectangle {
                                    anchors.centerIn: portChipRect
                                    width: portChipRect.width + 4
                                    height: portChipRect.height + 4
                                    radius: portChipRect.radius + 2
                                    color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                    visible: App.EnvironmentTheme.active.chipAccentBorder && portChipWrapper.isSelected
                                }

                                Rectangle {
                                    id: portChipRect
                                    width: portChipContent.width + App.Spacing.overallSpacing * 3
                                    height: App.Spacing.formElementHeight * 0.9
                                    radius: App.EnvironmentTheme.active.chipRadius === -1
                                        ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)

                                    color: portChipWrapper.isSelected ? App.Style.accent : App.Style.hoverColor

                                    border.width: App.EnvironmentTheme.active.chipAccentBorder
                                        ? 1
                                        : (portChipWrapper.isSelected ? 0 : 1)
                                    border.color: App.EnvironmentTheme.active.chipAccentBorder
                                        ? (portChipWrapper.isSelected
                                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                                        : Qt.rgba(App.Style.primaryTextColor.r,
                                                  App.Style.primaryTextColor.g,
                                                  App.Style.primaryTextColor.b, 0.1)

                                    Row {
                                        id: portChipContent
                                        anchors.centerIn: parent
                                        spacing: pageRoot.dp(6)

                                        Text {
                                            visible: modelData.isEsp32S3
                                            text: "★"
                                            color: portChipWrapper.isSelected ? "white" : App.Style.accent
                                            font.pixelSize: App.Spacing.overallText * 0.9
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        Text {
                                            text: modelData.port
                                            color: portChipWrapper.isSelected ? "white" : App.Style.primaryTextColor
                                            font.pixelSize: App.Spacing.overallText
                                            font.family: App.Style.fontFamily
                                            font.bold: modelData.isEsp32S3
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        id: portChipArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            if (settingsManager) {
                                                settingsManager.save_esp32_volume_port(modelData.port)
                                                if (!settingsManager.esp32VolumeEnabled) {
                                                    settingsManager.save_esp32_volume_enabled(true)
                                                }
                                                if (esp32VolumeManager) {
                                                    esp32VolumeManager.reset_reconnect_attempts()
                                                    esp32VolumeManager.connect_device()
                                                }
                                            }
                                        }
                                        onEntered: portChipRect.scale = 1.03
                                        onExited: portChipRect.scale = 1.0
                                    }

                                    layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && portChipWrapper.isSelected
                                    layer.effect: DropShadow {
                                        horizontalOffset: 0
                                        verticalOffset: 2
                                        radius: 4.0
                                        samples: 9
                                        color: Qt.rgba(0, 0, 0, 0.2)
                                    }

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: 100
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }
                            }
                        }

                        // "No ports" message chip
                        Rectangle {
                            visible: pageRoot.portsModel.length === 0
                            width: noPortsText.width + App.Spacing.overallSpacing * 3
                            height: App.Spacing.formElementHeight * 0.9
                            radius: App.EnvironmentTheme.active.chipRadius === -1
                                ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)
                            color: "transparent"
                            border.width: 1
                            border.color: App.EnvironmentTheme.active.chipAccentBorder
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                : Qt.rgba(App.Style.primaryTextColor.r,
                                          App.Style.primaryTextColor.g,
                                          App.Style.primaryTextColor.b, 0.2)

                            Text {
                                id: noPortsText
                                text: "No ports found - click Refresh"
                                anchors.centerIn: parent
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText
                                font.family: App.Style.fontFamily
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
                title: "Configuration"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Volume Step Size"
                            Layout.fillWidth: true
                        }

                        ValueDisplay {
                            text: settingsManager ? settingsManager.esp32VolumeStepSize.toFixed(2) + "%" : "1.00%"
                        }
                    }

                    SettingDescription {
                        text: "Volume change per encoder tick"
                    }

                    SettingsSlider {
                        id: vkVolumeStepSlider
                        from: 0.25
                        to: 10.0
                        stepSize: 0.25
                        value: settingsManager ? settingsManager.esp32VolumeStepSize : 1.0

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_esp32_volume_step_size(value)
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onEsp32VolumeStepSizeChanged(size) {
                                vkVolumeStepSlider.value = size
                            }
                        }
                    }

                    // Quick preset chips
                    Flow {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing * 0.8

                        Repeater {
                            model: [0.25, 0.5, 1.0, 2.0, 5.0]

                            Item {
                                id: presetChipWrapper
                                width: presetChipRect.width
                                height: presetChipRect.height

                                property bool isSelected: settingsManager && Math.abs(settingsManager.esp32VolumeStepSize - modelData) < 0.01

                                Rectangle {
                                    anchors.centerIn: presetChipRect
                                    width: presetChipRect.width + 4
                                    height: presetChipRect.height + 4
                                    radius: presetChipRect.radius + 2
                                    color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                    visible: App.EnvironmentTheme.active.chipAccentBorder && presetChipWrapper.isSelected
                                }

                                Rectangle {
                                    id: presetChipRect
                                    width: presetText.width + App.Spacing.overallSpacing * 2
                                    height: App.Spacing.formElementHeight * 0.7
                                    radius: App.EnvironmentTheme.active.chipRadius === -1
                                        ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)

                                    color: presetChipWrapper.isSelected ? App.Style.accent : App.Style.hoverColor

                                    border.width: App.EnvironmentTheme.active.chipAccentBorder
                                        ? 1
                                        : (presetChipWrapper.isSelected ? 0 : 1)
                                    border.color: App.EnvironmentTheme.active.chipAccentBorder
                                        ? (presetChipWrapper.isSelected
                                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                                        : Qt.rgba(App.Style.primaryTextColor.r,
                                                  App.Style.primaryTextColor.g,
                                                  App.Style.primaryTextColor.b, 0.1)

                                    Text {
                                        id: presetText
                                        anchors.centerIn: parent
                                        text: modelData + "%"
                                        color: presetChipWrapper.isSelected ? "white" : App.Style.primaryTextColor
                                        font.pixelSize: App.Spacing.overallText * 0.9
                                        font.family: App.Style.fontFamily
                                    }

                                    MouseArea {
                                        id: presetArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            if (settingsManager) {
                                                settingsManager.save_esp32_volume_step_size(modelData)
                                            }
                                        }
                                        onEntered: presetChipRect.scale = 1.05
                                        onExited: presetChipRect.scale = 1.0
                                    }

                                    layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && presetChipWrapper.isSelected
                                    layer.effect: DropShadow {
                                        horizontalOffset: 0
                                        verticalOffset: 2
                                        radius: 4.0
                                        samples: 9
                                        color: Qt.rgba(0, 0, 0, 0.2)
                                    }

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: 100
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── LED Indicator ──
            SettingCategory {
                title: "LED Indicator"
                description: "RGB LED indicator on the ESP32-S3 receiver"

                SettingsToggle {
                    id: ledSleepToggle
                    Layout.fillWidth: true
                    text: "Keep LEDs on"
                    checked: settingsManager ? !settingsManager.esp32LedSleepEnabled : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_esp32_led_sleep_enabled(!checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onEsp32LedSleepEnabledChanged(enabled) {
                            ledSleepToggle.checked = !enabled
                        }
                    }
                }

                SettingDescription {
                    text: "Stay on while OCTAVE runs (off = sleep after 5s idle)"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingLabel {
                        text: "Color Mode"
                    }

                    SettingDescription {
                        text: "Album art accent colors or a fixed color"
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
                description: "BerryIMU v3 accelerometer, gyroscope, magnetometer, and barometer on I2C bus 2"

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
                    Layout.fillWidth: true
                    text: "IMU Sensor"
                    checked: settingsManager ? settingsManager.imuEnabled : true
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_imu_enabled(checked)
                        }
                        if (typeof berryIMU !== "undefined" && berryIMU) {
                            berryIMU.setEnabled(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onImuEnabledChanged(enabled) {
                            imuEnabledToggle.checked = enabled
                        }
                    }
                }
            }

            // ── Display ──
            SettingCategory {
                title: "Display"
                description: "How sensor values are formatted and which cards appear in the sensor view"

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
                            text: settingsManager ? settingsManager.get_setting_with_default("sensorDecimalPlaces", 1) : "1"
                        }
                    }

                    SettingsSlider {
                        id: imuDecimalsSlider
                        from: 0; to: 4; stepSize: 1
                        value: settingsManager ? settingsManager.get_setting_with_default("sensorDecimalPlaces", 1) : 1
                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_setting("sensorDecimalPlaces", value)
                                imuDecimalsValue.text = value
                            }
                        }
                    }

                    SettingsToggle {
                        Layout.fillWidth: true
                        text: "Temperature in °F"
                        checked: settingsManager ? settingsManager.get_setting_with_default("sensorTempUnit", "F") === "F" : true
                        activeColor: App.Style.accent
                        inactiveColor: App.Style.hoverColor
                        onToggled: function(checked) {
                            if (settingsManager) {
                                settingsManager.save_setting("sensorTempUnit", checked ? "F" : "C")
                            }
                        }
                    }

                    SettingsToggle {
                        Layout.fillWidth: true
                        text: "Altitude in Feet"
                        checked: settingsManager ? settingsManager.get_setting_with_default("sensorAltitudeUnit", "m") === "ft" : false
                        activeColor: App.Style.accent
                        inactiveColor: App.Style.hoverColor
                        onToggled: function(checked) {
                            if (settingsManager) {
                                settingsManager.save_setting("sensorAltitudeUnit", checked ? "ft" : "m")
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
                            text: "Update Rate"
                            Layout.fillWidth: true
                        }

                        ValueDisplay {
                            id: imuEmitRateValue
                            text: (settingsManager ? settingsManager.get_setting_with_default("sensorEmitRate", 60) : 60) + " Hz"
                        }
                    }

                    SettingDescription {
                        text: "How often the IMU emits new readings (higher = smoother, more CPU)"
                    }

                    SettingsSlider {
                        id: imuEmitRateSlider
                        from: 10; to: 120; stepSize: 5
                        value: settingsManager ? settingsManager.get_setting_with_default("sensorEmitRate", 60) : 60
                        onMoved: {
                            imuEmitRateValue.text = value + " Hz"
                            imuEmitRateApply.restart()
                        }
                    }

                    Timer {
                        id: imuEmitRateApply
                        interval: 200
                        onTriggered: {
                            if (settingsManager) {
                                settingsManager.save_setting("sensorEmitRate", imuEmitRateSlider.value)
                            }
                            if (typeof berryIMU !== "undefined" && berryIMU) {
                                berryIMU.setEmitRate(imuEmitRateSlider.value)
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

                    SettingDescription {
                        text: "Show or hide individual readouts on the Sensors page"
                    }

                    Repeater {
                        model: [
                            { key: "sensorShowPitch",       label: "Pitch",       defaultOn: true },
                            { key: "sensorShowRoll",        label: "Roll",        defaultOn: true },
                            { key: "sensorShowHeading",     label: "Heading",     defaultOn: true },
                            { key: "sensorShowGForce",      label: "G-Force",     defaultOn: true },
                            { key: "sensorShowAltitude",    label: "Altitude",    defaultOn: true },
                            { key: "sensorShowTemperature", label: "Temperature", defaultOn: true }
                        ]
                        delegate: SettingsToggle {
                            Layout.fillWidth: true
                            text: modelData.label
                            checked: settingsManager
                                     ? settingsManager.get_setting_with_default(modelData.key, modelData.defaultOn)
                                     : modelData.defaultOn
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(checked) {
                                if (settingsManager) {
                                    settingsManager.save_setting(modelData.key, checked)
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
                description: "PAJ7620U2 infrared gesture recognition on I2C bus 2"

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
                    Layout.fillWidth: true
                    text: "Gesture Sensor"
                    checked: settingsManager ? settingsManager.gestureSensorEnabled : true
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_gesture_sensor_enabled(checked)
                        }
                        if (typeof gestureSensor !== "undefined" && gestureSensor) {
                            gestureSensor.setEnabled(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onGestureSensorEnabledChanged(enabled) {
                            gestureEnabledToggle.checked = enabled
                        }
                    }
                }
            }

            // ── Settings ──
            SettingCategory {
                title: "Settings"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        SettingLabel {
                            text: "Volume Step"
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

                    SettingDescription {
                        text: "Volume change per gesture (up/down)"
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
                            color: App.Style.hoverColor

                            Rectangle {
                                width: gsVolumeStepSlider.visualPosition * parent.width
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
                            text: "Gesture Cooldown"
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

                    SettingDescription {
                        text: "Minimum time between consecutive gestures"
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
                                settingsManager.save_gesture_cooldown(value)
                            }
                            if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                gestureSensor.setCooldown(value)
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
                            color: App.Style.hoverColor

                            Rectangle {
                                width: cooldownSlider.visualPosition * parent.width
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
                                settingsManager.save_gesture_cooldown(ms)
                            }
                            if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                gestureSensor.setCooldown(ms)
                            }
                        }
                    }
                }
            }

            // ── Mapping ──
            SettingCategory {
                title: "Mapping"
                description: "Choose which action each gesture triggers"

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

                        Flow {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing

                            Repeater {
                                model: pageRoot.actionOptions

                                Item {
                                    id: actionChipWrapper
                                    width: actionChipRect.width
                                    height: actionChipRect.height

                                    property string actionKey: modelData
                                    property bool isSelected: gestureRow.currentAction === actionKey

                                    Rectangle {
                                        anchors.centerIn: actionChipRect
                                        width: actionChipRect.width + pageRoot.dp(4)
                                        height: actionChipRect.height + pageRoot.dp(4)
                                        radius: actionChipRect.radius + 2
                                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                        visible: App.EnvironmentTheme.active.chipAccentBorder && actionChipWrapper.isSelected
                                    }

                                    Rectangle {
                                        id: actionChipRect
                                        width: actionChipText.width + App.Spacing.overallSpacing * 3
                                        height: App.Spacing.formElementHeight * 0.8
                                        radius: App.EnvironmentTheme.active.chipRadius === -1
                                            ? height / 2 : pageRoot.dpMin(App.EnvironmentTheme.active.chipRadius, 2)

                                        color: actionChipWrapper.isSelected ? App.Style.accent : App.Style.hoverColor

                                        border.width: App.EnvironmentTheme.active.chipAccentBorder
                                            ? 1
                                            : (actionChipWrapper.isSelected ? 0 : 1)
                                        border.color: App.EnvironmentTheme.active.chipAccentBorder
                                            ? (actionChipWrapper.isSelected
                                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                                                : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                                            : Qt.rgba(App.Style.primaryTextColor.r,
                                                      App.Style.primaryTextColor.g,
                                                      App.Style.primaryTextColor.b, 0.1)

                                        Text {
                                            id: actionChipText
                                            anchors.centerIn: parent
                                            text: pageRoot.actionLabels[actionChipWrapper.actionKey] || actionChipWrapper.actionKey
                                            color: actionChipWrapper.isSelected ? "white" : App.Style.primaryTextColor
                                            font.pixelSize: App.Spacing.overallText
                                            font.family: App.Style.fontFamily
                                        }

                                        MouseArea {
                                            id: actionChipArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                if (typeof gestureSensor !== "undefined" && gestureSensor) {
                                                    gestureSensor.setGestureAction(gestureRow.gestureName, actionChipWrapper.actionKey)
                                                }
                                            }
                                            onEntered: actionChipRect.scale = 1.05
                                            onExited: actionChipRect.scale = 1.0
                                        }

                                        layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && actionChipWrapper.isSelected
                                        layer.effect: DropShadow {
                                            horizontalOffset: 0
                                            verticalOffset: 2
                                            radius: 4.0
                                            samples: 9
                                            color: Qt.rgba(0, 0, 0, 0.2)
                                        }

                                        Behavior on scale {
                                            NumberAnimation {
                                                duration: 100
                                                easing.type: Easing.OutCubic
                                            }
                                        }
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
                            text: "Reset to Defaults"
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: resetArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
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

    // ─────────────────────────────────────────── Phone Dock
    Component {
        id: phoneDockContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            // ── Android Auto ──
            SettingCategory {
                title: "Android Auto"
                description: "Requires Android Auto app and Developer Mode on your phone."

                SettingsToggle {
                    id: androidAutoEnabledToggle
                    Layout.fillWidth: true
                    text: "Show in nav bar"
                    checked: settingsManager ? settingsManager.androidAutoEnabled : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_android_auto_enabled(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onAndroidAutoEnabledChanged() {
                            androidAutoEnabledToggle.checked = settingsManager.androidAutoEnabled
                        }
                    }
                }
            }

            // ── Phone Mirror ──
            SettingCategory {
                title: "Phone Mirror"
                description: "Mirror your phone over USB. Nothing to install: enable USB debugging on the phone and plug it in."

                SettingsToggle {
                    id: phoneMirrorEnabledToggle
                    Layout.fillWidth: true
                    text: "Show in nav bar"
                    checked: settingsManager ? settingsManager.phoneMirrorEnabled : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_phone_mirror_enabled(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onPhoneMirrorEnabledChanged() {
                            phoneMirrorEnabledToggle.checked = settingsManager.phoneMirrorEnabled
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing


                    SettingLabel {
                        text: "Virtual Display Size"
                    }

                    SettingDescription {
                        text: "scrcpy creates a separate landscape display on the phone (Android 11+). "
                              + "Leave empty to mirror the phone's own screen."
                    }

                    SettingsTextField {
                        id: scrcpyDisplaySizeField
                        Layout.fillWidth: true
                        text: settingsManager ? settingsManager.scrcpyDisplaySize : "1280x800"
                        placeholderText: "1280x800"

                        onEditingFinished: {
                            if (settingsManager) {
                                settingsManager.save_scrcpy_display_size(text)
                                if (phoneMirrorManager) {
                                    phoneMirrorManager.setDisplaySize(text)
                                    // Manager may have snapped the dimensions to multiples of 8
                                    if (phoneMirrorManager.displaySize !== text) {
                                        settingsManager.save_scrcpy_display_size(phoneMirrorManager.displaySize)
                                    }
                                }
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onScrcpyDisplaySizeChanged() {
                                scrcpyDisplaySizeField.text = settingsManager.scrcpyDisplaySize
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingLabel {
                        text: "Audio Forwarding"
                    }

                    SettingDescription {
                        text: "Not supported by the built-in mirror client yet; the setting is kept for a future release."
                    }

                    SettingsToggle {
                        id: scrcpyAudioEnabledToggle
                        Layout.fillWidth: true
                        text: "Forward phone audio"
                        checked: settingsManager ? settingsManager.scrcpyAudioEnabled : false
                        activeColor: App.Style.accent
                        inactiveColor: App.Style.hoverColor

                        onToggled: function(checked) {
                            if (settingsManager) {
                                settingsManager.save_scrcpy_audio_enabled(checked)
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onScrcpyAudioEnabledChanged() {
                                scrcpyAudioEnabledToggle.checked = settingsManager.scrcpyAudioEnabled
                            }
                        }
                    }

                    SettingDescription {
                        text: "Will apply once audio forwarding is implemented (Android 11+)"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingLabel {
                        text: "Status"
                    }

                    SettingDescription {
                        text: phoneMirrorManager && phoneMirrorManager.nativeAvailable
                            ? "Built-in mirror client ready (scrcpy server " + phoneMirrorManager.serverVersion + ", adb: " + phoneMirrorManager.adbPath + ")"
                            : (phoneMirrorManager ? phoneMirrorManager.getInstallInstructions() : "Phone mirror manager not available")
                        color: phoneMirrorManager && phoneMirrorManager.nativeAvailable
                            ? App.Style.statusConnected
                            : App.Style.statusError
                    }
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

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
