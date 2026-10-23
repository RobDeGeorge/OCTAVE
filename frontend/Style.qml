// Style.qml
pragma Singleton
import QtQuick 2.15

QtObject {
    // Base URL for frontend/assets/. Resolved here, in frontend/, so callers in
    // any subdirectory can build an icon path without guessing how many "../"
    // segments they need — settings/ pages and frontend/ layouts both consume
    // the tile icons. Deliberately a property rather than a function: singleton
    // *functions* are unreliable on Android (same Qt bug the local dp() wrappers
    // scattered through the QML work around).
    readonly property url assetBase: Qt.resolvedUrl("assets/")

    property string currentTheme: "SolarizedLight"
    property string currentFont: "System Default"

    // Available fonts (populated from Main.qml after scanning fonts folder)
    property var availableFonts: ["System Default"]

    // Map of font display names to their loaded font family names
    property var fontFamilyMap: ({})

    // System default font family (set by Main.qml at startup)
    property string systemDefaultFont: ""

    // The actual font family to use (resolved from currentFont)
    // Always returns a valid font family name - systemDefaultFont for System Default, custom font family otherwise
    property string fontFamily: currentFont === "System Default" ? systemDefaultFont : (fontFamilyMap[currentFont] || systemDefaultFont)

    // Family of the bundled icon-glyph font (assets/fonts/symbols/, set by
    // Main.qml once loaded). Every Text that draws a symbol (✓ ✗ ⌨ ⌫ ⚠ ♪ …)
    // uses symbolFont: Qt on Android has no working symbol-font fallback, so
    // those glyphs rendered as boxes whenever the UI font lacked them.
    property string symbolFontFamily: ""
    readonly property string symbolFont: symbolFontFamily !== "" ? symbolFontFamily : fontFamily

    // Signal when fonts list is updated
    signal fontsUpdated()

    // Signal when font changes (for real-time updates)
    signal fontChanged()

    // Function to set the current font
    function setFont(fontName) {
        if (availableFonts.indexOf(fontName) !== -1 || fontName === "System Default") {
            currentFont = fontName
            fontChanged()
        }
    }

    // Function to register available fonts (called from Main.qml)
    function registerFonts(fontList, familyMap) {
        availableFonts = ["System Default"].concat(fontList)
        fontFamilyMap = familyMap
        fontsUpdated()
    }

    // This will store our custom themes that are loaded from settings
    property var customThemes: ({})

    // Dynamic album art theme - updated in real-time based on current song
    property var albumArtTheme: null
    property bool isAlbumArtCaptureActive: currentTheme === "Album Art Capture"

    // The built-in palettes: data in frontend/themes.json, served by the C++
    // ThemeCatalog (themeCatalog), which every other frontend reads too
    readonly property var themes: (typeof themeCatalog !== "undefined" && themeCatalog) ? themeCatalog.themes : ({})

    // Reactive current theme object - this is what all color bindings should use
    // It updates whenever currentTheme changes OR when album art colors are extracted
    property var activeTheme: {
        // If Album Art Capture is active and we have dynamic colors, use them
        if (currentTheme === "Album Art Capture" && albumArtTheme) {
            return albumArtTheme
        }
        // Check if theme is a custom theme first
        if (customThemes[currentTheme]) {
            return customThemes[currentTheme]
        }
        // Fall back to built-in themes
        return themes[currentTheme] || themes["SolarizedLight"]
    }

    // Helper function for backward compatibility
    function getCurrentTheme() {
        return activeTheme
    }

    // Function to update the Album Art Capture theme with new colors
    // (settingsManager.albumArtColors, a theme object)
    function updateAlbumArtTheme(newTheme) {
        console.log("[AlbumArtCapture QML] updateAlbumArtTheme called")
        try {
            console.log("[AlbumArtCapture QML] Parsed theme, base color:", newTheme.base, "accent:", newTheme.accent)

            // Store the new theme - this should trigger activeTheme to re-evaluate
            albumArtTheme = newTheme

            // If Album Art Capture is currently active, trigger a visual refresh
            if (currentTheme === "Album Art Capture") {
                console.log("[AlbumArtCapture QML] Theme is active, forcing refresh...")

                // First paint after entering Album Art mode is instant — only
                // subsequent song changes fade with the configured speed.
                var instantFirst = _firstAlbumArtUpdate
                var savedDuration = colorTransitionMs
                if (instantFirst) colorTransitionMs = 0

                // Force activeTheme binding to re-evaluate by toggling currentTheme
                let savedTheme = currentTheme
                currentTheme = ""
                currentTheme = savedTheme

                if (instantFirst) {
                    _firstAlbumArtUpdate = false
                    Qt.callLater(function() { colorTransitionMs = savedDuration })
                }

                // Emit signal for any listeners
                albumArtThemeUpdated()

                console.log("[AlbumArtCapture QML] UI refresh complete")
            }
        } catch (e) {
            console.log("[AlbumArtCapture QML] Error applying album art theme:", e)
        }
    }

    // Signal emitted when album art theme colors are updated
    signal albumArtThemeUpdated()
    
    // Last non–Album-Art theme the user had selected — used by the Album Art
    // Capture toggle so that turning it off restores the prior choice.
    property string lastStaticTheme: ""

    // Function to get all theme names (built-in + custom).
    // "Album Art Capture" is intentionally excluded — it's surfaced as a
    // toggle under the chips, not as a selectable theme.
    function getAllThemeNames() {
        let builtIn = (typeof themeCatalog !== "undefined" && themeCatalog) ? themeCatalog.themeNames : Object.keys(themes)
        let allNames = builtIn.filter(function(n) { return n !== "Album Art Capture" })
        allNames = allNames.concat(Object.keys(customThemes))
        return allNames
    }

    // Curated ordering for the theme picker — built-in themes split into
    // light (light base) and dark (dark base) groups. The ordering within
    // each group is hand-tuned so the swatch palette flows nicely.
    readonly property var lightThemeNames: (typeof themeCatalog !== "undefined" && themeCatalog) ? themeCatalog.lightThemeNames : []
    readonly property var darkThemeNames: (typeof themeCatalog !== "undefined" && themeCatalog) ? themeCatalog.darkThemeNames : []

    function getLightThemeNames() { return lightThemeNames }
    function getDarkThemeNames() { return darkThemeNames }

    // Transition duration for smooth color shifts (e.g. album art theme changes)
    property int colorTransitionMs: 1000

    property color bottomBarGradientStart: activeTheme.base
    Behavior on bottomBarGradientStart { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarGradientEnd: activeTheme.baseAlt
    Behavior on bottomBarGradientEnd { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarToggleShade: activeTheme.bottombar.toggleShade
    Behavior on bottomBarToggleShade { ColorAnimation { duration: colorTransitionMs } }
    property color hoverColor: activeTheme.states.hover
    Behavior on hoverColor { ColorAnimation { duration: colorTransitionMs } }
    property color hoverPausedColor: activeTheme.states.paused
    Behavior on hoverPausedColor { ColorAnimation { duration: colorTransitionMs } }
    property color hoverPlayingColor: activeTheme.states.playing
    Behavior on hoverPlayingColor { ColorAnimation { duration: colorTransitionMs } }
    property color pausedHighlightColor: activeTheme.states.paused
    Behavior on pausedHighlightColor { ColorAnimation { duration: colorTransitionMs } }
    property color playingHighlightColor: activeTheme.states.playing
    Behavior on playingHighlightColor { ColorAnimation { duration: colorTransitionMs } }
    property color rowBackgroundColor: activeTheme.base
    Behavior on rowBackgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color backgroundColor: activeTheme.base
    Behavior on backgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color headerBackgroundColor: activeTheme.baseAlt
    Behavior on headerBackgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomToggleShade: activeTheme.mediaroom.toggleShade
    Behavior on mediaRoomToggleShade { ColorAnimation { duration: colorTransitionMs } }
    property color mediaContentArea: activeTheme.mainmenu.mediaContainer
    Behavior on mediaContentArea { ColorAnimation { duration: colorTransitionMs } }
    property color obdBoxBackground: activeTheme.obd.boxBackground
    Behavior on obdBoxBackground { ColorAnimation { duration: colorTransitionMs } }
    property color sidebarColor: activeTheme.base
    Behavior on sidebarColor { ColorAnimation { duration: colorTransitionMs } }
    property color contentColor: activeTheme.baseAlt
    Behavior on contentColor { ColorAnimation { duration: colorTransitionMs } }

    property color primaryTextColor: activeTheme.text.primary
    Behavior on primaryTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color secondaryTextColor: activeTheme.text.secondary
    Behavior on secondaryTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color headerTextColor: activeTheme.text.primary
    Behavior on headerTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color clockTextColor: activeTheme.accent
    Behavior on clockTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color metadataColor: activeTheme.accent
    Behavior on metadataColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdLabelColor: activeTheme.obd.labelColor ? activeTheme.obd.labelColor : secondaryTextColor
    Behavior on obdLabelColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdValueColor: activeTheme.obd.valueColor ? activeTheme.obd.valueColor : primaryTextColor
    Behavior on obdValueColor { ColorAnimation { duration: colorTransitionMs } }

    property color accent: activeTheme.accent
    Behavior on accent { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarActiveToggleButton: activeTheme.accent
    Behavior on bottomBarActiveToggleButton { ColorAnimation { duration: colorTransitionMs } }

    // Bottom bar button icon colors
    property color bottomBarPreviousButton: activeTheme.bottombar.previous
    Behavior on bottomBarPreviousButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPlayButton: activeTheme.bottombar.play
    Behavior on bottomBarPlayButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPauseButton: activeTheme.bottombar.pause
    Behavior on bottomBarPauseButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarNextButton: activeTheme.bottombar.next
    Behavior on bottomBarNextButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarVolumeButton: activeTheme.bottombar.volume
    Behavior on bottomBarVolumeButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarShuffleButton: activeTheme.bottombar.shuffle
    Behavior on bottomBarShuffleButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarHomeButton: activeTheme.bottombar.homeButton
    Behavior on bottomBarHomeButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarOBDButton: activeTheme.bottombar.obdButton
    Behavior on bottomBarOBDButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarMediaButton: activeTheme.bottombar.mediaButton
    Behavior on bottomBarMediaButton { ColorAnimation { duration: colorTransitionMs } }
    // Fall back to homeButton when an extracted (or older persisted) album-art
    // theme JSON doesn't carry a sensorButton key — keeps the icon reactive
    // even with partial palettes.
    property color bottomBarSensorButton: activeTheme.bottombar.sensorButton
        || activeTheme.bottombar.homeButton
    Behavior on bottomBarSensorButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarSettingsButton: activeTheme.bottombar.settingsButton
    Behavior on bottomBarSettingsButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarAndroidAutoButton: activeTheme.bottombar.androidAutoButton
    Behavior on bottomBarAndroidAutoButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPhoneMirrorButton: activeTheme.bottombar.phoneMirrorButton
    Behavior on bottomBarPhoneMirrorButton { ColorAnimation { duration: colorTransitionMs } }

    // MediaRoom button icon colors
    property color mediaRoomPreviousButton: activeTheme.mediaroom.previous
    Behavior on mediaRoomPreviousButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomPlayButton: activeTheme.mediaroom.play
    Behavior on mediaRoomPlayButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomPauseButton: activeTheme.mediaroom.pause
    Behavior on mediaRoomPauseButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomNextButton: activeTheme.mediaroom.next
    Behavior on mediaRoomNextButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomLeftButton: activeTheme.mediaroom.left
    Behavior on mediaRoomLeftButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomRightButton: activeTheme.mediaroom.right
    Behavior on mediaRoomRightButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomToggleButton: activeTheme.mediaroom.shuffle
    Behavior on mediaRoomToggleButton { ColorAnimation { duration: colorTransitionMs } }

    // Slider colors
    property color volumeSliderColor: activeTheme.sliders.volume
    Behavior on volumeSliderColor { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomSlider: activeTheme.sliders.media
    Behavior on mediaRoomSlider { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomSeekColor: activeTheme.sliders.volume
    Behavior on mediaRoomSeekColor { ColorAnimation { duration: colorTransitionMs } }
    property color settingsSliderColor: activeTheme.sliders.settings
    Behavior on settingsSliderColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdBarColor: activeTheme.obd.barColor
    Behavior on obdBarColor { ColorAnimation { duration: colorTransitionMs } }

    // Status colors (with safe fallbacks for themes that don't define them)
    property color statusConnected: activeTheme.status ? activeTheme.status.connected : "#44AA44"
    Behavior on statusConnected { ColorAnimation { duration: colorTransitionMs } }
    property color statusDisconnected: activeTheme.status ? activeTheme.status.disconnected : "#AA4444"
    Behavior on statusDisconnected { ColorAnimation { duration: colorTransitionMs } }
    property color statusError: activeTheme.status ? activeTheme.status.error : "#F44336"
    Behavior on statusError { ColorAnimation { duration: colorTransitionMs } }
    property color statusWarning: activeTheme.status ? activeTheme.status.warning : "#FF9800"
    Behavior on statusWarning { ColorAnimation { duration: colorTransitionMs } }
    property color statusInfo: activeTheme.status ? activeTheme.status.info : "#2196F3"
    Behavior on statusInfo { ColorAnimation { duration: colorTransitionMs } }
    property color statusSuccess: activeTheme.status ? activeTheme.status.success : "#4CAF50"
    Behavior on statusSuccess { ColorAnimation { duration: colorTransitionMs } }
    property color statusDanger: activeTheme.status ? activeTheme.status.danger : "#e74c3c"
    Behavior on statusDanger { ColorAnimation { duration: colorTransitionMs } }

    // Function to update theme
    function setTheme(theme) {
        if (themes[theme] || customThemes[theme]) {
            if (theme !== "Album Art Capture") lastStaticTheme = theme
            else _firstAlbumArtUpdate = true
            currentTheme = theme
        }
    }

    // Apply a theme without animating the transition. Used for startup loads
    // and manual theme switches — the configured `colorTransitionMs` only
    // applies to album-art-driven color shifts on song change.
    function setThemeInstant(theme) {
        if (!(themes[theme] || customThemes[theme])) return
        if (theme !== "Album Art Capture") lastStaticTheme = theme
        else _firstAlbumArtUpdate = true
        var saved = colorTransitionMs
        colorTransitionMs = 0
        currentTheme = theme
        Qt.callLater(function() { colorTransitionMs = saved })
    }

    // The first album-art paint after entering Album Art mode (startup with
    // it already on, or a fresh toggle-on) snaps instantly to the current
    // song's colors — only real song changes should honor the transition
    // speed. Reset to true whenever the theme switches to "Album Art Capture".
    property bool _firstAlbumArtUpdate: true
    
    // Changed signal name to avoid conflict
    signal customThemesUpdated()
}