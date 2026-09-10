import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.folderlistmodel 2.15
import "." as App

ApplicationWindow {
    id: mainWindow
    visible: true
    title: deviceName

    // Store the system default font family at startup
    property string systemDefaultFont: ""

    // Global font setting - applies to all child components
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    font.family: App.Style.fontFamily !== "" ? App.Style.fontFamily : systemDefaultFont

    // Minimum window constraints
    minimumWidth: 400
    minimumHeight: 300

    // Properties bound to settings manager
    property string deviceName: settingsManager ? settingsManager.deviceName : "Default Device"
    property string theme: settingsManager ? settingsManager.themeSetting : "Light"
    property real startUpVolume: settingsManager ? settingsManager.startUpVolume : 0.1
    property bool showClock: settingsManager ? settingsManager.showClock : true
    property bool clockFormat24Hour: settingsManager ? settingsManager.clockFormat24Hour : true
    property int clockSize: settingsManager ? settingsManager.clockSize : 18
    property string lastSettingsSection: settingsManager ? settingsManager.lastSettingsSection : "deviceSettings"
    property string fontSetting: settingsManager ? settingsManager.fontSetting : "System Default"

    // Album Art Colors - used by signal handlers to track changes
    property string lastAppliedAlbumArtColors: ""

    // Font loading properties
    property var loadedFonts: ({})
    property var fontLoaders: []

    // Screen dimension properties
    property int screenWidth: settingsManager ? settingsManager.screenWidth : 1280
    property int screenHeight: settingsManager ? settingsManager.screenHeight : 720

    // Bottom bar orientation property
    property bool isVerticalLayout: settingsManager ?
                                   settingsManager.bottomBarOrientation === "side" : false

    // ── Now Playing Studio overlay ──
    // When true, the Now Playing immersive editor takes over the entire window
    // (covers stackView + BottomBar). Toggled from the Now Playing tile click
    // in the settings tile pages. Origin rect drives the hero-morph open
    // animation (matches SettingsCardPopup): studio grows from the clicked
    // tile's rect to fill the window.
    property bool nowPlayingStudioOpen: false
    property rect nowPlayingStudioOriginRect: Qt.rect(0, 0, 0, 0)
    function openNowPlayingStudio(originRect) {
        if (originRect && originRect.width > 0)
            nowPlayingStudioOriginRect = originRect
        nowPlayingStudioOpen = true
    }
    function closeNowPlayingStudio() { nowPlayingStudioOpen = false }

    // Global shift light flash properties
    property real currentRpm: 0
    property var shiftLightFlags: []
    property var activeShiftFlag: null
    property bool shiftLightFlashVisible: true
    property bool shiftLightEnabled: settingsManager ? settingsManager.get_setting_with_default("rpm_shift_light_enabled", true) : true
    property real fullScreenFlashOpacity: settingsManager ? settingsManager.get_setting_with_default("rpm_fullscreen_flash_opacity", 0.5) : 0.5

    // Function to find active flag based on current RPM
    function findActiveFlag() {
        if (!shiftLightFlags || shiftLightFlags.length === 0) {
            activeShiftFlag = null
            return
        }
        // Check from highest priority (last) to lowest (first)
        var active = null
        for (var i = shiftLightFlags.length - 1; i >= 0; i--) {
            var flag = shiftLightFlags[i]
            var low = flag.rpmLow !== undefined ? flag.rpmLow : flag.rpm
            var high = flag.rpmHigh !== undefined ? flag.rpmHigh : 99999
            if (currentRpm >= low && currentRpm <= high) {
                active = flag
                break
            }
        }
        activeShiftFlag = active
    }

    // Set initial window size. On desktop use the saved user preference so
    // their previous window size persists across runs. On mobile we must NOT
    // set width/height at all — the Qt Android window manager pillarboxes
    // the window around display cutouts (per the manifest
    // windowLayoutInDisplayCutoutMode=never setting), and any hardcoded
    // width/height here overrides that pillarboxing, causing content to
    // intrude into the cutout / rounded-corner area. Leaving the bindings
    // unset on Android lets the OS own the sizing.
    width: isAndroid ? undefined : screenWidth
    height: isAndroid ? undefined : screenHeight

    // Handle window close (force close / X button)
    onClosing: function(close) {
        console.log("Window closing - running cleanup...")
        if (mediaManager) {
            mediaManager._save_playback_state()
        }
        if (spotifyManager) {
            spotifyManager.cleanup()
        }
        close.accepted = true
    }

    // Initialize settings and theme
    Component.onCompleted: {
        console.log("[GlobalFlash] ========== Main.qml Component.onCompleted START ==========")
        console.log("[QML] Main.qml Component.onCompleted - QML console working!")

        // Capture the system default font at startup (before any custom font is applied)
        systemDefaultFont = font.family
        // Also store it in Style for other components to use
        App.Style.systemDefaultFont = font.family

        if (settingsManager) {
            // Load custom themes BEFORE setTheme so saved custom themes
            // are available when the theme guard checks customThemes[theme]
            let customThemes = settingsManager.customThemes
            customThemes.forEach(function(themeName) {
                let themeJSON = settingsManager.get_custom_theme(themeName)
                let themeObj = JSON.parse(themeJSON)
                App.Style.addCustomTheme(themeName, themeObj)
            })

            // Load theme — startup is always instant, transition speed only
            // applies to album-art-driven shifts on song change.
            console.log("[QML] Loading theme:", settingsManager.themeSetting)
            if (settingsManager.themeSetting) {
                // If Album Art was the active theme last session, prime
                // `albumArtTheme` with the persisted palette BEFORE switching
                // currentTheme so the placeholder Album Art colors never
                // paint — `activeTheme` resolves straight to the saved one.
                if (settingsManager.themeSetting === "Album Art Capture"
                        && settingsManager.albumArtColors) {
                    App.Style.updateAlbumArtTheme(settingsManager.albumArtColors)
                }
                App.Style.setThemeInstant(settingsManager.themeSetting)
            }

            // Load dimensions (desktop only — mobile window is fullscreen and
            // already bound to Screen.width/height above; overwriting from
            // settings would shrink the render surface).
            if (!isAndroid) {
                width = settingsManager.screenWidth
                height = settingsManager.screenHeight
            }

            // Initialize spacing
            App.Spacing.updateDimensions(width, height)

            // Set auto-scale from screen detection
            if (typeof screenAutoScale !== "undefined") {
                App.Spacing.autoScaleFactor = screenAutoScale
            }

            // Set the UI scale from settings (now a user preference multiplier)
            App.Spacing.globalScale = settingsManager.uiScale

            // Load color transition speed
            App.Style.colorTransitionMs = settingsManager.colorTransitionMs

            if (mediaManager) {
                mediaManager.connect_settings_manager(settingsManager)
            }

            // Initialize orientation
            isVerticalLayout = settingsManager.bottomBarOrientation === "side"

            // Restore window state
            let savedState = settingsManager.get_window_state()
            if (savedState === "fullscreen") {
                mainWindow.visibility = Window.FullScreen
            } else if (savedState === "maximized") {
                mainWindow.visibility = Window.Maximized
            }
        }

        // Note: albumColorsExtracted signal is connected via Connections component below
        // for better reliability with PySide6 signals
        
        // Update theme list when custom themes change
        if (settingsManager) {
            settingsManager.customThemesChanged.connect(function() {
                // Build new custom themes object and assign in one shot
                // (avoids briefly clearing customThemes which would make activeTheme
                // fall back to SolarizedLight if a custom theme is currently active)
                var newCustomThemes = {}
                let customThemes = settingsManager.customThemes
                customThemes.forEach(function(themeName) {
                    let themeJSON = settingsManager.get_custom_theme(themeName)
                    newCustomThemes[themeName] = JSON.parse(themeJSON)
                })
                App.Style.customThemes = newCustomThemes

                // Force update of theme options
                App.Style.customThemesUpdated()
            })
        }

        // Load shift light flags for global full screen flash
        loadShiftLightFlags()
        console.log("[GlobalFlash] Loaded flags:", JSON.stringify(shiftLightFlags))

        // Debug: Check if obdManager is available and connect signal manually
        console.log("[GlobalFlash] obdManager available:", obdManager !== null && obdManager !== undefined)
        if (obdManager) {
            console.log("[GlobalFlash] obdManager.rpmChanged exists:", obdManager.rpmChanged !== null && obdManager.rpmChanged !== undefined)
            if (obdManager.rpmChanged) {
                console.log("[GlobalFlash] Manually connecting to obdManager.rpmChanged signal")
                obdManager.rpmChanged.connect(function(rpm) {
                    mainWindow.currentRpm = rpm
                    mainWindow.findActiveFlag()
                    if (mainWindow.activeShiftFlag !== null) {
                        console.log("[GlobalFlash] RPM:", rpm, "Active flag:", mainWindow.activeShiftFlag.color, "fullScreenFlash:", mainWindow.activeShiftFlag.fullScreenFlash)
                    }
                })
                console.log("[GlobalFlash] Signal connection established!")
            } else {
                console.log("[GlobalFlash] ERROR: obdManager.rpmChanged is null/undefined")
            }
        } else {
            console.log("[GlobalFlash] ERROR: obdManager is null/undefined")
        }
    }

    // Window resize handlers
    onWidthChanged: {
        if (settingsManager && width > 0 && width >= minimumWidth) {
            settingsManager.save_screen_width(width)
            screenWidth = width
            App.Spacing.updateDimensions(width, height)
        }
    }

    onHeightChanged: {
        if (settingsManager && height > 0 && height >= minimumHeight) {
            settingsManager.save_screen_height(height)
            screenHeight = height
            App.Spacing.updateDimensions(width, height)
        }
    }

    // Settings manager connections
    Connections {
        target: settingsManager
        
        function onScreenWidthChanged() {
            if (settingsManager && !isAndroid) {
                width = settingsManager.screenWidth
                App.Spacing.updateDimensions(width, height)
            }
        }

        function onScreenHeightChanged() {
            if (settingsManager && !isAndroid) {
                height = settingsManager.screenHeight
                App.Spacing.updateDimensions(width, height)
            }
        }
        
        function onThemeSettingChanged() {
            if (settingsManager) {
                var newTheme = settingsManager.themeSetting
                // Manual theme switch — instant, no fade.
                App.Style.setThemeInstant(newTheme)
                theme = newTheme

                // If Album Art Capture is selected, extract colors from current song
                if (newTheme === "Album Art Capture" && mediaManager) {
                    var currentFile = mediaManager.get_current_file()
                    if (currentFile)
                        mediaManager.extract_colors_from_album_art(currentFile)
                }
            }
        }

        function onFontSettingChanged() {
            if (settingsManager) {
                App.Style.setFont(settingsManager.fontSetting)
                fontSetting = settingsManager.fontSetting
            }
        }
        
        function onSongLengthTransitionChanged() {
            if (settingsManager && !settingsManager.songLengthTransition) {
                // Reverted to manual mode - restore the saved static value
                App.Style.colorTransitionMs = settingsManager.colorTransitionMs
            }
        }

        function onBottomBarOrientationChanged() {
            if (settingsManager) {
                isVerticalLayout = settingsManager.bottomBarOrientation === "side"
                // Force an update of the bottom bar
                bottomBar.isVertical = isVerticalLayout

                // Force recalculation of z-order
                stackView.z = 1
                bottomBar.z = 0

                // If on settings page, reload it after layout settles
                if (stackView.currentItem && stackView.currentItem.objectName === "settingsMenu") {
                    var section = stackView.currentItem.currentSection || ""
                    navReloadTimer.pendingSection = section
                    navReloadTimer.restart()
                }
            }
        }

        function onAlbumArtColorsChanged(themeJson) {
            console.log("[AlbumArtCapture] settingsManager received albumArtColorsChanged!")
            // In PySide6, signal arguments may not pass through Connections properly
            // So we read directly from the property as a workaround
            var colors = settingsManager.albumArtColors
            console.log("[AlbumArtCapture] Colors from property, length:", colors ? colors.length : "null")
            if (colors && colors.length > 0) {
                console.log("[AlbumArtCapture] Updating theme with new colors...")
                App.Style.updateAlbumArtTheme(colors)
            }
        }
    }

    // Media manager connections for Album Art Capture (backup)
    Connections {
        target: mediaManager

        function onAlbumColorsExtracted(themeJson) {
            console.log("[AlbumArtCapture] Connections received albumColorsExtracted!")
            console.log("[AlbumArtCapture] themeJson:", themeJson ? themeJson.substring(0, 100) : "null")
            App.Style.updateAlbumArtTheme(themeJson)
        }

        function onDurationChanged(durationMs) {
            if (settingsManager && settingsManager.songLengthTransition && durationMs > 0) {
                App.Style.colorTransitionMs = durationMs
            }
        }
    }

    // Spotify manager connections for Album Art Capture
    Connections {
        target: spotifyManager

        function onAlbumColorsExtracted(themeJson) {
            console.log("[AlbumArtCapture Spotify] Received albumColorsExtracted!")
            console.log("[AlbumArtCapture Spotify] themeJson:", themeJson ? themeJson.substring(0, 100) : "null")
            lastAppliedAlbumArtColors = themeJson
            App.Style.updateAlbumArtTheme(themeJson)
        }

        function onDurationChanged(durationMs) {
            if (settingsManager && settingsManager.songLengthTransition && durationMs > 0) {
                App.Style.colorTransitionMs = durationMs
            }
        }
    }

    // Remote navigation for perf testing (called from Python command server)
    function navigateTo(pageName) {
        var pageMap = {
            "MainMenu.qml":         { file: "MainMenu.qml",         props: { stackView: stackView, windowWidth: mainWindow.width, windowHeight: mainWindow.height } },
            "MediaRoom.qml":        { file: "MediaRoom.qml",        props: { stackView: stackView } },
            "MediaPlayer.qml":      { file: "MediaPlayer.qml",      props: { stackView: stackView, mainWindow: mainWindow } },
            "OBDHome.qml":          { file: "OBDHome.qml",          props: { stackView: stackView, mainWindow: mainWindow } },
            "OBDMenu.qml":          { file: "OBDMenu.qml",          props: { stackView: stackView, mainWindow: mainWindow } },
            "OBDDiagnostics.qml":   { file: "OBDDiagnostics.qml",   props: { stackView: stackView, mainWindow: mainWindow } },
            "SettingsMenu.qml":     { file: "SettingsMenu.qml",     props: { stackView: stackView, mainWindow: mainWindow, initialSection: "displaySettings" } },
            "PhoneMirrorView.qml":  { file: "PhoneMirrorView.qml",  props: { stackView: stackView } },
            "AndroidAutoView.qml":  { file: "AndroidAutoView.qml",  props: { stackView: stackView } },
            "SensorHome.qml":       { file: "SensorHome.qml",       props: { stackView: stackView, mainWindow: mainWindow } },
            "SensorMenu.qml":       { file: "SensorMenu.qml",       props: { stackView: stackView, mainWindow: mainWindow } },
            "CarMenu.qml":          { file: "CarMenu.qml",          props: { stackView: stackView, mainWindow: mainWindow } },
            "DownloadPage.qml":     { file: "DownloadPage.qml",     props: { stackView: stackView } },
            "ClockMenu.qml":        { file: "ClockMenu.qml",        props: { stackView: stackView } },
        }
        var entry = pageMap[pageName]
        if (!entry) {
            console.warn("[NAV] Unknown page:", pageName)
            return
        }
        var component = Qt.createComponent(entry.file)
        if (component.status === Component.Ready) {
            var page = component.createObject(stackView, entry.props)
            if (page) {
                stackView.push(page)
                console.log("[NAV] Pushed", pageName)
            }
        } else {
            console.warn("[NAV] Failed to load", pageName, component.errorString())
        }
    }

    function navigateHome() {
        stackView.pop(null)  // pop to the initial item
        console.log("[NAV] Returned to home")
    }

    // Open Settings on a specific section. Settings is special-cased because
    // SettingsMenu has a `required property string initialSection` — the
    // generic navigateTo() can't push it without one. Used by the command
    // server / screenshot sweeper to walk every settings page. If already on
    // Settings, switches section in place instead of stacking a new copy.
    function navigateToSettings(section) {
        var target = (section && section !== "") ? section : "displaySettings"
        if (stackView.currentItem && stackView.currentItem.objectName === "settingsMenu") {
            stackView.currentItem.navigateToCategory(target)
            console.log("[NAV] Settings section ->", target)
            return
        }
        var comp = Qt.createComponent("SettingsMenu.qml")
        if (comp.status !== Component.Ready) {
            console.warn("[NAV] SettingsMenu load failed:", comp.errorString())
            return
        }
        var page = comp.createObject(stackView, {
            stackView: stackView,
            mainWindow: mainWindow,
            initialSection: target
        })
        if (page) {
            stackView.push(page)
            console.log("[NAV] Pushed Settings ->", target)
        }
    }

    function navigateBack() {
        if (stackView.depth > 1) {
            stackView.pop()
            console.log("[NAV] Popped one page")
        }
    }

    // Main layout container
    Item {
        id: mainContainer
        anchors.fill: parent

        // Main stack view with adaptive anchoring
        StackView {
            id: stackView
            objectName: "stackView"
            z: 1

            // Cached DownloadPage — created once, reused across all navigations
            property var _cachedDownloadPage: null


            // Different anchoring based on orientation
            anchors {
                left: isVerticalLayout ? bottomBar.right : parent.left
                right: parent.right
                top: parent.top
                bottom: isVerticalLayout ? parent.bottom : bottomBar.top
            }

            initialItem: MainMenu {
                stackView: stackView
                windowWidth: mainWindow.width
                windowHeight: mainWindow.height
            }

            // Disable transitions for better performance
            pushEnter: null
            pushExit: null
            popEnter: null
            popExit: null
            replaceEnter: null
            replaceExit: null
        }

        // Reload settings page after nav bar orientation change
        Timer {
            id: navReloadTimer
            interval: 150
            repeat: false
            property string pendingSection: ""
            onTriggered: {
                if (stackView.currentItem && stackView.currentItem.objectName === "settingsMenu") {
                    var page = Qt.createComponent("SettingsMenu.qml").createObject(stackView, {
                        stackView: stackView,
                        mainWindow: mainWindow,
                        initialSection: pendingSection
                    })
                    stackView.replace(stackView.currentItem, page)
                }
            }
        }

        // Drop shadow for the bottom bar
        Rectangle {
            id: bottomBarShadow
            z: 2
            visible: !isVerticalLayout
            anchors.bottom: bottomBar.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 12
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#30000000" }
            }
        }
        Rectangle {
            id: sideBarShadow
            z: 2
            visible: isVerticalLayout
            anchors.left: bottomBar.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 12
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#30000000" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        // Bottom bar - positioning is handled internally in BottomBar.qml
        BottomBar {
            id: bottomBar
            z: 0
            stackView: stackView
            mainWindow: mainWindow
            isVertical: isVerticalLayout

            // Force update on orientation change
            onIsVerticalChanged: {
                // Trigger a layout update
                if (isVertical) {
                    anchors.left = parent.left
                    anchors.top = parent.top
                    anchors.bottom = parent.bottom
                    anchors.right = undefined
                    width = parent.width * 0.1
                } else {
                    anchors.bottom = parent.bottom
                    anchors.left = parent.left
                    anchors.right = parent.right
                    anchors.top = undefined
                    height = parent.height * App.Spacing.bottomBarHeightPercent
                }
            }
        }

        // ── Now Playing Studio: window-filling immersive editor ──
        // Covers StackView (settings tabs) + BottomBar; opened by the Now Playing
        // tile in Media settings. Internal back chip closes it. Geometry is
        // driven by the studio's openProgress + originRect for the hero-morph
        // open animation (matches SettingsCardPopup); we keep `visible` true
        // through the close animation until openProgress fully returns to 0.
        NowPlayingStudio {
            id: nowPlayingStudio
            z: 5
            visible: mainWindow.nowPlayingStudioOpen || openProgress > 0.001
            mainWindow: mainWindow
            originRect: mainWindow.nowPlayingStudioOriginRect
            openProgress: mainWindow.nowPlayingStudioOpen ? 1.0 : 0.0
            // Fully-open target excludes the BottomBar (so the nav stays
            // visible) and — when settings is in Sidebar layout — also the
            // settings sidebar on the left, so the user can switch sections
            // without closing the studio.
            readonly property bool _reserveSettingsSidebar:
                settingsManager && settingsManager.settingsLayoutStyle === "Sidebar"
            readonly property int _sidebarReserve:
                _reserveSettingsSidebar ? App.Spacing.settingsNavWidth : 0
            targetX: (isVerticalLayout ? bottomBar.width : 0) + _sidebarReserve
            targetY: 0
            targetWidth: parent.width - (isVerticalLayout ? bottomBar.width : 0) - _sidebarReserve
            targetHeight: parent.height - (isVerticalLayout ? 0 : bottomBar.height)
            onCloseRequested: mainWindow.closeNowPlayingStudio()
        }

        // BottomBar nav presists from inside the studio: if the user taps
        // Home / Music / OBD / Settings while the studio is open, the
        // stackView changes pages and we close the studio so it doesn't
        // hang on top of an unrelated screen.
        Connections {
            target: stackView
            function onCurrentItemChanged() {
                if (mainWindow.nowPlayingStudioOpen)
                    mainWindow.closeNowPlayingStudio()
                updateSensorConsumers()
            }
            Component.onCompleted: updateSensorConsumers()
            // The 3D vehicle and sensor pages are the only consumers of the
            // 60 Hz IMU stream; everywhere else 5 Hz keeps the settings status
            // fresh without seven queued signals per frame into idle bindings.
            function updateSensorConsumers() {
                if (typeof berryIMU === "undefined" || !berryIMU || !stackView.currentItem)
                    return
                var n = stackView.currentItem.objectName
                berryIMU.setActive(n === "carMenu" || n === "sensorMenu" || n === "sensorHome")
            }
        }

        // Global full screen overlay - INSIDE the main container for cross-platform compatibility
        // Must be last child to render on top of StackView and BottomBar
        Rectangle {
            id: globalFullScreenFlash
            anchors.fill: parent
            z: 999999  // High z-index to be above all other content
            visible: mainWindow.shiftLightEnabled &&
                     mainWindow.activeShiftFlag !== null &&
                     mainWindow.activeShiftFlag.fullScreenFlash === true &&
                     mainWindow.shiftLightFlashVisible
            color: mainWindow.activeShiftFlag ? mainWindow.activeShiftFlag.color : "transparent"
            // Use per-flag opacity if available, otherwise fall back to global setting
            opacity: mainWindow.activeShiftFlag && mainWindow.activeShiftFlag.fullScreenFlashOpacity !== undefined ?
                     mainWindow.activeShiftFlag.fullScreenFlashOpacity : mainWindow.fullScreenFlashOpacity

            // Allow clicks to pass through
            enabled: false

            Component.onCompleted: {
                console.log("[GlobalFlash] Overlay Rectangle created inside mainContainer")
            }
        }

        // Aura overlay - perimeter-only color fade (4 edge gradients)
        Item {
            id: globalAuraFlash
            anchors.fill: parent
            z: 999999
            enabled: false
            visible: mainWindow.shiftLightEnabled &&
                     mainWindow.activeShiftFlag !== null &&
                     mainWindow.activeShiftFlag.auraFlash === true &&
                     mainWindow.shiftLightFlashVisible
            opacity: mainWindow.activeShiftFlag && mainWindow.activeShiftFlag.auraFlashOpacity !== undefined ?
                     mainWindow.activeShiftFlag.auraFlashOpacity : 0.6

            property color auraColor: mainWindow.activeShiftFlag ? mainWindow.activeShiftFlag.color : "transparent"
            property real edgeFraction: mainWindow.activeShiftFlag && mainWindow.activeShiftFlag.auraSpread !== undefined ?
                                        mainWindow.activeShiftFlag.auraSpread : 0.18

            // Top edge
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: parent.height * globalAuraFlash.edgeFraction
                gradient: Gradient {
                    GradientStop { position: 0.0; color: globalAuraFlash.auraColor }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            // Bottom edge
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: parent.height * globalAuraFlash.edgeFraction
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: globalAuraFlash.auraColor }
                }
            }
            // Left edge
            Rectangle {
                anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                width: parent.width * globalAuraFlash.edgeFraction
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: globalAuraFlash.auraColor }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            // Right edge
            Rectangle {
                anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                width: parent.width * globalAuraFlash.edgeFraction
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: globalAuraFlash.auraColor }
                }
            }
        }
    }

    // Font folder model to scan for available fonts
    FolderListModel {
        id: fontFolderModel
        folder: Qt.resolvedUrl("assets/fonts")
        nameFilters: ["*.ttf", "*.otf", "*.TTF", "*.OTF"]
        showDirs: false
        showHidden: false
        sortField: FolderListModel.Name

        onStatusChanged: {
            console.log("Font folder status:", status, "count:", count, "folder:", folder)
            if (status === FolderListModel.Ready) {
                loadFontsFromFolder()
            }
        }

        onCountChanged: {
            console.log("Font folder count changed:", count)
            if (status === FolderListModel.Ready && count > 0) {
                loadFontsFromFolder()
            }
        }
    }

    // Dynamically created FontLoader instances
    property var fontLoaderComponent: Component {
        FontLoader {}
    }

    // Track pending font loads
    property int pendingFontLoads: 0
    property var pendingFontData: []

    // Function to load fonts from the folder
    function loadFontsFromFolder() {
        console.log("Loading fonts from folder, count:", fontFolderModel.count)

        // Clear previous loaders
        for (var j = 0; j < fontLoaders.length; j++) {
            fontLoaders[j].destroy()
        }
        fontLoaders = []
        pendingFontData = []
        pendingFontLoads = 0

        if (fontFolderModel.count === 0) {
            console.log("No fonts found in folder")
            App.Style.registerFonts([], {})
            return
        }

        pendingFontLoads = fontFolderModel.count

        for (var i = 0; i < fontFolderModel.count; i++) {
            var filePath = fontFolderModel.get(i, "fileUrl")
            var fileName = fontFolderModel.get(i, "fileName")

            // Create display name from filename (remove extension)
            var displayName = fileName.replace(/\.(ttf|otf)$/i, "").replace(/[-_]/g, " ")

            console.log("Creating FontLoader for:", fileName, "->", filePath)

            // Create a FontLoader for this font
            var loader = fontLoaderComponent.createObject(mainWindow, {
                "source": filePath,
                "objectName": displayName
            })

            fontLoaders.push(loader)

            // Handle async loading
            if (loader.status === FontLoader.Ready) {
                onFontLoaded(loader, displayName)
            } else if (loader.status === FontLoader.Loading) {
                // Connect to status change
                loader.statusChanged.connect(function() {
                    var ld = loader
                    var dn = displayName
                    return function() {
                        if (ld.status === FontLoader.Ready) {
                            onFontLoaded(ld, dn)
                        } else if (ld.status === FontLoader.Error) {
                            console.warn("Failed to load font:", dn)
                            pendingFontLoads--
                            checkFontsComplete()
                        }
                    }
                }())
            } else {
                console.warn("Font load error for:", fileName, "status:", loader.status)
                pendingFontLoads--
            }
        }

        // Check if all loaded synchronously
        checkFontsComplete()
    }

    function onFontLoaded(loader, displayName) {
        console.log("Font loaded:", displayName, "->", loader.name)
        pendingFontData.push({ name: displayName, family: loader.name })
        pendingFontLoads--
        checkFontsComplete()
    }

    function checkFontsComplete() {
        if (pendingFontLoads <= 0) {
            var fontNames = []
            var familyMap = {}

            for (var i = 0; i < pendingFontData.length; i++) {
                fontNames.push(pendingFontData[i].name)
                familyMap[pendingFontData[i].name] = pendingFontData[i].family
            }

            console.log("All fonts loaded:", fontNames)

            // Register fonts with Style
            App.Style.registerFonts(fontNames, familyMap)

            // Apply saved font setting
            if (settingsManager && settingsManager.fontSetting) {
                App.Style.setFont(settingsManager.fontSetting)
            }
        }
    }

    // Settings update functions
    function updateDeviceName(newDeviceName) {
        if (settingsManager && newDeviceName.trim() !== "") {
            settingsManager.save_device_name(newDeviceName)
            deviceName = newDeviceName
        }
    }

    function updateTheme(newTheme) {
        console.warn("[AlbumArtCapture] updateTheme called with:", newTheme)
        if (settingsManager) {
            settingsManager.save_theme_setting(newTheme)
            // Manual theme switch — instant, no fade.
            App.Style.setThemeInstant(newTheme)
            theme = newTheme

            // If Album Art Capture is selected, immediately extract colors from current song
            console.warn("[AlbumArtCapture] Checking if Album Art Capture, mediaManager exists:", typeof mediaManager)
            if (newTheme === "Album Art Capture") {
                try {
                    if (mediaManager) {
                        let currentFile = mediaManager.get_current_file()
                        console.warn("[AlbumArtCapture] Theme selected, current file:", currentFile)
                        if (currentFile) {
                            console.warn("[AlbumArtCapture] Calling extract_colors_from_album_art now...")
                            mediaManager.extract_colors_from_album_art(currentFile)
                            console.warn("[AlbumArtCapture] Call completed")
                        } else {
                            console.warn("[AlbumArtCapture] No current file!")
                        }
                    } else {
                        console.warn("[AlbumArtCapture] mediaManager is null/undefined!")
                    }
                } catch (e) {
                    console.error("[AlbumArtCapture] Error:", e)
                }
            }
        }
    }

    function updateFont(newFont) {
        if (settingsManager) {
            settingsManager.save_font_setting(newFont)
            App.Style.setFont(newFont)
            fontSetting = newFont
        }
    }

    function updateStartupVolume(newVolume) {
        if (settingsManager) {
            settingsManager.save_start_volume(newVolume)
            startUpVolume = newVolume
        }
    }

    function updateScreenDimensions(newWidth, newHeight) {
        if (settingsManager) {
            if (newWidth >= minimumWidth) {
                settingsManager.save_screen_width(newWidth)
                width = newWidth
            }
            if (newHeight >= minimumHeight) {
                settingsManager.save_screen_height(newHeight)
                height = newHeight
            }
            App.Spacing.updateDimensions(width, height)
        }
    }

    // Load shift light flags from settings
    function loadShiftLightFlags() {
        if (settingsManager) {
            try {
                var savedFlags = settingsManager.get_setting_with_default("rpm_flags", "[]")
                shiftLightFlags = JSON.parse(savedFlags)
            } catch(e) {
                shiftLightFlags = []
            }
            shiftLightEnabled = settingsManager.get_setting_with_default("rpm_shift_light_enabled", true)
            fullScreenFlashOpacity = settingsManager.get_setting_with_default("rpm_fullscreen_flash_opacity", 0.5)
        }
    }

    // Note: OBD manager signal connection is done manually in Component.onCompleted
    // for better reliability with PySide6

    // Settings manager connections for shift light settings changes
    Connections {
        target: settingsManager
        function onGenericSettingChanged(key) {
            if (key.startsWith("rpm_")) {
                mainWindow.loadShiftLightFlags()
            }
        }
    }

    // Flash timer for global shift light
    Timer {
        id: globalFlashTimer
        interval: mainWindow.activeShiftFlag ? mainWindow.activeShiftFlag.flashSpeed : 100
        running: mainWindow.activeShiftFlag !== null && mainWindow.activeShiftFlag.flash === true
        repeat: true
        onTriggered: {
            mainWindow.shiftLightFlashVisible = !mainWindow.shiftLightFlashVisible
        }
    }

    // Reset flash visibility when flag changes
    onActiveShiftFlagChanged: {
        shiftLightFlashVisible = true
        // Load flags on first activation if not loaded
        if (shiftLightFlags.length === 0) {
            loadShiftLightFlags()
        }
    }

}