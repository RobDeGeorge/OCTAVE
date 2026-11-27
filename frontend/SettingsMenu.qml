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
    // Tile (cardId) to reopen on load — lets a rebuild (e.g. nav bar
    // orientation change) put the user back inside the card they were in.
    property string initialTile: ""

    property string currentSection: ""
    property bool updateAvailable: networkManager && networkManager.updateStatus === "update-available"

    // Central page model — single source of truth for all pages
    // NOTE: `source` paths are relative to frontend/ (consumed by layout Loaders in frontend/)
    // `subSections` entries must match the corresponding page's tileModel
    // titles exactly \u2014 SettingsMenu.findChildByObjectName() looks cards up by
    // objectName, which the pages set from modelData.title.
    //
    // `icon` is the legacy Unicode glyph, kept only as a fallback; `iconSource`
    // is the theme-tinted SVG that actually renders.
    readonly property var pageModel: [
        { name: "Display",     section: "displaySettings",     source: "settings/DisplaySettingsPage.qml", icon: "\u263C", iconSource: Style.assetBase + "cat_display.svg",
          group: "Appearance",
          subSections: ["Layout", "Appearance", "Units", "Clock"] },
        { name: "Media",       section: "mediaSettings",       source: "settings/MediaSettingsPage.qml", icon: "\u266B", iconSource: Style.assetBase + "cat_media.svg",
          group: "Appearance",
          subSections: ["Library & Playback", "Spotify"] },
        { name: "OBD",         section: "obdSettings",         source: "settings/OBDSettingsPage.qml", icon: "\u26A1", iconSource: Style.assetBase + "cat_obd.svg",
          group: "Connectivity",
          subSections: ["Connection", "Black Box", "Advanced"] },
        { name: "Phone",       section: "phoneSettings",       source: "settings/PhoneSettingsPage.qml", icon: "\u260E", iconSource: Style.assetBase + "cat_accessories.svg",
          group: "Connectivity",
          desktopOnly: true,
          subSections: ["Phone Mirror", "Android Auto", "iPhone"] },
        { name: "Accessories", section: "accessoriesSettings", source: "settings/AccessoriesSettingsPage.qml", icon: "\u2388", iconSource: Style.assetBase + "cat_accessories.svg",
          group: "Connectivity",
          desktopOnly: true,
          subSections: ["Volume Knob", "IMU Sensor", "Gesture Sensor"] },
        { name: "Device",      section: "deviceSettings",      source: "settings/DeviceSettingsPage.qml", icon: "\u2699", iconSource: Style.assetBase + "cat_device.svg",
          group: "System",
          subSections: ["Device", "Vehicle", "Developer"] },
        { name: "About",       section: "about",               source: "settings/AboutPage.qml", icon: "\u2139", iconSource: Style.assetBase + "cat_about.svg",
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
                group: page.group || ""
            })
        }
        hubModel = model
    }

    // cardId of the tile popup currently open in the layout, or "".
    function openTileId() {
        if (layoutLoader.item && typeof layoutLoader.item.openTileId === "function")
            return layoutLoader.item.openTileId()
        return ""
    }

    // Open a card of the current section: at once in a layout already up,
    // else when it comes up. Layouts without tile popups just show the section.
    function openTile(cardId) {
        if (layoutLoader.item && typeof layoutLoader.item.openCard === "function")
            layoutLoader.item.openCard(cardId)
        else
            initialTile = cardId
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
        if (layoutLoader.item && typeof layoutLoader.item.navigateToCategory === "function")
            layoutLoader.item.navigateToCategory(section)
    }

    function navigateToSubSection(section, subSection) {
        if (layoutLoader.item && typeof layoutLoader.item.navigateToSubSection === "function") {
            layoutLoader.item.navigateToSubSection(section, subSection)
        } else {
            navigateToCategory(section)
        }
    }

    // When visibility changes, rebuild
    Connections {
        target: settingsManager
        function onSettingsMenuVisibilityChanged() {
            buildHubModel()
        }
    }

    // MAIN LAYOUT
    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        Loader {
            id: layoutLoader
            anchors.fill: parent
            source: "SettingsSidebarLayout.qml"

            onLoaded: {
                if (item) {
                    item.settingsMenu = settingsMenu
                    item.hubModel = Qt.binding(function() { return settingsMenu.hubModel })

                }
            }
        }
    }
}
