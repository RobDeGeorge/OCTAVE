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

    id: dashboardLayout

    // Required from orchestrator
    property var settingsMenu: null
    property var hubModel: []

    // State: "hub" = card grid landing, "detail" = full-width settings page
    property string viewState: ""

    // Sub-section scroll targeting
    property string pendingScrollTarget: ""

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

    // Widget source for a section
    function getWidgetForSection(section) {
        if (!settingsMenu) return ""
        for (var i = 0; i < settingsMenu.pageModel.length; i++) {
            if (settingsMenu.pageModel[i].section === section)
                return settingsMenu.pageModel[i].widget || ""
        }
        return ""
    }

    // Sections that appear in the top "featured" row of the hub, in order.
    readonly property var featuredSections: ["displaySettings", "mediaSettings", "obdSettings"]

    function makeHubCard(page) {
        return {
            name: page.name,
            section: page.section,
            icon: page.icon,
            widgetItems: [],
            widgetSource: getWidgetForSection(page.section)
        }
    }

    // Two-row hub model: top row = featured cards (in featured order, only those visible),
    // bottom row = remaining visible cards in their original pageModel order.
    property var pyramidRows: {
        if (!hubModel || hubModel.length === 0) return []
        var available = {}
        for (var i = 0; i < hubModel.length; i++) {
            available[hubModel[i].section] = hubModel[i]
        }
        var top = []
        for (var f = 0; f < featuredSections.length; f++) {
            var sec = featuredSections[f]
            if (available[sec]) {
                top.push(makeHubCard(available[sec]))
                delete available[sec]
            }
        }
        var bottom = []
        for (var j = 0; j < hubModel.length; j++) {
            var page = hubModel[j]
            if (available[page.section]) {
                bottom.push(makeHubCard(page))
            }
        }
        var rows = []
        if (top.length > 0) rows.push(top)
        if (bottom.length > 0) rows.push(bottom)
        return rows
    }

    // Navigate to a category detail view
    function navigateToCategory(section) {
        if (settingsMenu) {
            settingsMenu.currentSection = section
            if (settingsManager)
                settingsManager.set_last_settings_section(section)
        }
        viewState = "detail"
    }

    // Navigate to sub-section
    function navigateToSubSection(section, subSection) {
        pendingScrollTarget = subSection
        navigateToCategory(section)
    }

    // Return to hub
    function navigateToHub() {
        viewState = "hub"
    }

    // ─── Hub State ───
    Item {
        id: hubView
        anchors.fill: parent
        visible: false
        opacity: 0

        ColumnLayout {
            id: hubColumn
            anchors.fill: parent
            anchors.margins: App.Spacing.settingsHubGridSpacing
            spacing: App.Spacing.settingsHubGridSpacing

            Repeater {
                model: pyramidRows

                RowLayout {
                    id: pyramidRow
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: App.Spacing.settingsHubGridSpacing
                    property var rowCards: modelData

                    Repeater {
                        model: pyramidRow.rowCards

                        SettingsDashboardCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            categoryName: modelData.name
                            section: modelData.section
                            widgetItems: modelData.widgetItems
                            widgetSource: modelData.widgetSource
                            categoryIcon: modelData.icon
                            cardSpan: 1
                            radius: dpMin(App.EnvironmentTheme.active.hubCardRadius, 2)

                            onCategorySelected: function(sec) {
                                dashboardLayout.navigateToCategory(sec)
                            }
                            onSubSectionSelected: function(sec, sub) {
                                dashboardLayout.navigateToSubSection(sec, sub)
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
                                visible: modelData.section === "about"
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
                    onClicked: dashboardLayout.navigateToHub()
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: App.Style.accent
                }
            }

            // ─── Scroll rail (touch-friendly drag area) ───
            Rectangle {
                id: scrollRail
                anchors.top: headingBar.bottom
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: dp(40)
                color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.03)

                property var flick: contentLoader.item
                property bool canScroll: flick && flick.contentHeight > flick.height

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                }

                Rectangle {
                    id: scrollHandle
                    visible: scrollRail.canScroll
                    width: dp(6)
                    anchors.horizontalCenter: parent.horizontalCenter
                    radius: width / 2
                    color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                                   scrollHandleArea.pressed ? 0.8 : scrollHandleArea.containsMouse ? 0.6 : 0.4)

                    Behavior on color { ColorAnimation { duration: 100 } }

                    property real trackHeight: scrollRail.height
                    property real handleRatio: scrollRail.flick ? Math.min(1, scrollRail.flick.height / scrollRail.flick.contentHeight) : 1
                    height: Math.max(dp(50), trackHeight * handleRatio)

                    y: {
                        if (!scrollRail.canScroll) return 0
                        var scrollRange = scrollRail.flick.contentHeight - scrollRail.flick.height
                        var trackRange = scrollRail.height - height
                        if (scrollRange <= 0) return 0
                        return (scrollRail.flick.contentY / scrollRange) * trackRange
                    }
                }

                MouseArea {
                    id: scrollHandleArea
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true

                    property real dragStartY: 0
                    property real dragStartContentY: 0

                    onPressed: function(mouse) {
                        if (!scrollRail.canScroll) return
                        var scrollRange = scrollRail.flick.contentHeight - scrollRail.flick.height
                        var trackRange = scrollRail.height - scrollHandle.height
                        if (trackRange > 0) {
                            var targetHandleY = Math.max(0, Math.min(mouse.y - scrollHandle.height / 2, trackRange))
                            scrollRail.flick.contentY = (targetHandleY / trackRange) * scrollRange
                        }
                        dragStartY = mouse.y
                        dragStartContentY = scrollRail.flick.contentY
                    }

                    onPositionChanged: function(mouse) {
                        if (!pressed || !scrollRail.canScroll) return
                        var deltaY = mouse.y - dragStartY
                        var scrollRange = scrollRail.flick.contentHeight - scrollRail.flick.height
                        var trackRange = scrollRail.height - scrollHandle.height
                        if (trackRange <= 0) return
                        var newContentY = dragStartContentY + (deltaY / trackRange) * scrollRange
                        scrollRail.flick.contentY = Math.max(0, Math.min(newContentY, scrollRange))
                    }
                }
            }

            // ─── Content below heading ───
            Loader {
                id: contentLoader
                anchors {
                    top: headingBar.bottom
                    left: scrollRail.right
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: App.Spacing.settingsContentMargin
                    // Same gap under the header and above the bottom bar as the music library
                    topMargin: App.Spacing.overallMargin
                    bottomMargin: App.Spacing.overallMargin
                }
                source: viewState === "detail" && settingsMenu ? sourceForSection(settingsMenu.currentSection) : ""

                onLoaded: {
                    if (item && typeof item.mainWindow !== "undefined" && settingsMenu)
                        item.mainWindow = settingsMenu.mainWindow
                    if (item && typeof item.stackView !== "undefined" && settingsMenu)
                        item.stackView = settingsMenu.stackView
                    if (item && typeof item.currentSection !== "undefined" && settingsMenu)
                        item.currentSection = Qt.binding(function() { return settingsMenu ? settingsMenu.currentSection : "" })

                    if (pendingScrollTarget !== "") {
                        scrollToSectionTimer.targetName = pendingScrollTarget
                        pendingScrollTarget = ""
                        scrollToSectionTimer.restart()
                    } else {
                        var savedY = settingsMenu ? ScrollMemory.positions[settingsMenu.currentSection] : undefined
                        if (item && typeof item.contentY !== "undefined" && savedY !== undefined && savedY > 0) {
                            scrollRestoreTimer.savedY = savedY
                            scrollRestoreTimer.restart()
                        } else {
                            scrollRestoreTimer.stop()
                        }
                    }
                }
            }

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

            Timer {
                id: scrollToSectionTimer
                interval: 16
                repeat: true
                property string targetName: ""
                onTriggered: {
                    var fl = contentLoader.item
                    if (!fl || fl.contentHeight <= 0 || fl.height <= 0 || targetName === "") return

                    var content = fl.contentItem ? fl.contentItem : fl
                    var targetCard = findChildByObjectName(content, targetName)
                    if (targetCard) {
                        var pos = targetCard.mapToItem(fl.contentItem, 0, 0)
                        var scrollMax = Math.max(0, fl.contentHeight - fl.height)
                        fl.contentY = Math.min(pos.y, scrollMax)
                        targetName = ""
                        stop()
                    }
                }

                function findChildByObjectName(parent, name) {
                    for (var i = 0; i < parent.children.length; i++) {
                        var child = parent.children[i]
                        if (child.objectName === name) return child
                        var found = findChildByObjectName(child, name)
                        if (found) return found
                    }
                    return null
                }
            }

            Connections {
                target: contentLoader.item
                function onContentYChanged() {
                    if (contentLoader.item && settingsMenu && settingsMenu.currentSection)
                        ScrollMemory.positions[settingsMenu.currentSection] = contentLoader.item.contentY
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

    transitions: []
}
