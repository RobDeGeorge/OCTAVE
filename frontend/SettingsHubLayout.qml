import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"
import "settings/ScrollMemory.js" as ScrollMemory

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: hubLayout

    // Required from orchestrator
    property var settingsMenu: null
    property var hubModel: []

    // State: "hub" = card grid landing, "detail" = full-width settings page
    property string viewState: ""

    // Decide initial viewState once settingsMenu is wired up by the orchestrator's onLoaded
    onSettingsMenuChanged: {
        if (!settingsMenu || viewState !== "") return

        // Mid-session layout switch: stay in detail if already viewing a section
        if (settingsMenu.currentSection && settingsMenu.currentSection !== "") {
            var page = settingsMenu.pageForSection(settingsMenu.currentSection)
            if (page && settingsMenu.isVisible(settingsMenu.currentSection)) {
                viewState = "detail"
                return
            }
        }
        // Initial app launch: resume last category if valid
        if (settingsMenu.isInitialLoad
                && settingsMenu.initialSection && settingsMenu.initialSection !== "") {
            var page2 = settingsMenu.pageForSection(settingsMenu.initialSection)
            if (page2 && settingsMenu.isVisible(settingsMenu.initialSection)) {
                settingsMenu.currentSection = settingsMenu.initialSection
                viewState = "detail"
                return
            }
        }
        viewState = "hub"
    }

    function sourceForSection(section) {
        if (!settingsMenu) return ""
        for (var i = 0; i < settingsMenu.pageModel.length; i++) {
            if (settingsMenu.pageModel[i].section === section)
                return settingsMenu.pageModel[i].source
        }
        return settingsMenu.pageModel[0].source
    }

    // Get widget info lines for a hub card
    function getWidgetForSection(section) {
        if (!settingsManager) return []
        switch (section) {
            case "displaySettings":
                return [
                    { label: "Theme", value: settingsManager.themeSetting },
                    { label: "Scale", value: Math.round(settingsManager.uiScale * 100) + "%" }
                ]
            case "mediaSettings":
                return [
                    { label: "Source", value: settingsManager.mediaSource === "spotify" ? "Spotify" : "Local" },
                    { label: "Autoplay", value: settingsManager.autoPlayOnStartup ? "On" : "Off" },
                    { label: "Visualizer", value: settingsManager.showWaveformVisualizer ? "On" : "Off" }
                ]
            case "obdSettings":
                return [
                    { label: "Status", value: obdManager ? obdManager.get_connection_status() : "N/A" },
                    { label: "Port", value: settingsManager.obdBluetoothPort || "Not set" },
                    { label: "Fast", value: settingsManager.obdFastMode ? "On" : "Off" }
                ]
            case "accessoriesSettings":
                var espConnected = (typeof esp32VolumeManager !== "undefined" && esp32VolumeManager && esp32VolumeManager.is_connected())
                return [
                    { label: "Knob", value: espConnected ? "Connected" : "Disconnected" },
                    { label: "Gestures", value: settingsManager.gestureSensorEnabled ? "Enabled" : "Disabled" },
                    { label: "Phone Dock", value: (settingsManager.androidAutoEnabled || settingsManager.phoneMirrorEnabled) ? "On" : "Off" }
                ]
            case "deviceSettings":
                return [
                    { label: "Name", value: settingsManager.deviceName || "OCTAVE" }
                ]
            default:
                return []
        }
    }

    function buildHubCards() {
        var model = []
        for (var i = 0; i < hubModel.length; i++) {
            model.push({
                name: hubModel[i].name,
                section: hubModel[i].section,
                icon: hubModel[i].icon,
                widgetItems: getWidgetForSection(hubModel[i].section)
            })
        }
        return model
    }

    property var cardModel: buildHubCards()

    // Rebuild when hubModel changes
    onHubModelChanged: cardModel = buildHubCards()

    // Navigate to a category detail view
    function navigateToCategory(section) {
        if (settingsMenu) {
            settingsMenu.currentSection = section
            if (settingsManager)
                settingsManager.set_last_settings_section(section)
        }
        viewState = "detail"
    }

    // Return to hub
    function navigateToHub() {
        // Save scroll position before leaving detail
        if (contentLoader.item && typeof contentLoader.item.contentY !== "undefined" && settingsMenu) {
            ScrollMemory.positions[settingsMenu.currentSection] = contentLoader.item.contentY
        }
        cardModel = buildHubCards()
        viewState = "hub"
    }

    // Live hub refresh
    Connections {
        target: typeof obdManager !== "undefined" && obdManager ? obdManager : null
        enabled: viewState === "hub"
        function onConnectionStatusChanged() { cardModel = buildHubCards() }
    }

    Connections {
        target: typeof esp32VolumeManager !== "undefined" && esp32VolumeManager ? esp32VolumeManager : null
        enabled: viewState === "hub"
        function onConnectionStatusChanged() { cardModel = buildHubCards() }
    }

    // Count grid rows for a flat (no-span) grid
    function getRowCount(cols) {
        return Math.ceil(cardModel.length / cols)
    }

    // ─── Hub State ───
    Item {
        id: hubView
        anchors.fill: parent
        visible: false
        opacity: 0

        GridLayout {
            id: hubGrid
            anchors.fill: parent
            anchors.margins: App.Spacing.settingsHubGridSpacing
            columns: width > 800 ? 3 : 2
            columnSpacing: App.Spacing.settingsHubGridSpacing
            rowSpacing: App.Spacing.settingsHubGridSpacing

            // Compute card height to fill available space (no scrolling)
            property int rowCount: getRowCount(columns)
            property real cardHeight: (height - (rowCount - 1) * rowSpacing) / rowCount

            Repeater {
                model: cardModel.length

                SettingsHubGridCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hubGrid.cardHeight
                    categoryName: cardModel[index] ? cardModel[index].name : ""
                    section: cardModel[index] ? cardModel[index].section : ""
                    widgetItems: cardModel[index] ? cardModel[index].widgetItems : []
                    categoryIcon: cardModel[index] ? cardModel[index].icon : ""
                    radius: App.EnvironmentTheme.active.hubCardRadius

                    onCategorySelected: function(sec) {
                        hubLayout.navigateToCategory(sec)
                    }

                    // Update notification dot
                    Rectangle {
                        id: aboutCardPulse
                        width: dp(8)
                        height: dp(8)
                        radius: width / 2
                        color: "#FF9800"
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: dp(6)
                        anchors.rightMargin: dp(6)
                        visible: cardModel[index] && cardModel[index].section === "about"
                                 && settingsMenu && settingsMenu.updateAvailable

                        SequentialAnimation on opacity {
                            running: aboutCardPulse.visible && settingsMenu && settingsMenu.updateAvailable  // visible includes hidden pages/sections
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                        }
                    }
                }
            }
        }
    }

    // ─── Detail State ───
    Item {
        id: detailView
        anchors.fill: parent
        visible: false
        opacity: 0

        Rectangle {
            anchors.fill: parent
            color: App.Style.contentColor

            // HUD lines (spacecraft)
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: App.Spacing.settingsContentMargin
                anchors.rightMargin: App.Spacing.settingsContentMargin
                height: 1
                z: 1
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                visible: App.EnvironmentTheme.active.contentHudLines
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: App.Spacing.settingsContentMargin
                anchors.rightMargin: App.Spacing.settingsContentMargin
                height: 1
                z: 1
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                visible: App.EnvironmentTheme.active.contentHudLines
            }

            // ─── Heading bar ───
            Item {
                id: headingBar
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: App.Spacing.overallSpacing * 2.4
                z: 2

                Row {
                    id: headingRow
                    anchors.left: parent.left
                    anchors.leftMargin: App.Spacing.settingsContentMargin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: App.Spacing.overallSpacing * 0.5

                    Text {
                        text: "\u2190"
                        color: floatingLabelArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 1.1
                        font.family: App.Style.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    Text {
                        text: settingsMenu ? settingsMenu.nameForSection(settingsMenu.currentSection) : ""
                        color: floatingLabelArea.containsMouse ? App.Style.accent : App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText * 1.1
                        font.bold: true
                        font.family: App.Style.fontFamily
                        font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
                        font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                }

                MouseArea {
                    id: floatingLabelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hubLayout.navigateToHub()
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: App.Style.accent
                }
            }

            // ─── Content below heading ───
            Loader {
                id: contentLoader
                anchors {
                    top: headingBar.bottom
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: App.Spacing.settingsContentMargin
                    rightMargin: App.Spacing.settingsContentMargin
                    // Same gap under the header and above the bottom bar as the music library
                    topMargin: App.Spacing.overallMargin
                    bottomMargin: App.Spacing.overallMargin
                }
                source: viewState === "detail" && settingsMenu ? sourceForSection(settingsMenu.currentSection) : ""

                property string previousSection: ""

                onSourceChanged: {
                    if (previousSection && item && typeof item.contentY !== "undefined") {
                        ScrollMemory.positions[previousSection] = item.contentY
                    }
                    previousSection = settingsMenu ? settingsMenu.currentSection : ""
                }

                onLoaded: {
                    if (item && typeof item.mainWindow !== "undefined" && settingsMenu)
                        item.mainWindow = settingsMenu.mainWindow
                    if (item && typeof item.stackView !== "undefined" && settingsMenu)
                        item.stackView = settingsMenu.stackView
                    if (item && typeof item.currentSection !== "undefined" && settingsMenu)
                        item.currentSection = Qt.binding(function() { return settingsMenu ? settingsMenu.currentSection : "" })

                    // Restore scroll position (use timer to wait for content to layout)
                    if (settingsMenu && settingsMenu.currentSection !== "") {
                        var savedY = ScrollMemory.positions[settingsMenu.currentSection]
                        if (item && typeof item.contentY !== "undefined" && savedY !== undefined && savedY > 0) {
                            scrollRestoreTimer.savedY = savedY
                            scrollRestoreTimer.restart()
                        }
                    }
                }

                // Save scroll position continuously as user scrolls
                Connections {
                    target: contentLoader.item
                    function onContentYChanged() {
                        if (contentLoader.item && settingsMenu && settingsMenu.currentSection !== "")
                            ScrollMemory.positions[settingsMenu.currentSection] = contentLoader.item.contentY
                    }
                }

                // Polls until the Flickable has valid dimensions, then restores scroll
                Timer {
                    id: scrollRestoreTimer
                    interval: 16
                    repeat: true
                    property real savedY: -1
                    onTriggered: {
                        var fl = contentLoader.item
                        if (fl && fl.contentHeight > 0 && fl.height > 0 && savedY >= 0) {
                            fl.contentY = Math.min(savedY, Math.max(0, fl.contentHeight - fl.height))
                            savedY = -1
                            stop()
                        }
                    }
                }
            }
        }
    }

    // ─── State management ───
    states: [
        State {
            name: "hub"
            when: viewState === "hub"
            PropertyChanges { target: hubView; visible: true; opacity: 1.0 }
            PropertyChanges { target: detailView; visible: false; opacity: 0 }
        },
        State {
            name: "detail"
            when: viewState === "detail"
            PropertyChanges { target: hubView; visible: false; opacity: 0 }
            PropertyChanges { target: detailView; visible: true; opacity: 1.0 }
        }
    ]

    transitions: [
        Transition {
            from: "hub"; to: "detail"
            SequentialAnimation {
                NumberAnimation { target: hubView; property: "opacity"; to: 0; duration: 150; easing.type: Easing.OutQuad }
                PropertyAction { target: hubView; property: "visible"; value: false }
                PropertyAction { target: detailView; property: "visible"; value: true }
                NumberAnimation { target: detailView; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.InQuad }
            }
        },
        Transition {
            from: "detail"; to: "hub"
            SequentialAnimation {
                NumberAnimation { target: detailView; property: "opacity"; to: 0; duration: 150; easing.type: Easing.OutQuad }
                PropertyAction { target: detailView; property: "visible"; value: false }
                PropertyAction { target: hubView; property: "visible"; value: true }
                NumberAnimation { target: hubView; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.InQuad }
            }
        }
    ]
}
