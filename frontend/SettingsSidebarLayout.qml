import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"

// Default settings layout (settingsLayoutStyle "Sidebar" — the stored name
// predates the grid). One flat grid of every settings card from every
// section, like an app drawer; tapping a tile grows its card into a popup.
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: sidebarLayout

    // Required from orchestrator
    property var settingsMenu: null
    property var hubModel: []

    // The grid always has this many rows; columns grow to fit the tiles, so a
    // tile count that is a multiple of it (20 on desktop, 16 on Android
    // without Accessories) fills the grid with no empty cells.
    readonly property int gridRows: 4

    function openTileId() {
        return contentArea.detailCardId
    }

    // Settings button while already in settings: close the open card.
    function navigateToHub() {
        if (contentArea.detailCardId !== "")
            contentArea.closeTile()
    }

    // There are no section pages any more. About is the one section shown as
    // a single card, so asking for it (e.g. closing the wiki) opens that card.
    function navigateToCategory(section) {
        if (section === "about" && contentArea.detailCardId !== "section:about")
            contentArea.openTile("section:about", null)
    }

    // Every visible section's tiles, tagged with their section. About has no
    // tiles of its own, so it gets one tile that opens the whole page.
    // Bumped as hidden pages load — Repeater.itemAt() is not a binding dependency.
    property int _allPagesLoaded: 0

    function _aboutSummary() {
        var d = typeof diagnosticsManager !== "undefined" ? diagnosticsManager : null
        var info = d ? { state: "v" + d.appVersion + " · " + d.backendName } : {}
        if (settingsMenu && settingsMenu.updateAvailable)
            info.status = "#FF9800"
        return info
    }
    readonly property var allTiles: {
        var _ = _allPagesLoaded
        var tiles = []
        for (var i = 0; i < hubModel.length; i++) {
            var entry = hubModel[i]
            if (entry.section === "about") {
                tiles.push({ cardId: "section:about", title: entry.name,
                             summary: sidebarLayout._aboutSummary,
                             component: aboutContent, section: entry.section })
                continue
            }
            var loader = allPagesRepeater.itemAt(i)
            var page = loader ? loader.item : null
            if (page && typeof page.tileModel !== "undefined") {
                for (var j = 0; j < page.tileModel.length; j++) {
                    var t = page.tileModel[j]
                    tiles.push({ cardId: t.cardId, title: t.title, summary: t.summary,
                                 component: t.component, section: entry.section })
                }
            }
        }
        return tiles
    }

    // Reopen the card the user was in before a rebuild (nav bar orientation
    // change). Waits for the section pages, which own the cards, to load.
    onAllTilesChanged: {
        if (!settingsMenu || settingsMenu.initialTile === "")
            return
        contentArea.openTile(settingsMenu.initialTile, null)
        if (contentArea.detailCardId === settingsMenu.initialTile)
            settingsMenu.initialTile = ""
    }

    // About opens the full page inside the card popup.
    Component {
        id: aboutContent
        Item {
            implicitHeight: aboutLoader.item ? aboutLoader.item.contentHeight : 0
            Loader {
                id: aboutLoader
                anchors.fill: parent
                source: "settings/AboutPage.qml"
                onLoaded: {
                    item.interactive = false
                    if (settingsMenu)
                        item.stackView = settingsMenu.stackView
                }
            }
        }
    }

    // One hidden instance of each section page, so its tile components
    // (which live in the page) can fill the popup. Built with renderCards
    // false so the pages' own stacked cards are never created.
    Repeater {
        id: allPagesRepeater
        model: sidebarLayout.hubModel
        onItemAdded: sidebarLayout._allPagesLoaded++
        onItemRemoved: sidebarLayout._allPagesLoaded++
        delegate: Loader {
            visible: false
            active: modelData.section !== "about"
            Component.onCompleted: if (active) setSource(modelData.source, { renderCards: false })
            onLoaded: {
                sidebarLayout._allPagesLoaded++
                if (typeof item.mainWindow !== "undefined" && settingsMenu)
                    item.mainWindow = settingsMenu.mainWindow
                if (typeof item.stackView !== "undefined" && settingsMenu)
                    item.stackView = settingsMenu.stackView
                if (typeof item.currentSection !== "undefined")
                    item.currentSection = modelData.section
            }
        }
    }

    // "Now Playing" hijacks the whole window — see SettingsHubCard.
    function selectTile(cardId, rect) {
        if (cardId === "media_now_playing"
            && settingsMenu && settingsMenu.mainWindow
            && typeof settingsMenu.mainWindow.openNowPlayingStudio === "function") {
            var p = allTilesGrid.parent.mapToItem(null, rect.x, rect.y)
            settingsMenu.mainWindow.openNowPlayingStudio(
                Qt.rect(p.x, p.y, rect.width, rect.height))
            return
        }
        contentArea.openTile(cardId, rect)
    }

    Rectangle {
        id: contentArea
        anchors.fill: parent
        color: App.Style.contentColor
        clip: true

        property string detailCardId: ""
        property var detailTile: null
        // Rect of the clicked tile in contentArea coordinates — drives
        // the popup's zoom-from-tile transition.
        property rect originRect: Qt.rect(0, 0, 0, 0)

        function openTile(cardId, rect) {
            var tm = sidebarLayout.allTiles
            for (var i = 0; i < tm.length; i++) {
                if (tm[i].cardId === cardId) {
                    detailTile = tm[i]
                    originRect = rect ? rect : (allTilesGrid.tileRect(cardId) || originRect)
                    detailCardId = cardId
                    return
                }
            }
        }

        function closeTile() {
            var r = detailCardId !== "" ? allTilesGrid.tileRect(detailCardId) : null
            if (r) originRect = r
            detailCardId = ""
            // The popup keeps showing this tile's content while it shrinks
            // and unloads it once closed, so the inner component's
            // Component.onDestruction still fires (page-scoped resources
            // such as the Now Playing live PiP are released).
            detailTile = null
        }

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

        SettingsTilePage {
            id: allTilesGrid
            anchors {
                fill: parent
                margins: App.Spacing.settingsContentMargin
            }
            z: 2
            tileModel: sidebarLayout.allTiles
            columns: Math.max(1, Math.ceil(tileModel.length / sidebarLayout.gridRows))
            onTileSelected: function(cardId, rect) { sidebarLayout.selectTile(cardId, rect) }
        }

        // ── Detail popup (hero/morph: grows from the clicked tile's rect) ───
        SettingsCardPopup {
            id: detailPopup
            z: 3
            visible: morphing
            open: contentArea.detailCardId !== ""
            originRect: contentArea.originRect
            title: contentArea.detailTile ? contentArea.detailTile.title : ""
            contentComponent: contentArea.detailTile ? contentArea.detailTile.component : null

            onBackRequested: contentArea.closeTile()
        }
    }
}
