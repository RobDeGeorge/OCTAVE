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

    id: sidebarLayout

    // Required from orchestrator
    property var settingsMenu: null
    property var hubModel: []

    // Look up the QML source for a given section
    function sourceForSection(section) {
        if (!settingsMenu) return ""
        for (var i = 0; i < settingsMenu.pageModel.length; i++) {
            if (settingsMenu.pageModel[i].section === section)
                return settingsMenu.pageModel[i].source
        }
        return settingsMenu.pageModel[0].source
    }

    function openTileId() {
        return contentArea.detailCardId
    }

    // ── "All settings" view ───────────────────────────────────────────
    // The settings button (SettingsMenu.handleSettingsButton → navigateToHub)
    // toggles between the normal sidebar + section view and one flat grid of
    // every settings card from every section, like an app drawer.
    property bool allSettingsMode: false

    function navigateToHub() {
        contentArea.closeTile()
        allSettingsMode = !allSettingsMode
    }

    // Every visible section's tiles, tagged with their section. Sections
    // without tiles (About) get a single tile that jumps to the section.
    // Bumped as hidden pages load — Repeater.itemAt() is not a binding dependency.
    property int _allPagesLoaded: 0
    readonly property var allTiles: {
        var _ = _allPagesLoaded
        var tiles = []
        for (var i = 0; i < hubModel.length; i++) {
            var entry = hubModel[i]
            var loader = allPagesRepeater.itemAt(i)
            var page = loader ? loader.item : null
            if (page && typeof page.tileModel !== "undefined") {
                for (var j = 0; j < page.tileModel.length; j++) {
                    var t = page.tileModel[j]
                    tiles.push({ cardId: t.cardId, title: t.title, icon: t.icon || "",
                                 iconSource: t.iconSource || "", component: t.component,
                                 section: entry.section })
                }
            } else if (loader && !loader.active) {
                tiles.push({ cardId: "section:" + entry.section, title: entry.name,
                             icon: entry.icon || "", iconSource: entry.iconSource || "",
                             section: entry.section })
            }
        }
        return tiles
    }

    // One hidden instance of each section page while the grid is open, so
    // its tile components (which live in the page) can fill the popup.
    // Built with renderCards false, like contentLoader, so it stays cheap.
    Repeater {
        id: allPagesRepeater
        model: sidebarLayout.allSettingsMode ? sidebarLayout.hubModel : []
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

    // Shared by both tile grids. "Now Playing" hijacks the whole window — see
    // SettingsHubCard; a section tile leaves the grid for that section.
    function selectTile(cardId, rect, sourceGrid) {
        if (cardId === "media_now_playing"
            && settingsMenu && settingsMenu.mainWindow
            && typeof settingsMenu.mainWindow.openNowPlayingStudio === "function") {
            var p = sourceGrid.parent.mapToItem(null, rect.x, rect.y)
            settingsMenu.mainWindow.openNowPlayingStudio(
                Qt.rect(p.x, p.y, rect.width, rect.height))
            return
        }
        if (cardId.indexOf("section:") === 0) {
            allSettingsMode = false
            if (settingsMenu)
                settingsMenu.navigateToCategory(cardId.substring(8))
            return
        }
        contentArea.openTile(cardId, rect)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle { // Left Navigation Panel
            id: sidebarPanel
            visible: !sidebarLayout.allSettingsMode
            Layout.preferredWidth: App.Spacing.settingsNavWidth
            Layout.fillHeight: true
            color: App.Style.sidebarColor

            // Sidebar lighting gradient (subtle overhead light simulation)
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, 0.04) }
                    GradientStop { position: 0.6; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.06) }
                }
            }

            // Lit edge highlight along the top
            Rectangle {
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }
                height: 1
                color: Qt.rgba(255, 255, 255, 0.08)
            }

            // Soft shadow on the right edge
            Rectangle {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    left: parent.right
                }
                width: 10
                z: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.15) }
                    GradientStop { position: 0.4; color: Qt.rgba(0, 0, 0, 0.06) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            ColumnLayout {
                anchors {
                    fill: parent
                    margins: App.Spacing.settingsNavMargin
                }
                spacing: 0

                ListView {
                    id: navListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // Scroll only in the fallback case where rows hit their floor
                    // (1 px slack absorbs fractional row-height rounding), and
                    // never rubber-band past the ends.
                    interactive: contentHeight > height + 1
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    model: hubModel.length

                    // Rows split the panel height evenly so the list always
                    // fills it exactly — no gap below the last entry. The floor
                    // keeps rows tappable on very short windows (list scrolls).
                    readonly property real rowHeight: count > 0 && height > 0
                        ? Math.max(dp(36), height / count)
                        : App.Spacing.settingsButtonHeight

                    // One shared label size for every row: the largest size
                    // (capped at the design size) at which the longest section
                    // name still fits, so rows never shrink individually.
                    readonly property real labelBaseSize: App.Spacing.overallText * 1.6
                    readonly property real labelFontSize: {
                        var avail = width - 24 - 12
                        var widest = 0
                        for (var i = 0; i < hubModel.length; i++)
                            widest = Math.max(widest, labelMetrics.advanceWidth(hubModel[i].name || ""))
                        // Never taller than about half a row, so labels stay
                        // inside their row when many sections squeeze rows short.
                        var size = Math.min(labelBaseSize, rowHeight * 0.45)
                        if (widest <= 0 || avail <= 0 || widest * size / labelBaseSize <= avail)
                            return size
                        return Math.max(dp(8), Math.floor(labelBaseSize * avail / widest))
                    }
                    FontMetrics {
                        id: labelMetrics
                        // Bold, since the selected row is bold and therefore widest.
                        font.pixelSize: navListView.labelBaseSize
                        font.bold: true
                        font.family: App.Style.fontFamily
                    }

                    delegate: Item {
                        id: delegateRoot
                        property var entry: hubModel[index] || {}
                        property string itemSection: entry.section || ""
                        property string itemName: entry.name || ""

                        width: navListView.width
                        height: navListView.rowHeight

                        // Elevation shadow - outer
                        Rectangle {
                            x: navBackground.x - 2
                            y: navBackground.y + 4
                            width: navBackground.width + 4
                            height: navBackground.height + 2
                            radius: navBackground.radius + 3
                            color: Qt.rgba(0, 0, 0, 0.06)
                            opacity: settingsMenu && settingsMenu.currentSection === parent.itemSection ? 1.0
                                : (navMouseArea.containsMouse ? 0.6 : 0)

                            Behavior on opacity { NumberAnimation { duration: 200 } }
                        }

                        // Elevation shadow - inner
                        Rectangle {
                            x: navBackground.x - 1
                            y: navBackground.y + 2
                            width: navBackground.width + 2
                            height: navBackground.height + 1
                            radius: navBackground.radius + 1
                            color: Qt.rgba(0, 0, 0, 0.12)
                            opacity: settingsMenu && settingsMenu.currentSection === parent.itemSection
                                ? (navMouseArea.pressed ? 0.4 : 1.0)
                                : (navMouseArea.containsMouse ? 0.5 : 0)

                            Behavior on opacity { NumberAnimation { duration: 200 } }
                        }

                        // Background - rounded selection
                        Rectangle {
                            id: navBackground
                            anchors {
                                left: parent.left
                                right: parent.right
                                top: parent.top
                                bottom: parent.bottom
                                leftMargin: 8
                                rightMargin: 8
                                topMargin: 3
                                bottomMargin: 3
                            }
                            radius: App.EnvironmentTheme.active.navItemRadius
                            color: settingsMenu && settingsMenu.currentSection === parent.itemSection
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                : "transparent"
                            scale: navMouseArea.pressed ? 0.97 : 1.0

                            // Accent border (spacecraft)
                            border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
                            border.color: settingsMenu && settingsMenu.currentSection === delegateRoot.itemSection
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.5)
                                : (navMouseArea.containsMouse
                                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                    : "transparent")

                            // Top lit edge on selected/hovered item
                            Rectangle {
                                anchors {
                                    top: parent.top
                                    left: parent.left
                                    right: parent.right
                                    leftMargin: 2
                                    rightMargin: 2
                                }
                                height: 1
                                radius: 0.5
                                color: Qt.rgba(255, 255, 255, 0.1)
                                opacity: settingsMenu && settingsMenu.currentSection === delegateRoot.entry.section ? 1.0
                                    : (navMouseArea.containsMouse ? 0.5 : 0)

                                Behavior on opacity { NumberAnimation { duration: 200 } }
                            }

                            Behavior on color { ColorAnimation { duration: 150 } }
                            Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } }
                        }

                        // Hover effect - rounded
                        Rectangle {
                            id: navHoverRectangle
                            anchors {
                                left: parent.left
                                right: parent.right
                                top: parent.top
                                bottom: parent.bottom
                                leftMargin: 8
                                rightMargin: 8
                                topMargin: 3
                                bottomMargin: 3
                            }
                            radius: App.EnvironmentTheme.active.navItemRadius
                            color: Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)
                            opacity: 0

                            Behavior on opacity { NumberAnimation { duration: 150 } }
                        }

                        // Text
                        Text {
                            anchors {
                                left: parent.left
                                leftMargin: 24
                                right: parent.right
                                rightMargin: 12
                                verticalCenter: parent.verticalCenter
                            }
                            text: parent.itemName
                            color: settingsMenu && settingsMenu.currentSection === parent.itemSection ? App.Style.primaryTextColor : App.Style.secondaryTextColor
                            // Shared size (see navListView.labelFontSize) so every
                            // title shows the whole word at the same size.
                            font.pixelSize: navListView.labelFontSize
                            font.bold: settingsMenu && settingsMenu.currentSection === parent.itemSection
                            font.family: App.Style.fontFamily
                            elide: Text.ElideNone
                        }

                        // Update notification dot
                        Rectangle {
                            id: aboutEntryPulse
                            width: dp(7)
                            height: dp(7)
                            radius: width / 2
                            color: "#FF9800"
                            z: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            visible: delegateRoot.itemSection === "about"
                                     && settingsMenu && settingsMenu.updateAvailable

                            SequentialAnimation on opacity {
                                running: aboutEntryPulse.visible && settingsMenu && settingsMenu.updateAvailable  // visible includes hidden pages/sections
                                loops: Animation.Infinite
                                NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                                NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                            }
                        }

                        // Entire area clickable
                        MouseArea {
                            id: navMouseArea
                            anchors.fill: parent
                            onClicked: {
                                // Save scroll position of current page before switching
                                if (contentLoader.item && typeof contentLoader.item.contentY !== "undefined" && settingsMenu) {
                                    ScrollMemory.positions[settingsMenu.currentSection] = contentLoader.item.contentY
                                }

                                // Sidebar navigation overrides the immersive Now Playing studio —
                                // dismiss it so the user lands on the section they tapped instead
                                // of the studio hanging over the new page. Mirrors the BottomBar
                                // hook in Main.qml that closes the studio on stackView changes.
                                if (settingsMenu && settingsMenu.mainWindow
                                    && typeof settingsMenu.mainWindow.closeNowPlayingStudio === "function") {
                                    settingsMenu.mainWindow.closeNowPlayingStudio()
                                }

                                if (settingsMenu) {
                                    settingsMenu.currentSection = parent.itemSection
                                    if (settingsManager)
                                        settingsManager.set_last_settings_section(parent.itemSection)
                                }
                            }
                            hoverEnabled: true

                            onEntered: {
                                if (settingsMenu && settingsMenu.currentSection !== delegateRoot.itemSection) {
                                    navHoverRectangle.opacity = 1
                                }
                            }
                            onExited: {
                                navHoverRectangle.opacity = 0
                            }
                        }
                    }
                }
            }
        }

        Rectangle { // Right Content Area
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: App.Style.contentColor
            clip: true

            // ── Tile-mode state (for pages exposing a `tileModel`) ────────
            property bool useTileLayout: false
            property string detailCardId: ""
            property var detailTile: null
            // Rect of the clicked tile in contentArea coordinates — drives
            // the popup's zoom-from-tile transition.
            property rect originRect: Qt.rect(0, 0, 0, 0)

            function openTile(cardId, rect) {
                var tm = sidebarLayout.allSettingsMode ? sidebarLayout.allTiles
                    : (contentLoader.item && typeof contentLoader.item.tileModel !== "undefined"
                        ? contentLoader.item.tileModel : null)
                if (!tm)
                    return
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
                // Null detailTile too so the popup's Loader unloads and the
                // inner component's Component.onDestruction fires. Without
                // this, page-scoped resources (e.g. the Now Playing live PiP)
                // would persist after the user backs out of the tile. The
                // popup's body has already faded out via contentOpacity by
                // the time openProgress passes 0.45, so destroying the
                // content is not visible.
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

            Loader {
                id: contentLoader
                anchors {
                    fill: parent
                    margins: App.Spacing.settingsContentMargin
                }
                // Loaded with setSource() rather than a `source` binding so
                // renderCards is false before the page is built: this layout
                // hides the page behind its tile grid, and building the
                // page's stacked cards anyway cost 50+ ms per section switch.
                readonly property string sectionSource: settingsMenu ? sourceForSection(settingsMenu.currentSection) : ""
                onSectionSourceChanged: setSource(sectionSource, { renderCards: false })
                Component.onCompleted: setSource(sectionSource, { renderCards: false })

                // Track previous section for scroll save
                property string previousSection: ""

                onSourceChanged: {
                    // Save scroll position of previous page
                    if (previousSection && item && typeof item.contentY !== "undefined") {
                        ScrollMemory.positions[previousSection] = item.contentY
                    }
                    previousSection = settingsMenu ? settingsMenu.currentSection : ""

                    // Reset tile-mode state for the new page
                    contentArea.detailCardId = ""
                    contentArea.detailTile = null
                    contentArea.useTileLayout = false
                }

                onLoaded: {
                    // Bind optional properties if the page declares them
                    if (item && typeof item.mainWindow !== "undefined" && settingsMenu)
                        item.mainWindow = settingsMenu.mainWindow
                    if (item && typeof item.stackView !== "undefined" && settingsMenu)
                        item.stackView = settingsMenu.stackView
                    if (item && typeof item.currentSection !== "undefined" && settingsMenu)
                        item.currentSection = Qt.binding(function() { return settingsMenu ? settingsMenu.currentSection : "" })

                    // Detect tile-mode page (exposes a tileModel array)
                    var hasTiles = item && typeof item.tileModel !== "undefined"
                    contentArea.useTileLayout = hasTiles
                    if (hasTiles) {
                        // Hide the page's own Flickable rendering — the tile grid replaces it.
                        item.visible = false

                        // Reopen the tile the user was in before a rebuild. openTile()
                        // is a no-op if the cardId isn't on this page, so only clear
                        // once it actually lands.
                        if (settingsMenu && settingsMenu.initialTile !== "") {
                            contentArea.openTile(settingsMenu.initialTile, null)
                            if (contentArea.detailCardId === settingsMenu.initialTile)
                                settingsMenu.initialTile = ""
                        }
                    }

                    // Restore scroll position only for non-tile pages (use timer to wait for content to layout)
                    if (!hasTiles && settingsMenu && settingsMenu.currentSection !== "") {
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

            // ── Tile grid (replaces page rendering when page exposes tileModel) ─
            SettingsTilePage {
                id: tileGrid
                anchors {
                    fill: parent
                    margins: App.Spacing.settingsContentMargin
                }
                z: 2
                visible: contentArea.useTileLayout && !sidebarLayout.allSettingsMode
                tileModel: contentArea.useTileLayout && contentLoader.item
                    ? contentLoader.item.tileModel : []
                hiddenCardId: contentArea.detailCardId
                onTileSelected: function(cardId, rect) { sidebarLayout.selectTile(cardId, rect, tileGrid) }
            }

            // ── All-settings grid (settings button toggles it) ─────────────
            // Covers the section page. Columns: near-square tiles for the
            // pane's aspect ratio, preferring a count that fills the last row.
            Rectangle {
                anchors.fill: parent
                z: 2
                color: App.Style.contentColor
                visible: opacity > 0
                opacity: sidebarLayout.allSettingsMode ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }

                SettingsTilePage {
                    id: allTilesGrid
                    anchors {
                        fill: parent
                        margins: App.Spacing.settingsContentMargin
                    }
                    tileModel: sidebarLayout.allTiles
                    columns: {
                        var n = tileModel.length
                        if (n === 0 || height <= 0) return 4
                        var ideal = Math.max(2, Math.min(n, Math.round(Math.sqrt(n * width / height))))
                        var rowMin = dp(100) + App.Spacing.settingsHubGridSpacing
                        var best = ideal, bestEmpty = n
                        for (var c = Math.max(2, ideal - 1); c <= Math.min(n, ideal + 1); c++) {
                            var empty = Math.ceil(n / c) * c - n
                            if (empty < bestEmpty || (empty === bestEmpty && c === ideal)) {
                                best = c
                                bestEmpty = empty
                            }
                        }
                        // Add columns if the rows would not fit at the tiles'
                        // minimum height.
                        while (best < n && Math.ceil(n / best) * rowMin > height)
                            best++
                        return best
                    }
                    hiddenCardId: contentArea.detailCardId
                    onTileSelected: function(cardId, rect) { sidebarLayout.selectTile(cardId, rect, allTilesGrid) }
                }
            }

            // ── Detail popup (hero/morph: grows from the clicked tile's rect) ───
            SettingsCardPopup {
                id: detailPopup
                z: 3
                visible: (contentArea.useTileLayout || sidebarLayout.allSettingsMode) && (openProgress > 0.001 || contentArea.detailCardId !== "")
                title: contentArea.detailTile ? contentArea.detailTile.title : ""
                contentComponent: contentArea.detailTile ? contentArea.detailTile.component : null

                // 0.0 = collapsed onto the originating tile, 1.0 = filling
                // the content area. Drives geometry + content fade together.
                property real openProgress: contentArea.detailCardId === "" ? 0.0 : 1.0

                // Geometry interpolates between the tile rect and the full pane.
                x: contentArea.originRect.x * (1.0 - openProgress)
                y: contentArea.originRect.y * (1.0 - openProgress)
                width: contentArea.originRect.width
                    + (parent.width - contentArea.originRect.width) * openProgress
                height: contentArea.originRect.height
                    + (parent.height - contentArea.originRect.height) * openProgress

                // Header + body fade in during the second half of the morph,
                // so we see the card grow first and the page contents resolve
                // once there's enough room for them.
                contentOpacity: Math.max(0.0, (openProgress - 0.45) / 0.55)

                // Suppress animation on initial mount so the popup doesn't
                // visibly animate at startup.
                property bool _animEnabled: false
                Component.onCompleted: Qt.callLater(function() { _animEnabled = true })

                onBackRequested: contentArea.closeTile()

                Behavior on openProgress {
                    enabled: detailPopup._animEnabled
                    NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
                }
            }
        }
    }
}
