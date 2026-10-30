import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Basic 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "." as App
import "BottomBarLayout.js" as BarLayout

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: bottomBar
    objectName: "bottomBar"
    property bool isVertical: settingsManager && settingsManager.bottomBarOrientation === "side"

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // What is playing and its controls, whichever source has them (the phone
    // while mirroring, else local media): nowPlaying (C++) decides and routes
    readonly property var _np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    readonly property bool useRemote: !!_np && _np.source !== "local"   // the phone or Android Auto
    readonly property string playIcon: (_np && _np.playing) ? "./assets/pause_button.svg" : "./assets/play_button.svg"
    function mediaPrevious() { if (_np) _np.previous() }
    function mediaTogglePlay() { if (_np) _np.togglePlay() }
    function mediaNext() { if (_np) _np.next() }
    // The phone's shuffle belongs to its app: nowPlaying ignores it; the
    // button is dimmed meanwhile
    function mediaToggleShuffle() { if (_np) _np.toggleShuffle() }

    readonly property bool isShuffleEnabled: !!_np && _np.shuffled

    // null at shutdown, when the managers go before the engine
    readonly property int volumePercent: volumeController ? volumeController.percent : 0
    readonly property string muteIcon: {
        if (!volumeController || volumeController.effectivelyMuted)
            return "./assets/mute_on.svg"
        if (volumePercent < 33) return "./assets/mute_off_low.svg"
        if (volumePercent < 66) return "./assets/mute_off_med.svg"
        return "./assets/mute_off_high.svg"
    }

    readonly property string clockTimeText: (typeof clock !== "undefined" && clock) ? clock.time : ""
    property bool clockSizeAuto: settingsManager ? settingsManager.clockSizeAuto : true
    property int manualClockSize: settingsManager ? settingsManager.clockSize : 18

    // ── Size and place ──
    // The bar is a band along the bottom (or the left, as a side bar). Its
    // thickness follows the window but stays within limits: never thinner
    // than a touchable minimum, never a fat slab on a portrait or ultrawide
    // screen. Everything inside is placed by BottomBarLayout.js from the
    // bar's own size, so nothing inside depends on the window directly.
    readonly property real _parentW: parent ? parent.width : 0
    readonly property real _parentH: parent ? parent.height : 0
    readonly property int thickness: isVertical
        ? Math.round(Math.max(Math.min(_parentW * 0.1, _parentH * 0.16),
                              Math.min(_parentW * 0.2, dpMin(72, 56))))
        : Math.round(Math.max(Math.min(_parentH * App.Spacing.bottomBarHeightPercent, _parentW * 0.09),
                              Math.min(_parentH * 0.2, dpMin(56, 44))))
    x: 0
    y: isVertical ? 0 : _parentH - height
    width: isVertical ? thickness : _parentW
    height: isVertical ? _parentH : thickness

    // Text is measured at 100 px and scaled: the layout needs width and
    // height per pixel of font size. Digits are measured as "8" so the clock
    // doesn't move the layout as the minutes change.
    TextMetrics {
        id: clockMetrics
        font.family: bottomBar.globalFont
        font.pixelSize: 100
        text: (bottomBar.clockTimeText || "88:88 PM").replace(/[0-9]/g, "8")
    }
    // How tall the digits stand, ink only: the auto clock matches it to the
    // nav icons, whatever the font's line height
    TextMetrics {
        id: clockDigitMetrics
        font.family: bottomBar.globalFont
        font.pixelSize: 100
        text: "8"
    }
    TextMetrics {
        id: volumeMetrics
        font.family: bottomBar.globalFont
        font.pixelSize: 100
        font.bold: true
        text: "100%"
    }

    // The nav buttons shown, in order
    readonly property var navSections: {
        var s = ["home", "obd", "media", "sensor", "settings"]
        if (settingsManager && settingsManager.androidAutoEnabled)   // Settings > Accessories > Phone Dock > Android Auto
            s.push("androidAuto")
        if (typeof phoneMirrorManager !== "undefined" && phoneMirrorManager !== null && phoneMirrorManager.nativeAvailable
                && settingsManager && settingsManager.phoneMirrorEnabled)
            s.push("phoneMirror")
        return s
    }

    readonly property var geo: BarLayout.compute({
        vertical: isVertical,
        width: width,
        height: height,
        navCount: navSections.length,
        showMedia: settingsManager ? settingsManager.showBottomBarMediaControls : true,
        showClock: settingsManager ? settingsManager.showClock : true,
        clockAuto: clockSizeAuto,
        clockPx: manualClockSize,
        clockWidthPerPx: clockMetrics.advanceWidth / 100,
        clockHeightPerPx: clockMetrics.height / 100,
        clockDigitHeightPerPx: clockDigitMetrics.tightBoundingRect.height / 100,
        volumeWidthPerPx: volumeMetrics.advanceWidth / 100,
        volumeHeightPerPx: volumeMetrics.height / 100,
        // Smallest nav button before controls are dropped: in proportion
        // to the window (not dp(), which follows the screen), never below a
        // fingertip
        minUnit: Math.max(24, 0.045 * Math.min(_parentW, _parentH)),
        scale: App.Spacing.globalScale
    })

    // Show a sensor page, returning true on success. openPage() logs every
    // failure — a silently-swallowed load error here reads to the user as "the
    // sensor button is dead" with nothing in the console to explain it.
    function _pushSensorPage(qmlFile) {
        return stackView.openPage(qmlFile) !== null
    }

    // Sensor nav button. Opens the last-visited sensor subpage when we have one,
    // falling back to SensorHome if that page can no longer load — e.g. CarMenu
    // needs the QtQuick3D module, which is an optional runtime dependency. The
    // stale preference is cleared so the shortcut doesn't dead-end again.
    function openSensorSection() {
        var currentItem = stackView.currentItem
        var name = currentItem ? currentItem.objectName : ""

        if (name === "sensorHome")
            return  // already on Sensor Home

        var isSensorSubpage = name.indexOf("sensor") === 0 || name === "carMenu"

        if (isSensorSubpage) {
            // On a sensor subpage — pop back to SensorHome
            while (stackView.depth > 1) {
                var item = stackView.currentItem
                if (item && item.objectName === "sensorHome")
                    return
                stackView.pop()
            }
            _pushSensorPage("SensorHome.qml")
            return
        }

        // From a non-sensor page — jump to the last-visited sensor subpage
        var lastPage = settingsManager ? settingsManager.get_setting_with_default("lastSensorPage", "") : ""
        if (lastPage !== "" && lastPage !== "SensorHome.qml") {
            if (_pushSensorPage(lastPage))
                return
            console.warn("[BottomBar] Clearing unusable lastSensorPage:", lastPage)
            if (settingsManager)
                settingsManager.save_setting("lastSensorPage", "")
        }
        _pushSensorPage("SensorHome.qml")
    }

    function persistentWikiSettings() {
        for (var i = stackView.depth - 1; i > 0; --i) {
            var item = stackView.get(i)
            if (item && item.objectName === "settingsMenu"
                    && item.activeWikiPopup && item.activeWikiPopup.opened)
                return item
        }
        return null
    }

    function openHomeSection() {
        var savedSettings = persistentWikiSettings()
        if (!savedSettings) {
            while (stackView.depth > 1)
                stackView.pop()
            return
        }

        // Reuse a Home page already above the preserved Settings page.
        for (var i = stackView.depth - 1; i > 0; --i) {
            var item = stackView.get(i)
            if (item === savedSettings)
                break
            if (item && item.objectName === "mainMenu") {
                if (stackView.currentItem !== item)
                    stackView.pop(item)
                return
            }
        }

        stackView.openPage("MainMenu.qml", {
            mainWindow: bottomBar.mainWindow,
            windowWidth: bottomBar.mainWindow.width,
            windowHeight: bottomBar.mainWindow.height
        })
    }

    function openSettingsSection() {
        var currentItem = stackView.currentItem
        if (currentItem && currentItem.objectName === "settingsMenu") {
            if (typeof currentItem.handleSettingsButton === "function")
                currentItem.handleSettingsButton()
            else if (typeof currentItem.navigateToHub === "function")
                currentItem.navigateToHub()
            return
        }

        var savedSettings = persistentWikiSettings()
        if (savedSettings) {
            stackView.pop(savedSettings)
            return
        }

        while (stackView.depth > 1)
            stackView.pop()
        stackView.openPage("SettingsMenu.qml", { initialSection: lastSettingsSection })
    }

    // OBD nav button. On an OBD subpage it steps back to OBD Home; from
    // anywhere else it jumps to the last-visited OBD page (falling back to
    // OBD Home when that page can no longer load). With no adapter connected
    // it always goes to OBD Home, which is then the connect prompt.
    function openOBDSection() {
        var currentItem = stackView.currentItem
        var name = currentItem ? currentItem.objectName : ""
        var connected = typeof obdManager !== "undefined" && obdManager && obdManager.connected === true

        if (name === "obdHome")
            return  // already on OBD Home

        if (name.indexOf("obd") === 0) {
            while (stackView.depth > 1) {
                var item = stackView.currentItem
                if (item && item.objectName === "obdHome")
                    return
                stackView.pop()
            }
            stackView.openPage("OBDHome.qml")
            return
        }

        var lastPage = connected && settingsManager ? settingsManager.get_setting_with_default("lastOBDPage", "") : ""
        if (lastPage !== "" && stackView.openPage(lastPage))
            return
        stackView.openPage("OBDHome.qml")
    }

    // Media nav button toggles between MediaRoom and MediaPlayer when on
    // either; from anywhere else it opens the user's chosen default page.
    function openMediaSection() {
        var currentItem = stackView.currentItem
        var name = currentItem ? currentItem.objectName : ""

        if (name === "mediaRoom") {
            stackView.openPage("MediaPlayer.qml")
        } else if (name === "mediaPlayer") {
            stackView.openPage("MediaRoom.qml")
        } else {
            var defaultPage = settingsManager ? settingsManager.musicButtonDefaultPage : "mediaRoom"
            stackView.openPage(defaultPage === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml")
        }
    }

    // Both mirror views launch/reattach their stream in StackView.onActivated,
    // so re-showing the cached instance is all a press has to do.
    function openAndroidAutoSection() {
        stackView.openPage("AndroidAutoView.qml")
    }

    // Pressed again while the mirror is on screen and streaming, the button
    // is the phone's Home button.
    function openPhoneMirrorSection() {
        // An iPhone mirroring over AirPlay is the phone on screen
        if (typeof airPlayManager !== "undefined" && airPlayManager.mirroring) {
            stackView.openPage("AirPlayView.qml")
            return
        }
        var currentItem = stackView.currentItem
        if (currentItem && currentItem.objectName === "phoneMirrorView"
                && currentItem.mirrorRunning && phoneMirrorManager) {
            phoneMirrorManager.pressHome()
            return
        }
        stackView.openPage("PhoneMirrorView.qml")
    }

    // Tapping the clock opens the Clock menu.
    function openClockSection() {
        stackView.openPage("ClockMenu.qml")
    }

    // What each nav button shows and does
    readonly property var navButtons: ({
        home:        { icon: "./assets/home_button.svg",          open: openHomeSection },
        obd:         { icon: "./assets/obd_button.svg",           open: openOBDSection },
        media:       { icon: "./assets/media_button.svg",         open: openMediaSection },
        sensor:      { icon: "./assets/sensor_button.svg",        open: openSensorSection },
        settings:    { icon: "./assets/settings_button.svg",      open: openSettingsSection },
        androidAuto: { icon: "./assets/android_auto_button.svg",  open: openAndroidAutoSection },
        phoneMirror: { icon: "./assets/phone_mirror_button.svg",  open: openPhoneMirrorSection }
    })
    function navColor(section) {
        switch (section) {
        case "home": return App.Style.bottomBarHomeButton
        case "obd": return App.Style.bottomBarOBDButton
        case "media": return App.Style.bottomBarMediaButton
        case "sensor": return App.Style.bottomBarSensorButton
        case "settings": return App.Style.bottomBarSettingsButton
        case "androidAuto": return App.Style.bottomBarAndroidAutoButton
        case "phoneMirror": return App.Style.bottomBarPhoneMirrorButton
        }
        return App.Style.accent
    }

    gradient: Gradient {
        orientation: isVertical ? Gradient.Horizontal : Gradient.Vertical
        GradientStop { position: 1.5; color: App.Style.bottomBarGradientStart }
        GradientStop { position: 0.0; color: App.Style.bottomBarGradientEnd }
    }

    // Which nav button owns the page on top of the stack ("" = none). Drives
    // the highlight plate behind that button.
    readonly property string activeSection: {
        var item = stackView ? stackView.currentItem : null
        var n = item ? item.objectName : ""
        if (n === "mainMenu") return "home"
        if (n.indexOf("obd") === 0) return "obd"
        if (n === "mediaRoom" || n === "mediaPlayer") return "media"
        if (n.indexOf("sensor") === 0 || n === "carMenu") return "sensor"
        if (n === "settingsMenu") return "settings"
        if (n === "androidAutoView") return "androidAuto"
        if (n === "phoneMirrorView" || n === "airPlayView") return "phoneMirror"
        return ""
    }

    // Nav presses run a frame late. The press handler is where a page may
    // still have to be built (a tap before pre-building finishes can take
    // 30-350 ms), and a press and release both handled before the next frame
    // meant the button's squish never reached the screen. Waiting until the
    // squish has been synced to the render thread (see PressBounce) lets it
    // keep animating through any page work; the cost is about two frames
    // (~30 ms) of latency.
    property var _pendingNav: null
    property int _navFramesLeft: 0
    function navAfterFrame(fn) {
        _pendingNav = fn
        // Two swaps: the first may be a frame synced before the press.
        _navFramesLeft = 2
        navFrameFallback.restart()
    }
    function _runPendingNav() {
        navFrameFallback.stop()
        var fn = _pendingNav
        _pendingNav = null
        if (fn)
            fn()
    }
    Connections {
        target: bottomBar.mainWindow
        enabled: bottomBar._pendingNav !== null
        function onFrameSwapped() {
            if (--bottomBar._navFramesLeft <= 0)
                bottomBar._runPendingNav()
        }
    }
    // In case no frame comes (nothing on screen changed).
    Timer { id: navFrameFallback; interval: 80; onTriggered: bottomBar._runPendingNav() }

    // Tap feedback for the nav buttons: squish, then spring back. Built only
    // from Animators, so the whole bounce runs on the scene graph's render
    // thread and keeps moving while the GUI thread is busy switching pages.
    // (A Behavior/NumberAnimation on `pressed` runs on the GUI thread and froze
    // whenever a page switch did real work.) It plays in full on every tap,
    // however short, instead of tracking how long the finger stays down.
    component PressBounce: ParallelAnimation {
        id: bounce
        required property Item target
        SequentialAnimation {
            ScaleAnimator { target: bounce.target; from: 1.0; to: 0.8; duration: 90; easing.type: Easing.OutQuad }
            ScaleAnimator { target: bounce.target; from: 0.8; to: 1.0; duration: 220; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
        }
        SequentialAnimation {
            OpacityAnimator { target: bounce.target; from: 1.0; to: 0.7; duration: 90 }
            OpacityAnimator { target: bounce.target; from: 0.7; to: 1.0; duration: 150 }
        }
    }

    // An SVG icon drawn in one colour
    component TintedIcon: Item {
        id: tinted
        property url source
        property color color
        Image {
            id: tintedImage
            anchors.fill: parent
            source: tinted.source
            sourceSize: Qt.size(Math.max(1, width * 2), Math.max(1, height * 2))
            fillMode: Image.PreserveAspectFit
            smooth: true
            antialiasing: true
            mipmap: true
            visible: false
        }
        ColorOverlay {
            anchors.fill: tintedImage
            source: tintedImage
            color: tinted.color
        }
    }

    // One control: the item spans its touch area (geo.hits), the face inside
    // it is the drawn part (geo.items). The layout guarantees neither
    // overlaps a neighbour's.
    component BarSlot: Item {
        id: slot
        required property var face
        required property var hit
        readonly property alias faceItem: faceItem
        default property alias content: faceItem.data
        visible: face.visible
        x: hit.x
        y: hit.y
        width: hit.w
        height: hit.h
        Item {
            id: faceItem
            x: slot.face.x - slot.hit.x
            y: slot.face.y - slot.hit.y
            width: slot.face.w
            height: slot.face.h
        }
    }

    // A media control: drawn as a nav button is (outline, icon at 70 %),
    // squishes while pressed, acts on release
    component MediaButton: BarSlot {
        id: mb
        property url source
        property color color
        property bool toggled: false
        property real restOpacity: 1.0
        signal tapped()

        Item {
            anchors.fill: parent
            scale: area.pressed ? 0.8 : 1.0
            opacity: area.pressed ? 0.7 : mb.restOpacity
            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
            Behavior on opacity { NumberAnimation { duration: 150 } }

            Rectangle {
                anchors.fill: parent
                radius: Math.min(width, height) * 0.14  // as the nav buttons
                color: mb.toggled ? App.Style.bottomBarToggleShade : "transparent"
                border.color: App.Style.accent
                border.width: 1
            }
            TintedIcon {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height) * 0.7
                height: width
                source: mb.source
                color: mb.color
            }
        }
        MouseArea {
            id: area
            parent: mb
            anchors.fill: parent
            onClicked: mb.tapped()
        }
    }

    signal clicked()

    MouseArea {
        anchors.fill: parent
        onClicked: function(mouse) {
            bottomBar.clicked()
            mouse.accepted = false // Allow clicks to pass through
        }
        z: -1 // Put behind other controls
    }

    required property StackView stackView
    required property Window mainWindow

    // ── Media controls ──
    MediaButton {
        face: bottomBar.geo.items.prev; hit: bottomBar.geo.hits.prev
        source: "./assets/previous_button.svg"
        color: App.Style.bottomBarPreviousButton
        onTapped: bottomBar.mediaPrevious()
    }
    MediaButton {
        face: bottomBar.geo.items.play; hit: bottomBar.geo.hits.play
        source: bottomBar.playIcon
        color: App.Style.bottomBarPlayButton
        onTapped: bottomBar.mediaTogglePlay()
    }
    MediaButton {
        face: bottomBar.geo.items.next; hit: bottomBar.geo.hits.next
        source: "./assets/next_button.svg"
        color: App.Style.bottomBarNextButton
        onTapped: bottomBar.mediaNext()
    }
    MediaButton {
        face: bottomBar.geo.items.shuffle; hit: bottomBar.geo.hits.shuffle
        source: "./assets/shuffle_button.svg"
        color: bottomBar.isShuffleEnabled ? App.Style.bottomBarActiveToggleButton : App.Style.bottomBarShuffleButton
        toggled: bottomBar.isShuffleEnabled
        restOpacity: bottomBar.useRemote ? 0.35 : 1.0
        onTapped: bottomBar.mediaToggleShuffle()
    }
    MediaButton {
        face: bottomBar.geo.items.mute; hit: bottomBar.geo.hits.mute
        source: bottomBar.muteIcon
        color: App.Style.bottomBarVolumeButton
        // At 0 % this restores the startup volume instead
        onTapped: volumeController.toggleMute()
    }
    BarSlot {
        id: volumeSlot
        face: bottomBar.geo.items.volume; hit: bottomBar.geo.hits.volume
        Text {
            anchors.centerIn: parent
            text: bottomBar.volumePercent + "%"
            color: App.Style.primaryTextColor
            font.pixelSize: bottomBar.geo.volumePx
            font.bold: true
            font.family: bottomBar.globalFont
        }
        MouseArea {
            parent: volumeSlot
            anchors.fill: parent
            onClicked: popupSlider.open()
        }
    }

    // ── Nav buttons ──
    Repeater {
        model: bottomBar.navSections

        BarSlot {
            id: navSlot
            required property string modelData
            required property int index
            readonly property bool active: bottomBar.activeSection === modelData
            face: bottomBar.geo.nav[index] || ({ x: 0, y: 0, w: 0, h: 0, visible: false })
            hit: bottomBar.geo.navHits[index] || ({ x: 0, y: 0, w: 0, h: 0, visible: false })

            // Accent-tinted plate behind the active button: grows out from
            // the button's centre when it becomes active, shrinks back when
            // it doesn't. Outside the face so the press bounce leaves it be.
            Rectangle {
                parent: navSlot
                z: -1
                x: navSlot.faceItem.x; y: navSlot.faceItem.y
                width: navSlot.faceItem.width; height: navSlot.faceItem.height
                radius: Math.min(width, height) * 0.14  // matches the outline
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
                scale: navSlot.active ? 1 : 0
                opacity: navSlot.active ? 1 : 0
                visible: opacity > 0
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: Math.min(width, height) * 0.14
                border.color: App.Style.accent
                border.width: 1
            }
            TintedIcon {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height) * 0.7
                height: width
                source: bottomBar.navButtons[navSlot.modelData].icon
                color: bottomBar.navColor(navSlot.modelData)
            }

            // Settings: update available
            Rectangle {
                visible: navSlot.modelData === "settings" && networkManager && networkManager.updateStatus === "update-available"
                width: Math.max(6, Math.min(parent.width, parent.height) * 0.14)
                height: width
                radius: width / 2
                color: "#FF9800"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -width / 8
                anchors.rightMargin: -width / 8

                SequentialAnimation on opacity {
                    running: navSlot.modelData === "settings" && networkManager && networkManager.updateStatus === "update-available"
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                }
            }

            PressBounce { id: navBounce; target: navSlot.faceItem }

            MouseArea {
                parent: navSlot
                anchors.fill: parent
                onPressAndHold: if (navSlot.modelData === "settings") settingsVisibilityPopup.open()
                onPressed: {
                    navBounce.restart()
                    bottomBar.navAfterFrame(bottomBar.navButtons[navSlot.modelData].open)
                }
            }
        }
    }

    // ── Clock ──
    BarSlot {
        id: clockSlot
        face: bottomBar.geo.items.clock; hit: bottomBar.geo.hits.clock
        Rectangle {
            anchors.fill: parent
            color: clockArea.pressed ? App.Style.hoverColor : "transparent"
            radius: Math.min(width, height) * 0.14
        }
        Text {
            anchors.centerIn: parent
            font.pixelSize: bottomBar.geo.clockPx
            font.family: bottomBar.globalFont
            color: App.Style.clockTextColor
            text: bottomBar.clockTimeText
        }
        MouseArea {
            id: clockArea
            parent: clockSlot
            anchors.fill: parent
            onPressed: bottomBar.navAfterFrame(bottomBar.openClockSection)
        }
    }

    // ── Volume popup ── opened by tapping the volume %
    Popup {
        id: popupSlider
        parent: Overlay.overlay
        width: parent ? parent.width : 0
        height: Math.min(dp(140), parent ? parent.height * 0.5 : dp(140))
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: App.Style.backgroundColor
            radius: dpMin(8, 2)
            border.color: App.Style.accent
            border.width: 1
        }

        contentItem: Item {
            Label {
                id: percentLabel
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: dp(15)
                text: Math.round(volumeSlider.value) + "%"
                color: App.Style.accent
                font.pixelSize: dp(32)
                font.bold: true
            }

            Item {
                id: sliderContainer
                anchors.top: percentLabel.bottom
                anchors.topMargin: dp(15)
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: dp(15)

                Slider {
                    id: volumeSlider
                    anchors.centerIn: parent
                    width: parent.width * 0.95
                    height: dp(50)
                    from: 0
                    to: 100
                    stepSize: 1
                    value: bottomBar.volumePercent

                    background: Rectangle {
                        x: volumeSlider.leftPadding
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                        width: volumeSlider.availableWidth
                        height: dp(16)
                        radius: dpMin(8, 2)
                        color: App.Style.hoverColor

                        Rectangle {
                            // Ends at the handle's center so the rounded end stays under it
                            width: volumeSlider.visualPosition * (parent.width - volumeSlider.handle.width) + volumeSlider.handle.width / 2
                            height: parent.height
                            color: App.Style.volumeSliderColor
                            radius: dpMin(8, 2)
                        }
                    }

                    // Larger handle for better touch
                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                        width: dp(40)
                        height: dp(40)
                        radius: width / 2
                        color: App.Style.accent
                    }

                    onValueChanged: {
                        // Single dispatch to settings + every audio output;
                        // raising it while muted unmutes (VolumeController)
                        // null at shutdown: the binding to percent drops to 0 as the managers go
                        if (volumeController)
                            volumeController.applyVolume(Math.round(value))
                    }
                }

                // Touch areas along the track, so a drag that starts a little
                // off the slider still moves it
                Repeater {
                    model: 9
                    MouseArea {
                        x: (index * (volumeSlider.width / 8)) - width / 2 + volumeSlider.x
                        y: volumeSlider.y + volumeSlider.height / 2 - height / 2
                        width: dp(80)
                        height: dp(80)
                        preventStealing: false
                        onMouseXChanged: {
                            if (pressed) {
                                var sliderX = mapToItem(volumeSlider, mouseX, mouseY).x
                                volumeSlider.value = Math.max(0, Math.min(1,
                                    (sliderX - volumeSlider.leftPadding) / volumeSlider.availableWidth)) * 100
                            }
                        }
                    }
                }
            }
        }
    }

    // Volume set anywhere else (knob, gesture, media room): the slider's own
    // writes break its binding, so follow along here
    Connections {
        target: volumeController
        function onPercentChanged(percent) {
            if (!volumeSlider.pressed)
                volumeSlider.value = percent
        }
    }

    // Settings Visibility Popup - opened by press-and-hold on the settings button
    // Defined at bottomBar level so both horizontal and vertical layouts can access it
    Popup {
        id: settingsVisibilityPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: parent.width * 0.5
        height: parent.height * 0.85
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: App.Style.contentColor
            radius: App.Spacing.overallMargin
            border.color: App.Style.accent
            border.width: 2

            layer.enabled: true
            layer.effect: DropShadow {
                horizontalOffset: 0
                verticalOffset: 6
                radius: 30
                samples: 31
                color: "#90000000"
            }
        }

        contentItem: Item {
            // Header at top
            Text {
                id: popupHeader
                text: "Toggle View"
                font.pixelSize: App.Spacing.overallText * 2.5
                font.bold: true
                font.family: bottomBar.globalFont
                color: App.Style.primaryTextColor
                anchors.top: parent.top
                anchors.topMargin: App.Spacing.overallMargin
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Close button at bottom
            Rectangle {
                id: closeBtn
                width: App.Spacing.overallText * 10
                height: App.Spacing.overallText * 3
                radius: App.Spacing.overallMargin * 0.5
                color: closeButtonMouse.pressed ? Qt.darker(App.Style.accent, 1.2) :
                       closeButtonMouse.containsMouse ? Qt.lighter(App.Style.accent, 1.1) : App.Style.accent
                anchors.bottom: parent.bottom
                anchors.bottomMargin: App.Spacing.overallMargin
                anchors.horizontalCenter: parent.horizontalCenter

                Behavior on color { ColorAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    text: "Close"
                    font.pixelSize: App.Spacing.overallText * 1.4
                    font.bold: true
                    font.family: bottomBar.globalFont
                    color: "white"
                }

                MouseArea {
                    id: closeButtonMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: settingsVisibilityPopup.close()
                }
            }

            // Toggle list in middle
            Flickable {
                id: toggleScrollView
                anchors.top: popupHeader.bottom
                anchors.topMargin: App.Spacing.overallSpacing
                anchors.bottom: closeBtn.top
                anchors.bottomMargin: App.Spacing.overallSpacing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: App.Spacing.overallMargin
                anchors.rightMargin: App.Spacing.overallMargin
                clip: true
                contentHeight: toggleColumn.height
                boundsBehavior: Flickable.DragAndOvershootBounds
                flickDeceleration: 1200
                maximumFlickVelocity: 4000

                Column {
                    id: toggleColumn
                    width: toggleScrollView.width
                    spacing: App.Spacing.overallSpacing * 0.5

                    Repeater {
                        model: [
                            { name: "Device", section: "deviceSettings" },
                            { name: "Media", section: "mediaSettings" },
                            { name: "Display", section: "displaySettings" },
                            { name: "OBD", section: "obdSettings" },
                            { name: "Accessories", section: "accessoriesSettings" },
                            { name: "About", section: "about" }
                        ]

                        delegate: Rectangle {
                            id: toggleRow
                            width: toggleScrollView.width
                            height: App.Spacing.overallText * 4
                            radius: App.Spacing.overallMargin * 0.5
                            color: toggleMouseArea.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                            property bool isChecked: settingsManager ? settingsManager.is_settings_section_visible(modelData.section) : true

                            Connections {
                                target: settingsManager
                                function onSettingsMenuVisibilityChanged() {
                                    toggleRow.isChecked = settingsManager.is_settings_section_visible(modelData.section)
                                }
                            }

                            MouseArea {
                                id: toggleMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var newState = !toggleRow.isChecked
                                    toggleRow.isChecked = newState
                                    if (settingsManager) {
                                        settingsManager.save_settings_section_visibility(modelData.section, newState)
                                    }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: App.Spacing.overallMargin
                                anchors.rightMargin: App.Spacing.overallMargin
                                spacing: App.Spacing.overallSpacing

                                Text {
                                    text: modelData.name
                                    font.pixelSize: App.Spacing.overallText * 1.6
                                    font.family: bottomBar.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    id: toggleTrack
                                    Layout.preferredWidth: App.Spacing.overallText * 5
                                    Layout.preferredHeight: App.Spacing.overallText * 2.5
                                    radius: height / 2
                                    color: toggleRow.isChecked ?
                                        Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.6) :
                                        Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.4)

                                    Behavior on color { ColorAnimation { duration: 200 } }

                                    Rectangle {
                                        id: toggleHandle
                                        width: parent.height - 8
                                        height: parent.height - 8
                                        radius: height / 2
                                        x: toggleRow.isChecked ? parent.width - width - 4 : 4
                                        y: 4
                                        color: "white"

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width * 0.45
                                            height: width
                                            radius: width / 2
                                            color: App.Style.accent
                                            opacity: toggleRow.isChecked ? 1 : 0
                                            scale: toggleRow.isChecked ? 1 : 0.5

                                            Behavior on opacity { NumberAnimation { duration: 200 } }
                                            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                        }

                                        layer.enabled: true
                                        layer.effect: DropShadow {
                                            verticalOffset: 2
                                            radius: 6
                                            samples: 13
                                            color: "#40000000"
                                        }

                                        Behavior on x {
                                            NumberAnimation {
                                                duration: 250
                                                easing.type: Easing.OutBack
                                                easing.overshoot: 0.8
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
