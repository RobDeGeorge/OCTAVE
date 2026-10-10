import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App
import "ScrollMemory.js" as ScrollMemory

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: hubCard

    property string categoryName: ""
    property string section: ""
    property string categoryIcon: ""
    property string groupName: ""
    property string pageSource: ""
    property bool isCenter: false

    // Passed through from SettingsMenu so loaded pages can access them
    property var settingsMenu: null

    // ── Tile-mode state (mirrors SettingsSidebarLayout) ─────────────────────
    // When the loaded page exposes a `tileModel`, the card hides the page's
    // Flickable and renders a tile grid + hero-morph popup, just like the
    // sidebar's content area. Pages without a tileModel render normally.
    property bool useTileLayout: false
    property string detailCardId: ""
    property var detailTile: null
    // Rect of the clicked tile in cardContent coordinates — drives the
    // popup's zoom-from-tile transition.
    property rect originRect: Qt.rect(0, 0, 0, 0)

    function openTile(cardId, rect) {
        if (!pageLoader.item || typeof pageLoader.item.tileModel === "undefined")
            return
        var tm = pageLoader.item.tileModel
        for (var i = 0; i < tm.length; i++) {
            if (tm[i].cardId === cardId) {
                detailTile = tm[i]
                if (rect) originRect = rect
                detailCardId = cardId
                return
            }
        }
    }

    function closeTile() {
        detailCardId = ""
        // The popup keeps showing this tile's content while it shrinks and
        // unloads it once closed, so the inner component's
        // Component.onDestruction still fires (e.g. Now Playing live PiP).
        detailTile = null
    }

    // Close any open popup when the card scrolls away from center.
    onIsCenterChanged: {
        if (!isCenter) closeTile()
    }

    color: "transparent"
    radius: dpMin(App.EnvironmentTheme.active.cardRadius, 2)
    clip: true

    // Accent border (spacecraft)
    border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
    border.color: App.EnvironmentTheme.active.accentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                  App.EnvironmentTheme.active.accentBorderOpacity) : "transparent"

    // Card background — elevated surface
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: {
            var base = App.Style.contentColor
            return Qt.rgba(base.r, base.g, base.b, 0.92)
        }
    }

    // Subtle white lift
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Qt.rgba(1, 1, 1, 0.05)
    }

    // Frosted highlight gradient
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 0.15; color: Qt.rgba(1, 1, 1, 0.02) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Top edge highlight line
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: parent.radius
        anchors.rightMargin: parent.radius
        height: 1
        color: Qt.rgba(1, 1, 1, 0.08)
    }

    App.CornerBrackets {
        visible: App.EnvironmentTheme.active.cornerBrackets
    }

    // Bottom accent line
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: App.Style.accent }
            GradientStop { position: 0.7; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: 0.6
    }


    // ─── Card content: settings page fills entire card ───
    Item {
        id: cardContent
        anchors.fill: parent
        anchors.leftMargin: App.Spacing.overallSpacing * 0.3
        anchors.rightMargin: App.Spacing.overallSpacing * 0.3
        anchors.topMargin: dp(2)
        anchors.bottomMargin: dp(2)

        Loader {
            id: pageLoader
            anchors.fill: parent
            source: hubCard.pageSource ? ("../" + hubCard.pageSource) : ""
            active: hubCard.isCenter

            onSourceChanged: {
                detailPopup.snapShut()
                hubCard.detailCardId = ""
                hubCard.detailTile = null
                hubCard.useTileLayout = false
            }

            onLoaded: {
                if (item) {
                    if (typeof item.mainWindow !== "undefined")
                        item.mainWindow = hubCard.settingsMenu ? hubCard.settingsMenu.mainWindow : null
                    if (typeof item.stackView !== "undefined")
                        item.stackView = hubCard.settingsMenu ? hubCard.settingsMenu.stackView : null
                    if (typeof item.currentSection !== "undefined")
                        item.currentSection = Qt.binding(function() { return hubCard.section })
                }

                // Detect tile-mode page (exposes a tileModel array) and hide
                // the page's own Flickable rendering — the tile grid replaces it.
                var hasTiles = item && typeof item.tileModel !== "undefined"
                hubCard.useTileLayout = hasTiles
                if (hasTiles) {
                    item.visible = false
                }

                // Restore saved scroll position only for non-tile pages
                if (!hasTiles && hubCard.section !== "") {
                    var savedY = ScrollMemory.positions[hubCard.section]
                    if (item && typeof item.contentY !== "undefined" && savedY !== undefined && savedY > 0) {
                        scrollRestoreTimer.savedY = savedY
                        scrollRestoreTimer.restart()
                    }
                }
            }
        }

        // ── Tile grid (replaces page rendering when page exposes tileModel) ─
        SettingsTilePage {
            id: tileGrid
            anchors.fill: parent
            z: 2
            visible: hubCard.useTileLayout
            tileModel: hubCard.useTileLayout && pageLoader.item
                ? pageLoader.item.tileModel : []
            onTileSelected: function(cardId, rect) {
                // "Now Playing" hijacks the whole window via mainWindow,
                // not the in-card popup, since it needs every pixel.
                if (cardId === "media_now_playing"
                    && hubCard.settingsMenu && hubCard.settingsMenu.mainWindow
                    && typeof hubCard.settingsMenu.mainWindow.openNowPlayingStudio === "function") {
                    // Map the tile's rect (in tilePage.parent coords) into
                    // the scene root so the studio's hero-morph animation
                    // can grow from exactly where the user tapped.
                    var src = tileGrid.parent
                    var p = src.mapToItem(null, rect.x, rect.y)
                    hubCard.settingsMenu.mainWindow.openNowPlayingStudio(
                        Qt.rect(p.x, p.y, rect.width, rect.height))
                    return
                }
                hubCard.openTile(cardId, rect)
            }
        }

        // ── Detail popup (hero/morph: grows from the clicked tile's rect) ───
        SettingsCardPopup {
            id: detailPopup
            z: 3
            visible: hubCard.useTileLayout && morphing
            open: hubCard.detailCardId !== ""
            originRect: hubCard.originRect
            title: hubCard.detailTile ? hubCard.detailTile.title : ""
            contentComponent: hubCard.detailTile ? hubCard.detailTile.component : null

            onBackRequested: hubCard.closeTile()
        }

        // Polls until the Flickable has valid dimensions, then restores scroll
        Timer {
            id: scrollRestoreTimer
            interval: 16
            repeat: true
            property real savedY: -1
            onTriggered: {
                var fl = pageLoader.item
                if (fl && fl.contentHeight > 0 && fl.height > 0 && savedY >= 0) {
                    fl.contentY = Math.min(savedY, Math.max(0, fl.contentHeight - fl.height))
                    savedY = -1
                    stop()
                }
            }
        }

        // Save scroll position continuously as the user scrolls
        // (only fires for non-tile pages; tile-mode pages have their Flickable hidden)
        Connections {
            target: pageLoader.item
            function onContentYChanged() {
                if (pageLoader.item && hubCard.section !== "" && !hubCard.useTileLayout)
                    ScrollMemory.positions[hubCard.section] = pageLoader.item.contentY
            }
        }
    }

}
