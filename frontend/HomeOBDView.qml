// HomeOBDView.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: homeOBDView

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Full screen flash settings (opacity is global, but enabled is per-flag)
    property real fullScreenFlashOpacity: settingsManager ? settingsManager.get_setting_with_default("rpm_fullscreen_flash_opacity", 0.5) : 0.5

    // Track if any shift light is active and flashing (will be set by the RPM card)
    property var activeShiftFlag: null
    property bool shiftLightFlashVisible: true

    // Parameter info from centralized singleton (covers all 92 params: 84 OBD + 8 IMU)
    property var parameterInfo: App.OBDParameterModel.parameterInfo
    
    // Smart grid layout - 1 column for up to 5 cards, 2 columns for 6-8 cards
    property int cardCount: obdRepeater.count
    property int columnCount: cardCount <= 5 ? 1 : 2

    GridLayout {
        id: gridLayout
        anchors.fill: parent
        anchors.margins: dp(10)
        columnSpacing: dp(10)
        rowSpacing: dp(10)
        columns: homeOBDView.columnCount

        // Dynamically create displays (max 8)
        Repeater {
            id: obdRepeater
            model: {
                // Use the settings if available, or default to 4 standard params
                let params;
                if (settingsManager && settingsManager.get_home_obd_parameters) {
                    params = settingsManager.get_home_obd_parameters()
                } else {
                    params = ["SPEED", "RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE"]
                }
                // Limit to maximum 8 cards
                return params.slice(0, 8)
            }
            
            delegate: Rectangle {
                id: display
                property string param: modelData
                property var info: homeOBDView.parameterInfo[param] ||
                                {title: param, unit: "", minValue: 0, maxValue: 100}
                // Live value from centralized singleton
                property real value: App.OBDParameterModel.paramValues[param] || 0

                // Animated display value - fast rolling effect
                property real displayValue: value
                Behavior on displayValue {
                    NumberAnimation {
                        duration: 50
                        easing.type: Easing.Linear
                    }
                }

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.columnSpan: (index === 0 && homeOBDView.columnCount === 2 && homeOBDView.cardCount % 2 !== 0) ? 2 : 1
                color: App.Style.backgroundColor
                border.color: App.Style.accent
                border.width: 1
                radius: 3
                
                // Shift light - positioned on right middle of RPM card
                // Uses flag-based settings from settingsManager
                Rectangle {
                    id: shiftLight
                    visible: param === "RPM" && shiftLightEnabled && showOnHomeCard
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: dp(15)
                    width: parent.height * shiftLightSize
                    height: width
                    radius: width / 2

                    // Load shift light settings
                    property bool shiftLightEnabled: settingsManager ? settingsManager.get_setting_with_default("rpm_shift_light_enabled", true) : true
                    property bool showOnHomeCard: settingsManager ? settingsManager.get_setting_with_default("rpm_show_on_home_card", true) : true
                    property var flags: {
                        if (settingsManager) {
                            try {
                                var savedFlags = settingsManager.get_setting_with_default("rpm_flags", "[]")
                                return JSON.parse(savedFlags)
                            } catch(e) {
                                return []
                            }
                        }
                        return []
                    }

                    // Appearance settings
                    property real shiftLightSize: settingsManager ? settingsManager.get_setting_with_default("rpm_shift_light_size", 0.5) : 0.5
                    property real glowSize: settingsManager ? settingsManager.get_setting_with_default("rpm_glow_size", 0.6) : 0.6
                    property real glowIntensity: settingsManager ? settingsManager.get_setting_with_default("rpm_glow_intensity", 0.6) : 0.6
                    property int colorTransitionSpeed: settingsManager ? settingsManager.get_setting_with_default("rpm_color_transition_speed", 100) : 100
                    property bool pulseEnabled: settingsManager ? settingsManager.get_setting_with_default("rpm_pulse_enabled", false) : false

                    // Find the active flag based on current RPM value (range-based)
                    property var activeFlag: {
                        if (!flags || flags.length === 0) return null
                        // Find the flag whose range contains the current RPM
                        // Check from highest priority (last) to lowest (first)
                        var active = null
                        for (var i = flags.length - 1; i >= 0; i--) {
                            var flag = flags[i]
                            // Support both old format (rpm) and new format (rpmLow/rpmHigh)
                            var low = flag.rpmLow !== undefined ? flag.rpmLow : flag.rpm
                            var high = flag.rpmHigh !== undefined ? flag.rpmHigh : 99999
                            if (value >= low && value <= high) {
                                active = flag
                                break
                            }
                        }
                        return active
                    }

                    property bool isActive: activeFlag !== null

                    // Flash timer for flashing flags
                    property bool flashVisible: true
                    Timer {
                        id: flashTimer
                        interval: shiftLight.activeFlag ? shiftLight.activeFlag.flashSpeed : 100
                        running: shiftLight.activeFlag && shiftLight.activeFlag.flash
                        repeat: true
                        onTriggered: {
                            shiftLight.flashVisible = !shiftLight.flashVisible
                            // Update parent for full screen flash
                            homeOBDView.shiftLightFlashVisible = shiftLight.flashVisible
                        }
                    }

                    // Pulse animation timer
                    Timer {
                        id: pulseTimer
                        interval: 800
                        running: shiftLight.isActive && shiftLight.pulseEnabled && shiftLight.flashVisible
                        repeat: true
                        onTriggered: {
                            pulseAnimation.start()
                        }
                    }

                    SequentialAnimation {
                        id: pulseAnimation
                        PropertyAnimation {
                            target: shiftLight
                            property: "scale"
                            to: 1.15
                            duration: 200
                            easing.type: Easing.OutQuad
                        }
                        PropertyAnimation {
                            target: shiftLight
                            property: "scale"
                            to: 1.0
                            duration: 400
                            easing.type: Easing.InOutQuad
                        }
                    }

                    // Reset flash visibility when flag changes or flash stops
                    onActiveFlagChanged: {
                        flashVisible = true
                        homeOBDView.shiftLightFlashVisible = true
                    }

                    // Update parent with active flag for full screen flash
                    onIsActiveChanged: {
                        if (isActive && shiftLightEnabled) {
                            homeOBDView.activeShiftFlag = activeFlag
                        } else {
                            homeOBDView.activeShiftFlag = null
                        }
                    }

                    // Clear active flag when shift light is disabled
                    onShiftLightEnabledChanged: {
                        if (!shiftLightEnabled) {
                            homeOBDView.activeShiftFlag = null
                        } else if (isActive) {
                            homeOBDView.activeShiftFlag = activeFlag
                        }
                    }

                    color: {
                        if (activeFlag) {
                            // If flashing and flash is off, show dark
                            if (activeFlag.flash && !flashVisible) {
                                return "#1a1a1a"
                            }
                            return activeFlag.color
                        }
                        return "#1a1a1a"  // Dark silhouette when off
                    }
                    border.color: isActive ? Qt.darker(color, 1.3) : "#333333"
                    border.width: 2

                    // Glow effect for shift light (only when active)
                    Rectangle {
                        id: shiftLightGlow
                        anchors.centerIn: parent
                        width: parent.width * shiftLight.glowSize
                        height: width
                        radius: width / 2
                        color: Qt.lighter(parent.color, 1.5)
                        opacity: shiftLight.glowIntensity
                        visible: shiftLight.isActive && shiftLight.flashVisible && shiftLight.glowSize > 0
                    }

                    Behavior on color { ColorAnimation { duration: shiftLight.colorTransitionSpeed } }

                    // Reload settings when they change
                    Connections {
                        target: settingsManager
                        function onGenericSettingChanged(key) {
                            // Only reload if RPM-related settings changed
                            if (key.startsWith("rpm_")) {
                                shiftLight.shiftLightEnabled = settingsManager.get_setting_with_default("rpm_shift_light_enabled", true)
                                shiftLight.showOnHomeCard = settingsManager.get_setting_with_default("rpm_show_on_home_card", true)
                                shiftLight.shiftLightSize = settingsManager.get_setting_with_default("rpm_shift_light_size", 0.5)
                                shiftLight.glowSize = settingsManager.get_setting_with_default("rpm_glow_size", 0.6)
                                shiftLight.glowIntensity = settingsManager.get_setting_with_default("rpm_glow_intensity", 0.6)
                                shiftLight.colorTransitionSpeed = settingsManager.get_setting_with_default("rpm_color_transition_speed", 100)
                                shiftLight.pulseEnabled = settingsManager.get_setting_with_default("rpm_pulse_enabled", false)
                                try {
                                    var savedFlags = settingsManager.get_setting_with_default("rpm_flags", "[]")
                                    shiftLight.flags = JSON.parse(savedFlags)
                                } catch(e) {
                                    shiftLight.flags = []
                                }
                                // Also reload full screen flash opacity at the view level
                                homeOBDView.fullScreenFlashOpacity = settingsManager.get_setting_with_default("rpm_fullscreen_flash_opacity", 0.5)
                            }
                        }
                    }
                }

                // Progress bar strip at bottom of card
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: dp(4)
                    color: App.Style.backgroundColor
                    radius: parent.radius

                    Rectangle {
                        width: Math.max(dp(4), parent.width * Math.min(1, (value - info.minValue) / (info.maxValue - info.minValue)))
                        height: parent.height
                        color: App.Style.accent
                        radius: parent.radius
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: dp(5)
                    anchors.rightMargin: dp(5)
                    anchors.topMargin: dp(5)
                    anchors.bottomMargin: dp(4) + dp(4)
                    spacing: dp(5)

                    Text {
                        text: info.title.toUpperCase()
                        font.pixelSize: App.Spacing.mainMenuOBDTextSize
                        font.family: homeOBDView.globalFont
                        color: App.Style.secondaryTextColor
                        Layout.alignment: Qt.AlignLeft
                    }

                    Text {
                        text: displayValue.toFixed(1) + " " + info.unit
                        font.pixelSize: App.Spacing.mainMenuOBDDataSize
                        font.bold: true
                        font.family: homeOBDView.globalFont
                        color: App.Style.primaryTextColor
                        Layout.alignment: Qt.AlignLeft
                    }
                }
            }
        }
    }
    
    // Single connection to settings changes
    Connections {
        target: settingsManager
        function onHomeOBDParametersChanged() {
            // Force refresh of the repeater
            let currentParams = settingsManager.get_home_obd_parameters();

            // First set model to empty array to force re-creation of all delegates
            obdRepeater.model = [];

            // Then delay setting the new model to ensure clean refresh
            Qt.callLater(function() {
                obdRepeater.model = currentParams;
            });
        }
    }
}