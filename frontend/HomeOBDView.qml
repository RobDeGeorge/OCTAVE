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

    // Parameter info from centralized singleton (covers all 92 params: 84 OBD + 8 IMU)
    property var parameterInfo: App.OBDParameterModel.parameterInfo
    
    // Smart grid layout - 1 column for up to 5 cards, 2 columns for 6-8 cards.
    // Portrait gives this view the full width, so 2 columns from 2 cards up.
    property int cardCount: obdRepeater.count
    property int columnCount: cardCount <= (App.Spacing.isPortrait ? 1 : 5) ? 1 : 2

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
                property real value: App.OBDParameterModel.liveValue(param) || 0
                // False until a reading arrives, so the card shows "--"
                // rather than a misleading 0.0 while OBD is disconnected.
                readonly property bool hasValue: App.OBDParameterModel.liveValue(param) !== undefined

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
                
                // Shift light - right middle of the RPM card. ShiftLightManager
                // picks the flag and runs the flash; this only draws it. (Null checks:
                // at shutdown the managers go before the engine.)
                Rectangle {
                    id: shiftLight
                    visible: param === "RPM" && !!shiftLightManager && shiftLightManager.enabled && shiftLightManager.showOnHomeCard
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: dp(15)
                    width: parent.height * (shiftLightManager ? shiftLightManager.lampSize : 0.5)
                    height: width
                    radius: width / 2

                    readonly property bool lit: !!shiftLightManager && shiftLightManager.lit

                    // Pulse animation timer
                    Timer {
                        id: pulseTimer
                        interval: 800
                        running: shiftLight.visible && shiftLight.lit && shiftLightManager.pulseEnabled
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

                    // Dark silhouette when off or in a flash's off phase
                    color: lit ? shiftLightManager.activeFlag.color : "#1a1a1a"
                    border.color: shiftLightManager && shiftLightManager.active ? Qt.darker(color, 1.3) : "#333333"
                    border.width: 2

                    // Glow effect for shift light (only when lit)
                    Rectangle {
                        id: shiftLightGlow
                        anchors.centerIn: parent
                        width: parent.width * (shiftLightManager ? shiftLightManager.glowSize : 0)
                        height: width
                        radius: width / 2
                        color: Qt.lighter(parent.color, 1.5)
                        opacity: shiftLightManager ? shiftLightManager.glowIntensity : 0
                        visible: shiftLight.lit && shiftLightManager.glowSize > 0
                    }

                    Behavior on color { ColorAnimation { duration: shiftLightManager ? shiftLightManager.colorTransitionSpeed : 0 } }
                }

                // Progress bar strip at bottom of card. Inset by the border
                // width: full-bleed, its background-coloured track painted over
                // the card's bottom border and rounded corners.
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: parent.border.width
                    height: dp(4)
                    color: App.Style.backgroundColor
                    radius: parent.radius

                    Rectangle {
                        visible: display.hasValue
                        width: Math.max(dp(4), parent.width * Math.max(0, Math.min(1, (value - info.minValue) / (info.maxValue - info.minValue))))
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
                        text: display.hasValue
                              ? displayValue.toFixed(App.OBDParameterModel.decimalsFor(param)) + " " + info.unit
                              : "-- " + info.unit
                        font.pixelSize: App.Spacing.mainMenuOBDDataSize
                        font.bold: true
                        font.family: homeOBDView.globalFont
                        color: display.hasValue ? App.Style.primaryTextColor : App.Style.secondaryTextColor
                        Layout.alignment: Qt.AlignLeft
                    }
                }
            }
        }
    }
    
    // Polling is demand-driven (obdManager.setParameterDemand): ask for the
    // Home tiles' PIDs while this view is actually on screen.
    readonly property var _demandIds: visible ? obdRepeater.model : []
    function _pushDemand(ids) {
        if (typeof obdManager !== "undefined" && obdManager && obdManager.setParameterDemand)
            obdManager.setParameterDemand("home", ids)
    }
    on_DemandIdsChanged: _pushDemand(_demandIds)
    Component.onCompleted: _pushDemand(_demandIds)
    Component.onDestruction: _pushDemand([])

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