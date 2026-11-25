import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import ".." as App
import "../home/HomeScreens.js" as HomeScreens

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // Each entry: { cardId, title, summary, component }. `summary()` returns
    // the tile's one line of state — { state, active, status }, all optional.
    // Keep state short (it elides past ~14 characters in the all-settings
    // grid), set active only for things that are on / connected, and leave
    // state out rather than fill it with an instruction — and is re-evaluated whenever a property it
    // reads changes. Other layouts ignore this and use the Repeater below.
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    function _cap(s) { s = String(s || ""); return s.charAt(0).toUpperCase() + s.slice(1) }

    // Drives the Clock tile's time.
    property date _now: new Date()
    // Only while Settings is the page on screen: this page lives on, hidden,
    // in the page cache, and the timer ticked for the life of the app
    Timer {
        interval: 1000
        repeat: true
        running: !pageRoot.stackView
                 || (pageRoot.stackView.currentItem && pageRoot.stackView.currentItem.objectName === "settingsMenu")
        onRunningChanged: if (running) pageRoot._now = new Date()
        onTriggered: pageRoot._now = new Date()
    }

    property var tileModel: [
        { cardId: "display_layout", title: "Layout", component: layoutContent,
          summary: function() {
              if (!settingsManager) return {}
              var win = settingsManager.windowState === "windowed"
                      ? settingsManager.screenWidth + "×" + settingsManager.screenHeight
                      : pageRoot._cap(settingsManager.windowState)
              return { state: win }
          } },
        { cardId: "display_appearance", title: "Appearance", component: appearanceContent,
          summary: function() {
              if (!settingsManager) return {}
              var theme = settingsManager.themeSetting
              return { state: theme === "Album Art Capture" ? "Album art" : theme }
          } },
        { cardId: "display_units", title: "Units", component: unitsContent,
          summary: function() {
              if (!settingsManager) return {}
              return { state: "\u00B0" + settingsManager.gaugeTempUnit + " \u00B7 " + settingsManager.gaugeSpeedUnit }
          } },
        { cardId: "display_clock", title: "Clock", component: clockContent,
          summary: function() {
              if (!settingsManager) return {}
              if (!settingsManager.showClock) return { state: "Hidden" }
              var use24h = settingsManager.clockFormat24Hour
              var fmt = (use24h ? "HH" : "h") + ":mm" + (settingsManager.clockShowSeconds ? ":ss" : "") + (use24h ? "" : " AP")
              return { state: Qt.formatTime(pageRoot._now, fmt) }
          } }
    ]

    function buildThemeChipColors() {
        var colors = {}
        var customNames = settingsManager ? settingsManager.customThemes : []
        for (var i = 0; i < customNames.length; i++) {
            var theme = App.Style.customThemes[customNames[i]]
            if (theme && theme.accent) {
                colors[customNames[i]] = theme.accent
            }
        }
        return colors
    }

    function buildThemeChipImages() {
        var images = {}
        var customNames = settingsManager ? settingsManager.customThemes : []
        for (var i = 0; i < customNames.length; i++) {
            var theme = App.Style.customThemes[customNames[i]]
            if (theme && theme.albumArt) {
                images[customNames[i]] = theme.albumArt
            }
        }
        return images
    }

    function buildFontChipFonts() {
        var fonts = {}
        var available = App.Style.availableFonts || []
        for (var i = 0; i < available.length; i++) {
            var name = available[i]
            if (name === "System Default") {
                fonts[name] = App.Style.systemDefaultFont || ""
            } else {
                fonts[name] = (App.Style.fontFamilyMap && App.Style.fontFamilyMap[name]) || ""
            }
        }
        return fonts
    }

    function buildThemeChipPalettes() {
        var palettes = {}
        var names = App.Style.getAllThemeNames()
        for (var i = 0; i < names.length; i++) {
            var name = names[i]
            // Album-art themes already render as image chips — skip palette here.
            if (App.Style.customThemes[name] && App.Style.customThemes[name].albumArt)
                continue
            var theme = App.Style.themes[name] || App.Style.customThemes[name]
            if (theme && theme.base && theme.baseAlt && theme.accent) {
                palettes[name] = [theme.accent, theme.baseAlt, theme.base]
            }
        }
        return palettes
    }

    contentWidth: width
    contentHeight: settingsContent.implicitHeight
    flickableDirection: Flickable.VerticalFlick
    clip: true
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1200
    maximumFlickVelocity: 4000
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

    // ── Card body components — shared between the Flickable rendering (below)
    //    and the SettingsCardPopup in the Sidebar layout.

    Component {
        id: layoutContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "UI Scale"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: uiScaleSlider
                        from: 0.5
                        to: 2.0
                        stepSize: 0.05
                        value: App.Spacing.globalScale
                        Layout.fillWidth: true

                        // Commit on release only. Updating globalScale mid-drag
                        // re-lays out the slider itself (track + handle scale
                        // with globalScale), so the handle jumps relative to
                        // the finger. Deferring to release keeps the geometry
                        // steady while dragging; the value display still
                        // reflects the live value as feedback.
                        onPressedChanged: {
                            if (!pressed && settingsManager) {
                                settingsManager.save_ui_scale(value)
                                App.Spacing.globalScale = value
                            }
                        }
                    }

                    ValueDisplay {
                        text: (uiScaleSlider.value * 100).toFixed(0) + "%"
                        Layout.fillWidth: false
                    }
                }
            }

            SettingCategory {
                title: "Screen Dimensions"

                RowLayout {
                    spacing: App.Spacing.overallSpacing
                    Layout.fillWidth: true

                    Text {
                        text: "Width:"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                    }

                    SettingsTextField {
                        id: screenWidth
                        Layout.preferredWidth: pageRoot.dp(120)
                        text: pageRoot.mainWindow ? pageRoot.mainWindow.width : ""
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {   // SettingsManager::kMin/kMaxScreenWidth
                            bottom: 320
                            top: 7680
                        }

                        function applyWidth() {
                            if (text && settingsManager) {
                                // Main.qml saves the new size from the window
                                pageRoot.mainWindow.width = parseInt(text)
                            }
                        }

                        onEditingFinished: applyWidth()
                        onActiveFocusChanged: if (!activeFocus) applyWidth()

                        Connections {
                            target: pageRoot.mainWindow
                            function onWidthChanged() {
                                if (!screenWidth.activeFocus)
                                    screenWidth.text = pageRoot.mainWindow.width
                            }
                        }
                    }

                    Text {
                        text: "Height:"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                    }

                    SettingsTextField {
                        id: screenHeight
                        Layout.preferredWidth: pageRoot.dp(120)
                        text: pageRoot.mainWindow ? pageRoot.mainWindow.height : ""
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {   // SettingsManager::kMin/kMaxScreenHeight
                            bottom: 300
                            top: 4320
                        }

                        function applyHeight() {
                            if (text && settingsManager) {
                                // Main.qml saves the new size from the window
                                pageRoot.mainWindow.height = parseInt(text)
                            }
                        }

                        onEditingFinished: applyHeight()
                        onActiveFocusChanged: if (!activeFocus) applyHeight()

                        Connections {
                            target: pageRoot.mainWindow
                            function onHeightChanged() {
                                if (!screenHeight.activeFocus)
                                    screenHeight.text = pageRoot.mainWindow.height
                            }
                        }
                    }

                    SettingsButton {
                        text: pageRoot.mainWindow && pageRoot.mainWindow.visibility === Window.FullScreen ? "Exit Fullscreen" : "Fullscreen"
                        Layout.preferredHeight: screenHeight.height
                        Layout.minimumWidth: pageRoot.dp(80)
                        tooltipText: pageRoot.mainWindow && pageRoot.mainWindow.visibility === Window.FullScreen ? "Exit fullscreen mode" : "Enter fullscreen mode"
                        onClicked: {
                            if (!pageRoot.mainWindow) return
                            if (pageRoot.mainWindow.visibility === Window.FullScreen) {
                                pageRoot.mainWindow.visibility = Window.Windowed
                                if (settingsManager) settingsManager.save_window_state("windowed")
                            } else {
                                pageRoot.mainWindow.visibility = Window.FullScreen
                                if (settingsManager) settingsManager.save_window_state("fullscreen")
                            }
                        }
                    }

                    SettingsButton {
                        text: "Maximize"
                        Layout.preferredHeight: screenHeight.height
                        Layout.minimumWidth: pageRoot.dp(80)
                        tooltipText: "Maximize window to fill screen"
                        onClicked: {
                            if (!pageRoot.mainWindow) return
                            pageRoot.mainWindow.visibility = Window.Maximized
                            if (settingsManager) settingsManager.save_window_state("maximized")
                        }
                    }

                    Item { Layout.fillWidth: true } // Spacer
                }
            }

            SettingCategory {
                title: "Nav Bar Position"

                SettingsSegmentedControl {
                    id: bottomBarOrientation
                    Layout.fillWidth: true
                    currentValue: settingsManager ? settingsManager.bottomBarOrientation : "bottom"
                    options: ["bottom", "side"]

                    onSelected: function(value) {
                        if (settingsManager)
                            settingsManager.save_bottom_bar_orientation(value)
                    }
                }
            }

            // What Home is: a card for each hand-built screen (frontend/home/,
            // the classic panels among them) and each dashboard, with a
            // picture of it; a tap makes it Home
            SettingCategory {
                id: homeScreenCategory
                title: "Home Screen"
                description: "Pick what Home shows."
                readonly property var dm: (typeof dashboardManager !== "undefined" && dashboardManager) ? dashboardManager : null
                // The homeScreen setting ("" = a dashboard) and the old classic switch
                property string screen: settingsManager ? settingsManager.get_setting_with_default("homeScreen", "") : ""
                property string legacyStyle: settingsManager ? settingsManager.get_setting_with_default("homeStyle", "dashboard") : "dashboard"
                readonly property string current: screen !== "" ? "screen:" + screen
                                                  : legacyStyle === "classic" ? "screen:classic"
                                                  : "dashboard:" + (dm ? dm.homeDashboardId : "")
                // [{key, label, preview, dashboardId}]
                readonly property var choices: {
                    var out = []
                    for (var s = 0; s < HomeScreens.list.length; s++) {
                        var h = HomeScreens.list[s]
                        out.push({ "key": "screen:" + h.id, "label": h.label, "preview": h.preview, "dashboardId": "" })
                    }
                    var list = dm ? dm.dashboards : []
                    for (var i = 0; i < list.length; i++)
                        out.push({ "key": "dashboard:" + list[i].id, "label": list[i].label,
                                   "preview": HomeScreens.dashboardPreviews[list[i].id] || "", "dashboardId": list[i].id })
                    return out
                }
                function pick(choice) {
                    var id = choice.dashboardId !== "" ? "" : choice.key.substring(7)
                    screen = id
                    legacyStyle = "dashboard"
                    if (settingsManager) {
                        settingsManager.save_setting("homeScreen", id)
                        settingsManager.save_setting("homeStyle", "dashboard")
                    }
                    if (choice.dashboardId !== "" && dm) dm.setHomeDashboard(choice.dashboardId)
                }

                Flow {
                    id: homeCards
                    objectName: "homeScreenCards"
                    Layout.fillWidth: true
                    spacing: pageRoot.dp(12)
                    readonly property int columns: width >= pageRoot.dp(560) ? 3 : width >= pageRoot.dp(300) ? 2 : 1
                    readonly property real cardWidth: Math.floor((width - (columns - 1) * spacing) / columns)

                    Repeater {
                        model: homeScreenCategory.choices
                        Rectangle {
                            id: homeCard
                            required property var modelData
                            objectName: "homeCard_" + modelData.key.replace(":", "_")
                            readonly property bool chosen: homeScreenCategory.current === modelData.key
                            width: homeCards.cardWidth
                            height: picture.height + caption.height + pageRoot.dp(6)
                            radius: pageRoot.dp(12)
                            color: Qt.darker(App.Style.obdBoxBackground, 1.15)
                            border.width: chosen ? pageRoot.dp(2) : 1
                            border.color: chosen ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.8)
                            scale: cardMouse.pressed ? 0.97 : 1.0
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                            // The picture: a shot of the screen, or the dashboard's cells
                            Item {
                                id: picture
                                x: pageRoot.dp(6); y: pageRoot.dp(6)
                                width: parent.width - pageRoot.dp(12)
                                height: Math.round(width * 31 / 64)
                                clip: true
                                Rectangle { anchors.fill: parent; color: App.Style.backgroundColor }
                                Image {
                                    anchors.fill: parent
                                    visible: homeCard.modelData.preview !== ""
                                    source: homeCard.modelData.preview !== "" ? Qt.resolvedUrl("../" + homeCard.modelData.preview) : ""
                                    sourceSize: Qt.size(640, 310)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                // A dashboard with no picture: where its widgets stand
                                Item {
                                    id: plan
                                    anchors.fill: parent
                                    anchors.margins: pageRoot.dp(4)
                                    visible: homeCard.modelData.preview === "" && homeCard.modelData.dashboardId !== ""
                                    readonly property var spec: visible && homeScreenCategory.dm
                                                                ? homeScreenCategory.dm.loadDashboard(homeCard.modelData.dashboardId) : null
                                    readonly property int cols: spec && spec.gridColumns ? spec.gridColumns : 12
                                    readonly property int rows: spec && spec.gridRows ? spec.gridRows : 6
                                    Repeater {
                                        model: plan.spec && plan.spec.cells ? plan.spec.cells : []
                                        Rectangle {
                                            required property var modelData
                                            x: (modelData.col || 0) * plan.width / plan.cols + 1
                                            y: (modelData.row || 0) * plan.height / plan.rows + 1
                                            width: (modelData.colSpan || 1) * plan.width / plan.cols - 2
                                            height: (modelData.rowSpan || 1) * plan.height / plan.rows - 2
                                            radius: pageRoot.dp(3)
                                            color: Qt.rgba(App.Style.obdBarColor.r, App.Style.obdBarColor.g, App.Style.obdBarColor.b, 0.22)
                                            border.width: 1
                                            border.color: Qt.rgba(App.Style.obdBarColor.r, App.Style.obdBarColor.g, App.Style.obdBarColor.b, 0.6)
                                        }
                                    }
                                }
                            }
                            Item {
                                id: caption
                                anchors.top: picture.bottom
                                x: pageRoot.dp(12)
                                width: parent.width - pageRoot.dp(24)
                                height: pageRoot.dp(40)
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - (tick.visible ? tick.width + pageRoot.dp(8) : 0)
                                    elide: Text.ElideRight
                                    text: homeCard.modelData.label
                                    color: homeCard.chosen ? App.Style.accent : App.Style.primaryTextColor
                                    font.family: App.Style.fontFamily
                                    font.pixelSize: App.Spacing.overallText
                                    font.bold: homeCard.chosen
                                }
                                // The one in use
                                Rectangle {
                                    id: tick
                                    visible: homeCard.chosen
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: pageRoot.dp(12); height: width; radius: width / 2
                                    color: App.Style.accent
                                }
                            }
                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: homeScreenCategory.pick(homeCard.modelData)
                            }
                        }
                    }
                }
            }

            SettingCategory {
                title: "Nav Bar Media Controls"

                inlineContent: SettingsToggle {
                    id: bottomBarMediaControlsToggle
                    compact: true
                    checked: settingsManager ? settingsManager.showBottomBarMediaControls : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_show_bottom_bar_media_controls(checked)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: appearanceContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                id: themeCategory
                title: "Theme"

                // Currently selected theme name, accounting for the Album Art
                // Capture override which "borrows" a static theme behind it.
                property string activeThemeName: {
                    if (!settingsManager) return "SolarizedLight"
                    if (settingsManager.themeSetting === "Album Art Capture")
                        return App.Style.lastStaticTheme || "CosmicVoyager"
                    return settingsManager.themeSetting
                }

                function pickTheme(value) {
                    if (settingsManager) {
                        // Picking a chip turns Album Art Capture off — the
                        // user is choosing a static theme.
                        settingsManager.save_theme_setting(value)
                        App.Style.setTheme(value)
                    }
                }

                Connections {
                    target: App.Style
                    function onCustomThemesUpdated() {
                        var palettes = pageRoot.buildThemeChipPalettes()
                        var images = pageRoot.buildThemeChipImages()
                        var deletable = settingsManager ? settingsManager.customThemes : []
                        customThemeChips.options = settingsManager ? settingsManager.customThemes : []
                        customThemeChips.chipPalettes = palettes
                        customThemeChips.chipImages = images
                        customThemeChips.deletableItems = deletable
                        lightThemeChips.chipPalettes = palettes
                        darkThemeChips.chipPalettes = palettes
                    }
                }

                // ── Light themes ────────────────────────────────────────
                SettingLabel {
                    text: "Light"
                    Layout.topMargin: App.Spacing.rowSpacing * 0.5
                }

                SettingsChips {
                    id: lightThemeChips
                    Layout.fillWidth: true
                    currentValue: themeCategory.activeThemeName
                    options: App.Style.getLightThemeNames()
                    chipPalettes: pageRoot.buildThemeChipPalettes()

                    onSelected: function(value) { themeCategory.pickTheme(value) }
                }

                // ── Dark themes ─────────────────────────────────────────
                SettingLabel {
                    text: "Dark"
                    Layout.topMargin: App.Spacing.rowSpacing * 1.5
                }

                SettingsChips {
                    id: darkThemeChips
                    Layout.fillWidth: true
                    currentValue: themeCategory.activeThemeName
                    options: App.Style.getDarkThemeNames()
                    chipPalettes: pageRoot.buildThemeChipPalettes()

                    onSelected: function(value) { themeCategory.pickTheme(value) }
                }

                // ── Album Art themes (only visible when the user has any) ──
                SettingLabel {
                    text: "Album Art Theme"
                    Layout.topMargin: App.Spacing.rowSpacing * 1.5
                    visible: customThemeChips.options.length > 0
                }

                SettingsChips {
                    id: customThemeChips
                    Layout.fillWidth: true
                    visible: options.length > 0
                    currentValue: themeCategory.activeThemeName
                    options: settingsManager ? settingsManager.customThemes : []
                    chipPalettes: pageRoot.buildThemeChipPalettes()
                    chipImages: pageRoot.buildThemeChipImages()
                    deletableItems: settingsManager ? settingsManager.customThemes : []

                    onSelected: function(value) { themeCategory.pickTheme(value) }

                    onItemDeleted: function(value) {
                        if (settingsManager) {
                            if (settingsManager.themeSetting === value) {
                                settingsManager.save_theme_setting("CosmicVoyager")
                                App.Style.setTheme("CosmicVoyager")
                            }
                            settingsManager.delete_custom_theme(value)
                        }
                    }
                }

                SettingSubcategory {
                    id: albumArtCard
                    Layout.fillWidth: true
                    Layout.topMargin: App.Spacing.rowSpacing
                    title: "Album Art Capture"
                    description: "Colors from the current song's album art."
                    togglable: true
                    checked: settingsManager && settingsManager.themeSetting === "Album Art Capture"

                    onToggled: function(on) {
                        if (!settingsManager) return
                        if (on) {
                            if (settingsManager.themeSetting !== "Album Art Capture")
                                App.Style.lastStaticTheme = settingsManager.themeSetting
                            settingsManager.save_theme_setting("Album Art Capture")
                            App.Style.setTheme("Album Art Capture")
                            // nowPlaying (C++) extracts from the live source's art
                        } else {
                            var fallback = App.Style.lastStaticTheme || "CosmicVoyager"
                            settingsManager.save_theme_setting(fallback)
                            App.Style.setTheme(fallback)
                        }
                    }

                    SettingLabel {
                        text: "Fade Time"
                    }

                    SettingsSlider {
                        id: colorTransitionSlider
                        from: 0
                        to: 2000
                        stepSize: 50
                        value: settingsManager ? settingsManager.colorTransitionMs : 1000
                        Layout.preferredHeight: App.Spacing.overallSliderHeight * 1.6

                        Connections {
                            target: settingsManager
                            function onColorTransitionMsChanged() {
                                colorTransitionSlider.value = settingsManager.colorTransitionMs
                            }
                        }

                        Timer {
                            id: colorTransitionUpdateTimer
                            interval: 100
                            running: false
                            repeat: false
                            onTriggered: {
                                if (settingsManager) {
                                    settingsManager.save_color_transition_ms(Math.round(colorTransitionSlider.value))
                                    App.Style.colorTransitionMs = Math.round(colorTransitionSlider.value)
                                }
                            }
                        }

                        onMoved: colorTransitionUpdateTimer.restart()
                    }

                    ValueDisplay {
                        text: Math.round(colorTransitionSlider.value) + "ms"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: App.Spacing.rowSpacing * 1.5
                        spacing: App.Spacing.overallSpacing

                        SettingLabel {
                            text: "Fade Over the Whole Song"
                        }

                        SettingsToggle {
                            id: songLengthTransitionToggle
                            compact: true
                            Layout.fillWidth: false
                            checked: settingsManager ? settingsManager.songLengthTransition : false
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor

                            onToggled: function(checked) {
                                if (settingsManager) {
                                    settingsManager.save_song_length_transition(checked)
                                }
                            }
                        }
                    }
                }
            }

            SettingCategory {
                title: "Font"
                description: "Drop .ttf/.otf files in the fonts folder"

                Connections {
                    target: App.Style
                    function onFontsUpdated() {
                        fontButton.options = App.Style.availableFonts
                        fontButton.chipFonts = pageRoot.buildFontChipFonts()
                    }
                }

                SettingsChips {
                    id: fontButton
                    Layout.fillWidth: true
                    currentValue: settingsManager ? settingsManager.fontSetting : "System Default"
                    options: App.Style.availableFonts
                    chipFonts: pageRoot.buildFontChipFonts()

                    onSelected: function(value) {
                        if (settingsManager) {
                            settingsManager.save_font_setting(value)
                            App.Style.setFont(value)
                        }
                    }
                }
            }

        }
    }

    // Units for the whole app. Temperature is one choice: it writes both
    // the gauges' setting and the Sensors page's, which used to be apart.
    Component {
        id: unitsContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Temperature"

                SettingsSegmentedControl {
                    objectName: "tempUnitControl"
                    Layout.fillWidth: true
                    options: ["\u00B0C", "\u00B0F"]
                    currentValue: settingsManager && settingsManager.gaugeTempUnit === "F" ? "\u00B0F" : "\u00B0C"
                    onSelected: function(value) {
                        if (!settingsManager) return
                        var unit = value === "\u00B0F" ? "F" : "C"
                        settingsManager.save_gauge_temp_unit(unit)
                        settingsManager.save_sensor_temp_unit(unit)
                    }
                }
            }

            SettingCategory {
                title: "Speed"

                SettingsSegmentedControl {
                    objectName: "speedUnitControl"
                    Layout.fillWidth: true
                    options: ["mph", "km/h"]
                    currentValue: settingsManager && settingsManager.gaugeSpeedUnit === "km/h" ? "km/h" : "mph"
                    onSelected: function(value) {
                        if (settingsManager) settingsManager.save_gauge_speed_unit(value)
                    }
                }
            }

            SettingCategory {
                title: "Altitude"

                SettingsSegmentedControl {
                    Layout.fillWidth: true
                    options: ["m", "ft"]
                    currentValue: settingsManager && settingsManager.sensorAltitudeUnit === "ft" ? "ft" : "m"
                    onSelected: function(value) {
                        if (settingsManager) settingsManager.save_sensor_altitude_unit(value)
                    }
                }
            }

            SettingDescription {
                text: "Trip History keeps the units each drive was recorded in."
            }
        }
    }

    Component {
        id: clockContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Show Clock"

                inlineContent: SettingsToggle {
                    compact: true
                    checked: settingsManager ? settingsManager.showClock : true

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_show_clock(checked)
                        }
                    }
                }
            }

            SettingCategory {
                // The rest only matter while the clock shows
                visible: settingsManager ? settingsManager.showClock : true
                title: "Show Seconds"

                inlineContent: SettingsToggle {
                    id: clockShowSecondsToggle
                    compact: true
                    checked: settingsManager ? settingsManager.clockShowSeconds : false

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_clock_show_seconds(checked)
                        }
                    }
                }
            }

            SettingCategory {
                visible: settingsManager ? settingsManager.showClock : true
                title: "Time Format"

                SettingsSegmentedControl {
                    id: timeFormatControl
                    Layout.fillWidth: true
                    options: ["24-hour", "12-hour"]
                    currentValue: settingsManager ?
                                (settingsManager.clockFormat24Hour ? "24-hour" : "12-hour") :
                                "24-hour"

                    onSelected: function(value) {
                        if (settingsManager) {
                            settingsManager.save_clock_format(value === "24-hour")
                        }
                    }
                }
            }

            SettingCategory {
                visible: settingsManager ? settingsManager.showClock : true
                title: "Clock Size"

                inlineContent: SettingsToggle {
                    id: clockSizeAutoToggle
                    compact: true
                    text: "Auto"
                    checked: settingsManager ? settingsManager.clockSizeAuto : true

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_clock_size_auto(checked)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: App.Spacing.rowSpacing
                    spacing: App.Spacing.overallSpacing
                    enabled: !clockSizeAutoToggle.checked
                    opacity: enabled ? 1.0 : 0.4

                    Behavior on opacity { NumberAnimation { duration: 150 } }

                    SettingsSlider {
                        id: clockSizeSlider
                        from: 10
                        to: 85
                        stepSize: 1
                        value: settingsManager ? settingsManager.clockSize : 18
                        Layout.fillWidth: true

                        onMoved: {
                            if (settingsManager) {
                                settingsManager.save_clock_size(value)
                            }
                        }
                    }

                    ValueDisplay {
                        text: clockSizeSlider.value.toFixed(0) + " px"
                        Layout.fillWidth: false
                    }
                }
            }
        }
    }

    // ── Stacked SettingsCards, for a host that leaves renderCards on (none does now).
    //    Sidebar layout sets pageRoot.visible = false and renders the tile grid + popup instead.
    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: App.Spacing.sectionSpacing

        Repeater {
            model: pageRoot.renderCards ? pageRoot.tileModel : []

            SettingsCard {
                Layout.fillWidth: true
                objectName: modelData.title
                cardId: modelData.cardId
                title: modelData.title

                Loader {
                    Layout.fillWidth: true
                    sourceComponent: modelData.component
                }
            }
        }

        // Bottom spacer
        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
