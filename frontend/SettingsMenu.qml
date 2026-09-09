import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"
import "settings/ExpandedCards.js" as ExpandedCards

Item {
    id: settingsMenu
    objectName: "settingsMenu"
    required property var stackView
    required property var mainWindow
    required property string initialSection

    property string currentSection: ""
    property bool updateAvailable: networkManager && networkManager.updateStatus === "update-available"

    // Central page model — single source of truth for all pages
    // NOTE: `source` paths are relative to frontend/ (consumed by layout Loaders in frontend/)
    //       `widget` paths are relative to frontend/settings/ (consumed by SettingsDashboardCard Loader)
    // `subSections` entries must match the corresponding page's tileModel
    // titles exactly \u2014 SettingsMenu.findChildByObjectName() looks cards up by
    // objectName, which the pages set from modelData.title.
    //
    // `icon` is the legacy Unicode glyph, kept only as a fallback; `iconSource`
    // is the theme-tinted SVG that actually renders.
    readonly property var pageModel: [
        { name: "Display",     section: "displaySettings",     source: "settings/DisplaySettingsPage.qml",     widget: "widgets/DisplayWidget.qml",     icon: "\u263C", iconSource: Style.assetBase + "cat_display.svg",
          group: "Appearance",
          subSections: ["Layout", "Window", "Appearance", "Clock"] },
        { name: "Media",       section: "mediaSettings",       source: "settings/MediaSettingsPage.qml",       widget: "widgets/MediaWidget.qml",       icon: "\u266B", iconSource: Style.assetBase + "media_button.svg",
          group: "Appearance",
          subSections: ["Library", "Playback", "Now Playing", "Spotify"] },
        { name: "OBD",         section: "obdSettings",         source: "settings/OBDSettingsPage.qml",         widget: "widgets/OBDWidget.qml",         icon: "\u26A1", iconSource: Style.assetBase + "obd_button.svg",
          group: "Connectivity",
          subSections: ["Connection", "Adapters", "Tuning", "Parameters"] },
        { name: "Accessories", section: "accessoriesSettings", source: "settings/AccessoriesSettingsPage.qml", widget: "widgets/AccessoriesWidget.qml", icon: "\u2388", iconSource: Style.assetBase + "sensor_button.svg",
          group: "Connectivity",
          desktopOnly: true,
          subSections: ["Volume Knob", "IMU Sensor", "Gesture Sensor", "Phone Dock"] },
        { name: "Device",      section: "deviceSettings",      source: "settings/DeviceSettingsPage.qml",      widget: "widgets/DeviceWidget.qml",      icon: "\u2699", iconSource: Style.assetBase + "settings_button.svg",
          group: "System",
          subSections: ["Device Name", "Network", "Power"] },
        { name: "About",       section: "about",               source: "settings/AboutPage.qml",               widget: "widgets/AboutWidget.qml",       icon: "\u2139", iconSource: Style.assetBase + "cat_about.svg",
          group: "System",
          subSections: [] }
    ]

    // Derived section order for fallback
    readonly property var sectionOrder: {
        var order = []
        for (var i = 0; i < pageModel.length; i++)
            order.push(pageModel[i].section)
        return order
    }

    // Visible pages model
    property var hubModel: []

    // Current layout style
    property string layoutStyle: settingsManager ? settingsManager.settingsLayoutStyle : "Sidebar"

    // True only on the very first layout load (app launch); false after a mid-session switch
    property bool isInitialLoad: true

    Component.onCompleted: {
        // Pre-load expanded card states cache before any cards are created
        ExpandedCards.load(settingsManager)

        buildHubModel()

        // Restore last active section for all layouts
        if (initialSection && initialSection !== "") {
            var idx = indexForSection(initialSection)
            if (idx >= 0) {
                currentSection = initialSection
            }
        }
    }

    function pageForSection(section) {
        for (var i = 0; i < pageModel.length; i++) {
            if (pageModel[i].section === section)
                return pageModel[i]
        }
        return null
    }

    function nameForSection(section) {
        for (var i = 0; i < pageModel.length; i++) {
            if (pageModel[i].section === section)
                return pageModel[i].name
        }
        return "Settings"
    }

    function isVisible(section) {
        return settingsManager ? settingsManager.is_settings_section_visible(section) : true
    }

    function indexForSection(section) {
        for (var i = 0; i < hubModel.length; i++) {
            if (hubModel[i].section === section)
                return i
        }
        return -1
    }

    function buildHubModel() {
        var model = []
        var android = (typeof isAndroid !== "undefined" && isAndroid)
        for (var i = 0; i < pageModel.length; i++) {
            var page = pageModel[i]
            if (page.desktopOnly && android) continue
            if (!isVisible(page.section)) continue
            model.push({
                name: page.name,
                section: page.section,
                source: page.source,
                icon: page.icon,
                iconSource: page.iconSource || "",
                group: page.group || "",
                widget: page.widget || ""
            })
        }
        hubModel = model
    }

    function navigateToHub() {
        if (layoutLoader.item && typeof layoutLoader.item.navigateToHub === "function") {
            layoutLoader.item.navigateToHub()
        }
    }

    function navigateToCategory(section) {
        currentSection = section
        if (settingsManager)
            settingsManager.set_last_settings_section(section)

        // Delegate to layout
        if (layoutLoader.item) {
            if (layoutStyle === "Carousel" && typeof layoutLoader.item.currentIndex !== "undefined") {
                var idx = indexForSection(section)
                if (idx >= 0) layoutLoader.item.currentIndex = idx
            } else if (typeof layoutLoader.item.navigateToCategory === "function") {
                layoutLoader.item.navigateToCategory(section)
            }
        }
    }

    function navigateToSubSection(section, subSection) {
        if (layoutLoader.item && typeof layoutLoader.item.navigateToSubSection === "function") {
            layoutLoader.item.navigateToSubSection(section, subSection)
        } else {
            navigateToCategory(section)
        }
    }

    property string pendingScrollTarget: ""

    // When visibility changes, rebuild
    Connections {
        target: settingsManager
        function onSettingsMenuVisibilityChanged() {
            buildHubModel()
        }
    }

    // When layout style changes, update and reload
    Connections {
        target: settingsManager
        function onSettingsLayoutStyleChanged(style) {
            isInitialLoad = false
            layoutStyle = style
        }
    }

    // Layout source mapping
    function layoutSourceForStyle(style) {
        switch (style) {
            case "Carousel":  return "SettingsCarouselLayout.qml"
            case "Sidebar":   return "SettingsSidebarLayout.qml"
            case "Hub":       return "SettingsHubLayout.qml"
            case "Dashboard": return "SettingsDashboardLayout.qml"
            default:          return "SettingsSidebarLayout.qml"
        }
    }

    // MAIN LAYOUT
    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        Loader {
            id: layoutLoader
            anchors.fill: parent
            source: layoutSourceForStyle(layoutStyle)

            onLoaded: {
                if (item) {
                    item.settingsMenu = settingsMenu
                    item.hubModel = Qt.binding(function() { return settingsMenu.hubModel })

                    // For Carousel, set initial index
                    if (layoutStyle === "Carousel" && initialSection && initialSection !== "") {
                        var idx = indexForSection(initialSection)
                        if (idx >= 0 && typeof item.currentIndex !== "undefined") {
                            item.currentIndex = idx
                            currentSection = initialSection
                        }
                    }
                }
            }
        }
    }

    // ─── Sub-section scroll targeting (for Carousel layout) ───
    Timer {
        id: scrollToSectionTimer
        interval: 16
        repeat: true
        onTriggered: {
            if (pendingScrollTarget === "") { stop(); return }

            // Only applies to Carousel layout's SwipeView
            if (layoutStyle !== "Carousel" || !layoutLoader.item) return

            // Find the SwipeView's current item
            var swipeView = null
            for (var i = 0; i < layoutLoader.item.children.length; i++) {
                var child = layoutLoader.item.children[i]
                if (child.toString().indexOf("SwipeView") !== -1) {
                    swipeView = child
                    break
                }
            }
            if (!swipeView) return

            var card = swipeView.currentItem
            if (!card) return
            var loader = findLoader(card)
            if (!loader || !loader.item) return

            var fl = loader.item
            if (fl.contentHeight <= 0 || fl.height <= 0) return

            var content = fl.contentItem ? fl.contentItem : fl
            var targetCard = findChildByObjectName(content, pendingScrollTarget)
            if (targetCard) {
                var pos = targetCard.mapToItem(fl.contentItem, 0, 0)
                var scrollMax = Math.max(0, fl.contentHeight - fl.height)
                fl.contentY = Math.min(pos.y, scrollMax)
                pendingScrollTarget = ""
                stop()
            }
        }

        function findLoader(item) {
            for (var i = 0; i < item.children.length; i++) {
                var child = item.children[i]
                if (child.toString().indexOf("QQuickLoader") !== -1 && child.item)
                    return child
                var found = findLoader(child)
                if (found) return found
            }
            return null
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
}
