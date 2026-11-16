import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import ".." as App

// The RPM shift light editor: on / off, the home card indicator, the redline
// and the list of flags (RPM bands with a colour and an effect). OBDMenu
// opens it from the RPM card. It reads its state from settingsManager and
// shiftLightManager when it opens and writes every change at once; the
// overlays and the home card lamp follow shiftLightManager, not this popup.
Popup {
    id: editor

    // The font of the page that opens the editor
    property string fontFamily: App.Style.fontFamily

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: parent.width * 0.92
    height: parent.height * 0.95
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Editor state, loaded in onOpened
    property bool shiftLightEnabled: true
    property bool showOnHomeCard: true       // Show indicator on home OBD card
    property int maxRpm: 8000
    property int selectedFlagIndex: -1       // Which flag is selected for editing (-1 = none)

    // Flags list - each flag has: rpmLow, rpmHigh, color, flash, flashSpeed, fullScreenFlash, fullScreenFlashOpacity, auraFlash, auraFlashOpacity, auraSpread
    property var flags: []

    // Default colors for new flags
    property var defaultColors: ["#00FF00", "#FFFF00", "#FF8800", "#FF0000", "#FF00FF", "#00FFFF"]

    readonly property bool hasSelection: selectedFlagIndex >= 0 && selectedFlagIndex < flags.length

    // Load settings when popup opens
    onOpened: {
        if (settingsManager) {
            shiftLightEnabled = settingsManager.get_setting_with_default("rpm_shift_light_enabled", true)
            showOnHomeCard = settingsManager.get_setting_with_default("rpm_show_on_home_card", true)
            maxRpm = settingsManager.get_setting_with_default("rpm_max_rpm", 8000)
            // Already migrated to rpmLow/rpmHigh bands by ShiftLightManager
            flags = shiftLightManager.flags
            selectedFlagIndex = -1
        }
    }

    function setShiftLightEnabled(on) {
        shiftLightEnabled = on
        if (settingsManager)
            settingsManager.save_setting("rpm_shift_light_enabled", shiftLightEnabled)
    }

    function setShowOnHomeCard(on) {
        showOnHomeCard = on
        if (settingsManager)
            settingsManager.save_setting("rpm_show_on_home_card", showOnHomeCard)
    }

    // A new redline; flags that reach past it are pulled back to it
    function setMaxRpm(value) {
        maxRpm = value
        if (settingsManager) {
            settingsManager.save_setting("rpm_max_rpm", value)
        }
        // Clamp flag rpmHigh values that exceed new max
        var needsSave = false
        var newFlags = flags.slice()
        for (var i = 0; i < newFlags.length; i++) {
            if (newFlags[i].rpmHigh > value) {
                newFlags[i] = Object.assign({}, newFlags[i])
                newFlags[i].rpmHigh = value
                // Also clamp rpmLow if it exceeds new rpmHigh
                if (newFlags[i].rpmLow > value) {
                    newFlags[i].rpmLow = value
                }
                needsSave = true
            }
        }
        if (needsSave) {
            flags = newFlags
            saveFlags()
        }
    }

    // Save flags to settings
    function saveFlags() {
        shiftLightManager.setFlags(flags)
    }

    // Add a new flag at a default position
    function addFlag() {
        var newRpmLow = Math.round(maxRpm * 0.6 / 100) * 100  // Default to 60% of max
        var newRpmHigh = Math.round(maxRpm * 0.85 / 100) * 100  // Default to 85% of max
        var colorIndex = flags.length % defaultColors.length
        var newFlag = {
            rpmLow: newRpmLow,
            rpmHigh: newRpmHigh,
            color: defaultColors[colorIndex],
            flash: false,
            flashSpeed: 100,
            fullScreenFlash: false,
            fullScreenFlashOpacity: 0.5,
            auraFlash: false,
            auraFlashOpacity: 0.6,
            auraSpread: 0.18
        }
        var newFlags = flags.slice()  // Create a copy
        newFlags.push(newFlag)
        // Sort by low RPM
        newFlags.sort(function(a, b) { return a.rpmLow - b.rpmLow })
        flags = newFlags
        selectedFlagIndex = flags.indexOf(newFlag)
        saveFlags()
    }

    // Remove a flag
    function removeFlag(index) {
        if (index >= 0 && index < flags.length) {
            var newFlags = flags.slice()
            newFlags.splice(index, 1)
            flags = newFlags
            if (selectedFlagIndex >= flags.length) {
                selectedFlagIndex = flags.length - 1
            }
            if (selectedFlagIndex < 0 && flags.length > 0) {
                selectedFlagIndex = 0
            }
            saveFlags()
        }
    }

    // Update a flag property
    function updateFlag(index, property, value) {
        if (index >= 0 && index < flags.length) {
            var newFlags = flags.slice()
            newFlags[index] = Object.assign({}, newFlags[index])
            newFlags[index][property] = value
            // Ensure rpmLow <= rpmHigh
            if (property === "rpmLow" && newFlags[index].rpmLow > newFlags[index].rpmHigh) {
                newFlags[index].rpmHigh = newFlags[index].rpmLow
            }
            if (property === "rpmHigh" && newFlags[index].rpmHigh < newFlags[index].rpmLow) {
                newFlags[index].rpmLow = newFlags[index].rpmHigh
            }
            // Re-sort if low RPM changed
            if (property === "rpmLow") {
                var updatedFlag = newFlags[index]
                newFlags.sort(function(a, b) { return a.rpmLow - b.rpmLow })
                selectedFlagIndex = newFlags.indexOf(updatedFlag)
            }
            flags = newFlags
            saveFlags()
        }
    }

    background: Rectangle {
        color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.95)
        radius: App.Spacing.overallMargin
        border.color: App.Style.accent
        border.width: 2

        layer.enabled: true
        layer.effect: DropShadow {
            horizontalOffset: 0
            verticalOffset: 6
            radius: 30
            samples: 31
            color: "#90000000"
        }
    }

    contentItem: Item {
        // Header at top
        Text {
            id: header
            text: "RPM Shift Light Settings"
            font.pixelSize: App.Spacing.overallText * 1.6
            font.bold: true
            font.family: editor.fontFamily
            color: App.Style.primaryTextColor
            anchors.top: parent.top
            anchors.topMargin: App.Spacing.overallMargin
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Close button at bottom
        Rectangle {
            id: closeButton
            width: App.Spacing.overallText * 10
            height: App.Spacing.overallText * 3
            radius: App.Spacing.overallMargin * 0.5
            color: closeMouse.pressed ? Qt.darker(App.Style.accent, 1.2) :
                   closeMouse.containsMouse ? Qt.lighter(App.Style.accent, 1.1) : App.Style.accent
            anchors.bottom: parent.bottom
            anchors.bottomMargin: App.Spacing.overallMargin
            anchors.horizontalCenter: parent.horizontalCenter

            Behavior on color { ColorAnimation { duration: 150 } }

            Text {
                anchors.centerIn: parent
                text: "Close"
                font.pixelSize: App.Spacing.overallText * 1.4
                font.bold: true
                font.family: editor.fontFamily
                color: "white"
            }

            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: editor.close()
            }
        }

        // Settings content in scrollable area
        ScrollView {
            id: scrollView
            anchors.top: header.bottom
            anchors.topMargin: App.Spacing.overallSpacing
            anchors.bottom: closeButton.top
            anchors.bottomMargin: App.Spacing.overallSpacing
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: App.Spacing.overallMargin
            anchors.rightMargin: App.Spacing.overallMargin
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            Column {
                width: scrollView.width
                spacing: App.Spacing.overallSpacing

                ShiftToggleRow {
                    objectName: "shiftLightEnabledRow"
                    width: parent.width
                    fontFamily: editor.fontFamily
                    text: "Enable Shift Light"
                    checked: editor.shiftLightEnabled
                    onToggleRequested: editor.setShiftLightEnabled(!editor.shiftLightEnabled)
                }

                ShiftToggleRow {
                    width: parent.width
                    fontFamily: editor.fontFamily
                    text: "Show Indicator on Home"
                    checked: editor.showOnHomeCard
                    onToggleRequested: editor.setShowOnHomeCard(!editor.showOnHomeCard)
                }

                // === MAX RPM SETTING ===
                RowLayout {
                    width: parent.width

                    Text {
                        text: "Redline"
                        font.pixelSize: App.Spacing.overallText * 1.1
                        font.family: editor.fontFamily
                        color: App.Style.primaryTextColor
                    }

                    ShiftSlider {
                        Layout.fillWidth: true
                        from: 4000
                        to: 12000
                        stepSize: 500
                        value: editor.maxRpm
                        fillColor: "#FF0000"
                        onMoved: editor.setMaxRpm(value)
                    }

                    Text {
                        text: editor.maxRpm
                        font.pixelSize: App.Spacing.overallText * 1.1
                        font.bold: true
                        font.family: editor.fontFamily
                        color: "#FF0000"
                        Layout.preferredWidth: App.Spacing.overallText * 4
                        horizontalAlignment: Text.AlignRight
                    }
                }

                // RPM Bar visualization with flags
                Item {
                    width: parent.width
                    height: App.Spacing.overallText * 8

                    // The RPM bar background
                    Rectangle {
                        id: rpmBar
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: App.Spacing.overallMargin
                        anchors.rightMargin: App.Spacing.overallMargin
                        anchors.verticalCenter: parent.verticalCenter
                        height: App.Spacing.overallSliderHeight * 1.2
                        radius: height / 2
                        color: Qt.darker(App.Style.contentColor, 1.2)

                        // Gradient fill showing base RPM range
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Qt.rgba(0.2, 0.2, 0.2, 1) }
                                GradientStop { position: 1.0; color: Qt.rgba(0.5, 0.1, 0.1, 1) }
                            }
                        }

                        // Range segments for each flag
                        Repeater {
                            model: editor.flags

                            Rectangle {
                                id: segment
                                required property var modelData
                                required property int index
                                readonly property bool selected: index === editor.selectedFlagIndex
                                property real lowPos: modelData.rpmLow / editor.maxRpm
                                property real highPos: modelData.rpmHigh / editor.maxRpm
                                x: lowPos * rpmBar.width
                                width: (highPos - lowPos) * rpmBar.width
                                height: rpmBar.height
                                anchors.verticalCenter: parent.verticalCenter
                                color: Qt.rgba(Qt.lighter(modelData.color, 1.2).r, Qt.lighter(modelData.color, 1.2).g, Qt.lighter(modelData.color, 1.2).b, 0.5)
                                border.color: selected ? "white" : modelData.color
                                border.width: selected ? 3 : 2
                                radius: rpmBar.radius

                                // Flag number label on segment
                                Text {
                                    anchors.centerIn: parent
                                    text: (segment.index + 1).toString()
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: editor.fontFamily
                                    color: "white"
                                    visible: parent.width > App.Spacing.overallText * 2
                                }

                                // Click to select flag
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: editor.selectedFlagIndex = segment.index
                                }
                            }
                        }

                        // RPM scale labels
                        Text {
                            anchors.left: parent.left
                            anchors.top: parent.bottom
                            anchors.topMargin: App.Spacing.overallSpacing * 0.5
                            text: "0"
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.family: editor.fontFamily
                            color: Qt.darker(App.Style.primaryTextColor, 1.3)
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: App.Spacing.overallSpacing * 0.5
                            text: Math.round(editor.maxRpm / 2).toString()
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.family: editor.fontFamily
                            color: Qt.darker(App.Style.primaryTextColor, 1.3)
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.top: parent.bottom
                            anchors.topMargin: App.Spacing.overallSpacing * 0.5
                            text: editor.maxRpm.toString()
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.family: editor.fontFamily
                            color: Qt.darker(App.Style.primaryTextColor, 1.3)
                        }
                    }
                }

                // Add Flag Button
                Rectangle {
                    objectName: "shiftLightAddFlag"
                    width: App.Spacing.overallText * 12
                    height: App.Spacing.overallText * 3
                    radius: App.Spacing.overallMargin * 0.5
                    color: addFlagMouse.pressed ? Qt.darker(App.Style.accent, 1.2) :
                           addFlagMouse.containsMouse ? Qt.lighter(App.Style.accent, 1.1) : App.Style.accent
                    anchors.horizontalCenter: parent.horizontalCenter

                    Behavior on color { ColorAnimation { duration: 150 } }

                    Row {
                        anchors.centerIn: parent
                        spacing: App.Spacing.overallSpacing * 0.5

                        Text {
                            text: "+"
                            font.pixelSize: App.Spacing.overallText * 1.6
                            font.bold: true
                            font.family: editor.fontFamily
                            color: "white"
                        }

                        Text {
                            text: "Add Flag"
                            font.pixelSize: App.Spacing.overallText * 1.4
                            font.bold: true
                            font.family: editor.fontFamily
                            color: "white"
                        }
                    }

                    MouseArea {
                        id: addFlagMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: editor.addFlag()
                    }
                }

                // === SECTION DIVIDER ===
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.2)
                    visible: editor.flags.length > 0
                }

                // === SELECTED FLAG SETTINGS ===
                ShiftFlagSettings {
                    objectName: "shiftLightFlagSettings"
                    width: parent.width
                    visible: editor.hasSelection
                    fontFamily: editor.fontFamily
                    flag: editor.hasSelection ? editor.flags[editor.selectedFlagIndex] : null
                    flagNumber: editor.selectedFlagIndex + 1
                    maxRpm: editor.maxRpm
                    onEdited: function(name, value) { editor.updateFlag(editor.selectedFlagIndex, name, value) }
                    onDeleteRequested: editor.removeFlag(editor.selectedFlagIndex)
                }

                // Empty state message
                Text {
                    width: parent.width
                    text: "No flags added yet. Click \"Add Flag\" to create your first shift light trigger point."
                    font.pixelSize: App.Spacing.overallText * 1.1
                    font.family: editor.fontFamily
                    color: Qt.darker(App.Style.primaryTextColor, 1.3)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: editor.flags.length === 0
                    topPadding: App.Spacing.overallSpacing
                    bottomPadding: App.Spacing.overallSpacing
                }

                // Bottom padding
                Item {
                    width: parent.width
                    height: App.Spacing.overallSpacing
                }
            }
        }
    }
}
