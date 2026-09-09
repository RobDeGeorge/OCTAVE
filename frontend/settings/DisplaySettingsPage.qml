import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import ".." as App

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // Each entry: { cardId, title, iconSource, component }. Other layouts ignore
    // this and use the Repeater rendering below.
    property var tileModel: [
        { cardId: "display_layout",     title: "Layout",     iconSource: App.Style.assetBase + "tile_layout.svg",     component: layoutContent },
        { cardId: "display_window",     title: "Window",     iconSource: App.Style.assetBase + "tile_window.svg",     component: windowContent },
        { cardId: "display_appearance", title: "Appearance", iconSource: App.Style.assetBase + "tile_appearance.svg", component: appearanceContent },
        { cardId: "display_clock",      title: "Clock",      iconSource: App.Style.assetBase + "tile_clock.svg",      component: clockContent }
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
                title: "UI Scaling"
                description: "Adjusts the overall size of UI elements. 100% = default."

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
                title: "Nav Bar Position"
                description: "Bottom or side placement"

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

            SettingCategory {
                title: "Nav Bar Media Controls"
                description: "Show play/pause and track skip buttons in the navigation bar."

                SettingsToggle {
                    id: bottomBarMediaControlsToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.showBottomBarMediaControls : true
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_show_bottom_bar_media_controls(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onShowBottomBarMediaControlsChanged() {
                            bottomBarMediaControlsToggle.checked = settingsManager.showBottomBarMediaControls
                        }
                    }
                }
            }

            SettingCategory {
                id: layoutCategory
                title: "Settings Layout"
                description: "Sidebar is the default. The other layouts are experimental."

                readonly property var experimentalLayouts: ["Carousel", "Hub", "Dashboard"]

                // Reveal the experimental layouts if the user is already on one,
                // or opts in via the toggle below. Derived from the active layout
                // so the picker is always consistent — no extra persisted setting.
                property bool showExperimental: settingsManager
                    ? experimentalLayouts.indexOf(settingsManager.settingsLayoutStyle) !== -1
                    : false

                SettingsChips {
                    Layout.fillWidth: true
                    options: layoutCategory.showExperimental
                             ? ["Sidebar", "Carousel", "Hub", "Dashboard"]
                             : ["Sidebar"]
                    currentValue: settingsManager ? settingsManager.settingsLayoutStyle : "Sidebar"
                    onSelected: function(value) {
                        if (settingsManager)
                            settingsManager.save_settings_layout_style(value)
                    }
                }

                SettingsToggle {
                    compact: true
                    Layout.alignment: Qt.AlignLeft
                    Layout.topMargin: App.Spacing.rowSpacing
                    text: "Show experimental layouts"
                    checked: layoutCategory.showExperimental
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        layoutCategory.showExperimental = checked
                        // Hiding them while one is active would strand the user on
                        // a now-invisible choice — fall back to the default.
                        if (!checked && settingsManager
                            && layoutCategory.experimentalLayouts.indexOf(settingsManager.settingsLayoutStyle) !== -1) {
                            settingsManager.save_settings_layout_style("Sidebar")
                        }
                    }
                }
            }
        }
    }

    Component {
        id: windowContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

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
                        validator: IntValidator {
                            bottom: 400
                            top: 3840
                        }

                        function applyWidth() {
                            if (text && settingsManager) {
                                const width = parseInt(text)
                                settingsManager.save_screen_width(width)
                                pageRoot.mainWindow.width = width
                                App.Spacing.updateDimensions(width, pageRoot.mainWindow.height)
                            }
                        }

                        onEditingFinished: applyWidth()
                        onActiveFocusChanged: if (!activeFocus) applyWidth()

                        Connections {
                            target: pageRoot.mainWindow
                            function onWidthChanged() {
                                if (!screenWidth.activeFocus) {
                                    screenWidth.text = pageRoot.mainWindow.width
                                    if (settingsManager) {
                                        settingsManager.save_screen_width(pageRoot.mainWindow.width)
                                    }
                                    App.Spacing.updateDimensions(pageRoot.mainWindow.width, pageRoot.mainWindow.height)
                                }
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
                        validator: IntValidator {
                            bottom: 300
                            top: 2160
                        }

                        function applyHeight() {
                            if (text && settingsManager) {
                                const height = parseInt(text)
                                settingsManager.save_screen_height(height)
                                pageRoot.mainWindow.height = height
                                App.Spacing.updateDimensions(pageRoot.mainWindow.width, height)
                            }
                        }

                        onEditingFinished: applyHeight()
                        onActiveFocusChanged: if (!activeFocus) applyHeight()

                        Connections {
                            target: pageRoot.mainWindow
                            function onHeightChanged() {
                                if (!screenHeight.activeFocus) {
                                    screenHeight.text = pageRoot.mainWindow.height
                                    if (settingsManager) {
                                        settingsManager.save_screen_height(pageRoot.mainWindow.height)
                                    }
                                    App.Spacing.updateDimensions(pageRoot.mainWindow.width, pageRoot.mainWindow.height)
                                }
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
                    description: "Pull a live color palette from the current song's album art."
                    togglable: true
                    checked: settingsManager && settingsManager.themeSetting === "Album Art Capture"

                    onToggled: function(on) {
                        if (!settingsManager) return
                        if (on) {
                            if (settingsManager.themeSetting !== "Album Art Capture")
                                App.Style.lastStaticTheme = settingsManager.themeSetting
                            settingsManager.save_theme_setting("Album Art Capture")
                            App.Style.setTheme("Album Art Capture")
                            if (typeof mediaManager !== "undefined" && mediaManager) {
                                var f = mediaManager.get_current_file()
                                if (f) mediaManager.extract_colors_from_album_art(f)
                            }
                        } else {
                            var fallback = App.Style.lastStaticTheme || "CosmicVoyager"
                            settingsManager.save_theme_setting(fallback)
                            App.Style.setTheme(fallback)
                        }
                    }

                    SettingLabel {
                        text: "Transition Speed"
                    }

                    SettingDescription {
                        text: "How long album art colors fade when the song changes. 0 = instant."
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

                    SettingLabel {
                        text: "Match Song Length"
                        Layout.topMargin: App.Spacing.rowSpacing * 1.5
                    }

                    SettingDescription {
                        text: "Colors transition over the full length of the current song."
                    }

                    SettingsToggle {
                        id: songLengthTransitionToggle
                        compact: true
                        Layout.fillWidth: true
                        text: checked ? "On" : "Off"
                        checked: settingsManager ? settingsManager.songLengthTransition : false
                        activeColor: App.Style.accent
                        inactiveColor: App.Style.hoverColor

                        onToggled: function(checked) {
                            if (settingsManager) {
                                settingsManager.save_song_length_transition(checked)
                            }
                        }

                        Connections {
                            target: settingsManager
                            function onSongLengthTransitionChanged() {
                                songLengthTransitionToggle.checked = settingsManager.songLengthTransition
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

    Component {
        id: clockContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Show Clock"
                description: "Display the current time on screen."

                SettingsToggle {
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.showClock : true

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_show_clock(checked)
                        }
                    }
                }
            }

            SettingCategory {
                title: "Show Seconds"
                description: "Append seconds to the clock display."

                SettingsToggle {
                    id: clockShowSecondsToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.clockShowSeconds : false

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_clock_show_seconds(checked)
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onClockShowSecondsChanged() {
                            clockShowSecondsToggle.checked = settingsManager.clockShowSeconds
                        }
                    }
                }
            }

            SettingCategory {
                title: "Time Format"

                SettingsSegmentedControl {
                    id: timeFormatControl
                    Layout.fillWidth: true
                    options: ["24-hour", "12-hour (AM/PM)"]
                    currentValue: settingsManager ?
                                (settingsManager.clockFormat24Hour ? "24-hour" : "12-hour (AM/PM)") :
                                "24-hour"

                    onSelected: function(value) {
                        if (settingsManager) {
                            settingsManager.save_clock_format(value === "24-hour")
                        }
                    }
                }
            }

            SettingCategory {
                title: "Clock Size"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

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

    // ── Default rendering: stacked SettingsCards (Carousel/Hub/Dashboard layouts).
    //    Sidebar layout sets pageRoot.visible = false and renders the tile grid + popup instead.
    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: App.Spacing.sectionSpacing

        Repeater {
            model: pageRoot.tileModel

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
