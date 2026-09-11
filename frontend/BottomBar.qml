import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import Qt5Compat.GraphicalEffects
import "." as App

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: bottomBar
    property bool isVertical: settingsManager && settingsManager.bottomBarOrientation === "side"

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Spotify integration - use Spotify when user chooses it AND it's connected
    property bool useSpotify: settingsManager && settingsManager.mediaSource === "spotify" &&
                              spotifyManager && spotifyManager.is_connected()

    // Shared shuffle state - single source of truth for all shuffle buttons
    property bool isShuffleEnabled: false

    // Clock text shared by both layouts. Held at bottomBar level (rather than
    // set on each layout's Text from its own Connections) so the layout that
    // is loaded on an orientation switch shows the current time immediately
    // instead of staying blank until the clock next emits — up to a minute
    // when seconds are hidden.
    property string clockTimeText: ""

    Connections {
        target: clock
        function onTimeChanged(time) {
            bottomBar.clockTimeText = time
        }
    }

    Component.onCompleted: {
        updateLayout()
        // Initialize shuffle state from the appropriate source
        if (useSpotify && spotifyManager) {
            isShuffleEnabled = spotifyManager.is_shuffled()
        } else if (mediaManager) {
            isShuffleEnabled = mediaManager.is_shuffled()
        }
    }
    
    // Build + push a sensor page, returning true on success. Every failure is
    // logged — a silently-swallowed load error here reads to the user as "the
    // sensor button is dead" with nothing in the console to explain it.
    function _pushSensorPage(qmlFile) {
        var component = Qt.createComponent(qmlFile)
        if (component.status !== Component.Ready) {
            console.warn("[BottomBar] Failed to load", qmlFile, "-", component.errorString())
            return false
        }
        var page = component.createObject(stackView, {
            stackView: bottomBar.stackView,
            mainWindow: stackView.parent.Window.window
        })
        if (!page) {
            console.warn("[BottomBar] Failed to instantiate", qmlFile)
            return false
        }
        stackView.push(page)
        return true
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

    function updateLayout() {
        if (isVertical) {
            // Release anchors first to prevent binding loops
            anchors.bottom = undefined
            anchors.right = undefined
            
            // Set anchors for vertical layout
            anchors.left = parent.left
            anchors.top = parent.top
            
            // Explicitly set width and height as percentages
            width = parent.width * 0.1
            height = parent.height
        } else {
            // Release anchors first
            anchors.top = undefined
            anchors.right = undefined
            
            // Set anchors for horizontal layout
            anchors.left = parent.left
            anchors.bottom = parent.bottom
            
            // Explicitly set width and height
            width = parent.width
            height = parent.height * App.Spacing.bottomBarHeightPercent
        }
    }
    
    // Watch for settings changes
    Connections {
        target: settingsManager
        function onBottomBarOrientationChanged() {
            updateLayout()
        }
    }
    
    // Watch for isVertical changes
    onIsVerticalChanged: {
        updateLayout()
    }
    
    // Explicitly handle parent size changes
    Connections {
        target: parent
        function onWidthChanged() {
            if (isVertical) {
                width = parent.width * 0.1
            } else {
                width = parent.width
            }
        }
        function onHeightChanged() {
            if (isVertical) {
                height = parent.height
            } else {
                height = parent.height * App.Spacing.bottomBarHeightPercent
            }
        }
    }
    
    gradient: Gradient {
        orientation: isVertical ? Gradient.Horizontal : Gradient.Vertical
        GradientStop { position: 1.5; color: App.Style.bottomBarGradientStart }
        GradientStop { position: 0.0; color: App.Style.bottomBarGradientEnd }
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

    Loader {
        anchors.fill: parent
        sourceComponent: isVertical ? verticalLayoutComponent : horizontalLayoutComponent

        Component { //horizontal 
            id: horizontalLayoutComponent

            RowLayout { // Main layout that divides the bar into three sections with equal spacing
                anchors.fill: parent
                spacing: 0 // We'll handle spacing within each section
                

                Item { // SECTION 1: Left section - Media Controls
                    id: mediaControlsSection
                    property bool showControls: settingsManager ? settingsManager.showBottomBarMediaControls : true
                    // When hidden, mirror the clock section's width so nav buttons sit at the true horizontal center
                    Layout.preferredWidth: showControls ? parent.width * 0.4 : clockSection.width
                    Layout.fillHeight: true
                    // Keep item visible to preserve space, just hide the contents

                    RowLayout {
                        id: mediaControls
                        visible: mediaControlsSection.showControls
                        anchors {
                            left: parent.left
                            leftMargin: App.Spacing.overallMargin
                            right: parent.right
                            rightMargin: App.Spacing.overallMargin
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: App.Spacing.bottomBarBetweenButtonMargin

                        // Previous button
                        Control {
                            id: previousButtonControl
                            implicitWidth: App.Spacing.bottomBarPreviousButtonWidth
                            implicitHeight: App.Spacing.bottomBarPreviousButtonHeight
                            Layout.alignment: Qt.AlignVCenter
                            
                            scale: mouseAreaPrev.pressed ? 0.8 : 1.0
                            opacity: mouseAreaPrev.pressed ? 0.7 : 1.0
                            
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack  
                                    easing.overshoot: 1.1
                                }
                            }
                            
                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle { color: "transparent" }
                            
                            contentItem: Item {
                                Image {
                                    id: previousButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.height
                                    source: "./assets/previous_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    id: colorOverlay
                                    anchors.fill: previousButtonImage
                                    source: previousButtonImage
                                    color: App.Style.bottomBarPreviousButton
                                }
                            }
                            Item {
                                id: previousButtonClickArea
                                anchors.centerIn: parent
                                width: parent.width * 2.5
                                height: parent.height * 3.0

                                MouseArea {
                                    id: mouseAreaPrev
                                    anchors.fill: parent
                                    onClicked: useSpotify ? spotifyManager.previous_track() : mediaManager.previous_track()
                                }
                            }
                        }

                        // Play/Pause button
                        Control {
                            id: playButtonControl
                            implicitWidth: App.Spacing.bottomBarPreviousButtonWidth
                            implicitHeight: App.Spacing.bottomBarPreviousButtonHeight
                            Layout.alignment: Qt.AlignVCenter
                            clip: false

                            scale: mouseAreaPlay.pressed ? 0.8 : 1.0
                            opacity: mouseAreaPlay.pressed ? 0.7 : 1.0

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack
                                    easing.overshoot: 1.1
                                }
                            }

                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle { color: "transparent" }

                            contentItem: Item {
                                clip: false
                                Image {
                                    id: playButtonImage
                                    anchors.centerIn: parent
                                    width: App.Spacing.bottomBarPlayButtonWidth
                                    height: App.Spacing.bottomBarPlayButtonHeight
                                    source: {
                                        var isPlaying = useSpotify ?
                                            (spotifyManager && spotifyManager.is_playing()) :
                                            (mediaManager && mediaManager.is_playing())
                                        return isPlaying ? "./assets/pause_button.svg" : "./assets/play_button.svg"
                                    }
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: playButtonImage
                                    source: playButtonImage
                                    color: App.Style.bottomBarPlayButton
                                }
                            }
                            Item {
                                id: playButtonClickArea
                                anchors.centerIn: parent
                                width: App.Spacing.bottomBarPlayButtonWidth * 1.5
                                height: App.Spacing.bottomBarPlayButtonHeight * 2.5

                                MouseArea {
                                    id: mouseAreaPlay
                                    anchors.fill: parent
                                    onClicked: useSpotify ? spotifyManager.toggle_play() : mediaManager.toggle_play()
                                }
                            }
                        }

                        // Next button
                        Control {
                            id: nextButtonControl
                            implicitWidth: App.Spacing.bottomBarNextButtonWidth
                            implicitHeight: App.Spacing.bottomBarNextButtonHeight
                            Layout.alignment: Qt.AlignVCenter

                            scale: mouseAreaNext.pressed ? 0.8 : 1.0
                            opacity: mouseAreaNext.pressed ? 0.7 : 1.0
                            
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack  
                                    easing.overshoot: 1.1
                                }
                            }
                            
                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }
                            
                            background: Rectangle { color: "transparent" }
                            
                            contentItem: Item {
                                Image {
                                    id: nextButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.height
                                    source: "./assets/next_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: nextButtonImage
                                    source: nextButtonImage
                                    color: App.Style.bottomBarNextButton
                                }
                            }
                            Item {
                                id: nextButtonClickArea
                                anchors.centerIn: parent
                                width: parent.width * 2.5
                                height: parent.height * 3.0

                                MouseArea {
                                    id: mouseAreaNext
                                    anchors.fill: parent
                                    onClicked: useSpotify ? spotifyManager.next_track() : mediaManager.next_track()
                                }
                            }
                        }

                        // Shuffle button
                        Control {
                            id: shuffleButton
                            implicitWidth: App.Spacing.bottomBarShuffleButtonWidth
                            implicitHeight: App.Spacing.bottomBarShuffleButtonHeight
                            Layout.alignment: Qt.AlignVCenter

                            scale: mouseAreaShuffle.pressed ? 0.8 : 1.0
                            opacity: mouseAreaShuffle.pressed ? 0.7 : 1.0
                            
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack  
                                    easing.overshoot: 1.1
                                }
                            }
                            
                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle {
                                color: bottomBar.isShuffleEnabled ? App.Style.bottomBarToggleShade : "transparent"
                                radius: 4
                            }

                            contentItem: Item {
                                Image {
                                    id: shuffleButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/shuffle_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: shuffleButtonImage
                                    source: shuffleButtonImage
                                    color: bottomBar.isShuffleEnabled ?
                                        App.Style.bottomBarActiveToggleButton :
                                        App.Style.bottomBarShuffleButton
                                }
                            }

                            Item {
                                id: shuffleButtonClickArea
                                anchors.centerIn: parent
                                width: parent.width * 1.5
                                height: parent.height * 2.5

                                MouseArea {
                                    id: mouseAreaShuffle
                                    anchors.fill: parent
                                    onClicked: {
                                        if (useSpotify) {
                                            spotifyManager.toggle_shuffle()
                                        } else {
                                            mediaManager.toggle_shuffle()
                                        }
                                    }
                                }
                            }
                        }

                        // Mute button
                        Control {
                            id: muteButton
                            property bool isMuted: false
                            implicitWidth: App.Spacing.bottomBarMuteButtonWidth
                            implicitHeight: App.Spacing.bottomBarMuteButtonHeight
                            Layout.alignment: Qt.AlignVCenter

                            scale: mouseAreaMute.pressed ? 0.8 : 1.0
                            opacity: mouseAreaMute.pressed ? 0.7 : 1.0
                            
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack  
                                    easing.overshoot: 1.1
                                }
                            }
                            
                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle { color: "transparent" }
                            
                            contentItem: Item {
                                Image {
                                    id: muteButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.height
                                    source: getUpdatedMuteSource()
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: muteButtonImage
                                    source: muteButtonImage
                                    color: App.Style.bottomBarVolumeButton
                                }
                            }
                            Item {
                                id: muteButtonClickArea
                                anchors.centerIn: parent
                                width: parent.width * 1.5
                                height: parent.height * 2.5
                            
                                MouseArea {
                                    id: mouseAreaMute
                                    anchors.fill: parent
                                    onClicked: {
                                        // If volume is 0, restore to startup volume instead of toggling mute
                                        if (volumeControl.currentValue === 0 && settingsManager) {
                                            var startupVol = Math.round(settingsManager.startUpVolume * 100)
                                            volumeSlider.value = startupVol
                                        } else {
                                            mediaManager.toggle_mute()
                                        }
                                    }
                                }
                            }
                        }

                        // Volume Control
                        Control {
                            id: volumeControl
                            implicitWidth: App.Spacing.bottomBarVolumeSliderWidth
                            implicitHeight: App.Spacing.bottomBarVolumeSliderHeight
                            Layout.alignment: Qt.AlignVCenter
                            
                            Text {
                                id: volumeText
                                anchors {
                                    left: parent.left
                                    leftMargin: App.Spacing.overallMargin
                                    verticalCenter: parent.verticalCenter
                                }
                                text: volumeControl.currentValue + "%"
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.bottomBarVolumeText
                                font.bold: true
                                font.family: bottomBar.globalFont
                            }
                            
                            property int currentValue: 0

                            Component.onCompleted: {
                                if (settingsManager) {
                                    currentValue = settingsManager.currentVolume
                                    // Route startup volume to every audio output via
                                    // the shared VolumeController (see backend/volume_utils.py).
                                    volumeController.applyVolume(currentValue)
                                } else {
                                    currentValue = 10
                                    mediaManager.setVolume(0.1)
                                }

                                updateMuteButtonImage()

                                if (mediaManager) {
                                    bottomBar.isShuffleEnabled = mediaManager.is_shuffled()
                                }
                            }
                            Item {
                                anchors.centerIn: parent
                                width: parent.width * 1.5
                                height: parent.height * 3.5

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: popupSlider.open()
                                }
                            }

                            Popup {
                                id: popupSlider
                                width: parent.Window.width
                                height: dp(140)
                                anchors.centerIn: Overlay.overlay
                                modal: true
                                focus: true
                                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                // Simple background
                                background: Rectangle {
                                    color: App.Style.backgroundColor
                                    radius: dpMin(8, 2)
                                    border.color: App.Style.accent
                                    border.width: 1
                                }

                                // Content with centered slider
                                contentItem: Item {
                                    anchors.fill: parent

                                    // Larger percentage display
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

                                    // Container for the slider - centered in the popup
                                    Item {
                                        id: sliderContainer
                                        anchors.top: percentLabel.bottom
                                        anchors.topMargin: dp(15)
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: dp(15)

                                        // Simplified slider
                                        Slider {
                                            id: volumeSlider
                                            anchors.centerIn: parent
                                            width: parent.width * 0.95
                                            height: dp(50)
                                            from: 0
                                            to: 100
                                            stepSize: 1
                                            value: volumeControl.currentValue
                                            
                                            // Simple slider background
                                            background: Rectangle {
                                                x: volumeSlider.leftPadding
                                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                                width: volumeSlider.availableWidth
                                                height: dp(16)
                                                radius: dpMin(8, 2)
                                                color: App.Style.hoverColor

                                                // Filled portion
                                                Rectangle {
                                                    width: volumeSlider.visualPosition * parent.width
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
                                                radius: dpMin(20, 2)  // Circular handle
                                                color: App.Style.accent
                                            }
                                            
                                            // The core functionality
                                            onValueChanged: {
                                                volumeControl.currentValue = value
                                                // Single dispatch to settings + every audio output.
                                                volumeController.applyVolume(Math.round(value))

                                                if (value > 0 && muteButton.isMuted) {
                                                    muteButton.isMuted = false
                                                    mediaManager.toggle_mute()
                                                }
                                                updateMuteButtonImage()
                                            }
                                        }
                                        
                                        // Circular touch area
                                        Item {
                                            id: touchAreaContainer
                                            anchors.centerIn: sliderContainer
                                            width: sliderContainer.width
                                            height: sliderContainer.height
                                            
                                            // Create multiple circular touch areas along the slider track
                                            Repeater {
                                                model: 9  // Create 9 touch areas along the slider
                                                
                                                // Each touch area is a circular MouseArea
                                                MouseArea {
                                                    id: circularTouchArea
                                                    // Position touch areas evenly along the slider
                                                    x: (index * (volumeSlider.width / 8)) - width/2 + volumeSlider.x
                                                    y: volumeSlider.y + volumeSlider.height/2 - height/2
                                                    width: dp(80)  // Large circular area
                                                    height: dp(80)
                                                    
                                                    // Make the touch area visually circular (only for debugging)
                                                    // Rectangle {
                                                    //     anchors.fill: parent
                                                    //     radius: width/2
                                                    //     color: "transparent"
                                                    //     border.width: 1
                                                    //     border.color: "red"
                                                    //     opacity: 0.3
                                                    // }
                                                    
                                                    // Handle touch interactions
                                                    onMouseXChanged: {
                                                        if (pressed) {
                                                            // Calculate global position relative to the slider
                                                            var globalX = mapToItem(volumeSlider, mouseX, mouseY).x
                                                            var newPosition = Math.max(0, Math.min(1, 
                                                                (globalX - volumeSlider.leftPadding) / volumeSlider.availableWidth))
                                                            volumeSlider.value = newPosition * 100
                                                        }
                                                    }
                                                    
                                                    // Allow slider to receive events too
                                                    preventStealing: false
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                Item { // SECTION 2: Center section - Navigation Buttons
                    property bool mediaControlsVisible: settingsManager ? settingsManager.showBottomBarMediaControls : true
                    Layout.fillWidth: true // Fill remaining space (centers content properly)
                    Layout.fillHeight: true
                    // With many nav buttons enabled the row can be wider than this
                    // section; keep it above the neighbouring sections so the
                    // outermost buttons (e.g. Phone Mirror) still receive clicks.
                    z: 1

                    RowLayout {
                        id: navigationBar
                        anchors.centerIn: parent
                        // Shrink the gap between buttons when they would not fit,
                        // instead of overflowing under the clock / media controls.
                        readonly property real preferredSpacing: App.Spacing.bottomBarBetweenButtonMargin * 6
                        readonly property real fitSpacing: {
                            var n = 0, w = 0
                            for (var i = 0; i < children.length; ++i) {
                                var c = children[i]
                                if (c.visible && c.implicitWidth > 0) { n++; w += c.implicitWidth }
                            }
                            if (n < 2) return preferredSpacing
                            var avail = parent.width - w
                            return Math.max(dp(4), Math.min(preferredSpacing, avail / (n - 1)))
                        }
                        spacing: fitSpacing
                        
                        // Home Button (Main Menu)
                        Control {
                            id: homeButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            
                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaHome.pressed ? 0.8 : 1.0
                                opacity: mouseAreaHome.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaHome.pressed ? 0.8 : 1.0
                                opacity: mouseAreaHome.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: homeButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/home_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: homeButtonImage
                                    source: homeButtonImage
                                    color: App.Style.bottomBarHomeButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaHome
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Pop to root/main menu
                                    while (stackView.depth > 1) {
                                        stackView.pop();
                                    }
                                }
                            }
                        }
                        
                        // OBD Button
                        Control {
                            id: obdButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaOBD.pressed ? 0.8 : 1.0
                                opacity: mouseAreaOBD.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaOBD.pressed ? 0.8 : 1.0
                                opacity: mouseAreaOBD.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: obdButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/obd_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: obdButtonImage
                                    source: obdButtonImage
                                    color: App.Style.bottomBarOBDButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaOBD
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    var name = currentItem ? currentItem.objectName : ""

                                    if (name === "obdHome") {
                                        // Already on OBD Home �� do nothing
                                        return
                                    }

                                    if (name.indexOf("obd") === 0) {
                                        // On an OBD subpage — go back to OBDHome
                                        var found = false
                                        while (stackView.depth > 1) {
                                            var item = stackView.currentItem
                                            if (item && item.objectName === "obdHome") {
                                                found = true
                                                break
                                            }
                                            stackView.pop()
                                        }
                                        if (!found) {
                                            var component = Qt.createComponent("OBDHome.qml")
                                            if (component.status === Component.Ready) {
                                                var page = component.createObject(stackView, {
                                                    stackView: bottomBar.stackView,
                                                    mainWindow: stackView.parent.Window.window
                                                })
                                                if (page) stackView.push(page)
                                            }
                                        }
                                    } else {
                                        // From a non-OBD page — check if we have a remembered subpage
                                        var lastPage = settingsManager ? settingsManager.get_setting_with_default("lastOBDPage", "") : ""
                                        var target = (lastPage !== "") ? lastPage : "OBDHome.qml"

                                        var component = Qt.createComponent(target)
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView,
                                                mainWindow: stackView.parent.Window.window
                                            })
                                            if (page) stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Media Button
                        Control {
                            id: mediaButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            
                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaMedia.pressed ? 0.8 : 1.0
                                opacity: mouseAreaMedia.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaMedia.pressed ? 0.8 : 1.0
                                opacity: mouseAreaMedia.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: mediaButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/media_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: mediaButtonImage
                                    source: mediaButtonImage
                                    color: App.Style.bottomBarMediaButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaMedia
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    var defaultPage = settingsManager ? settingsManager.musicButtonDefaultPage : "mediaRoom"

                                    if (currentItem && currentItem.objectName === "mediaRoom") {
                                        // On MediaRoom, go to MediaPlayer
                                        var component = Qt.createComponent("MediaPlayer.qml")
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView,
                                                mainWindow: stackView.parent.Window.window
                                            })
                                            stackView.push(page)
                                        }
                                    } else if (currentItem && currentItem.objectName === "mediaPlayer") {
                                        // On MediaPlayer, go to MediaRoom
                                        var component = Qt.createComponent("MediaRoom.qml")
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView
                                            })
                                            stackView.push(page)
                                        }
                                    } else {
                                        // From other pages, go to the default page based on setting
                                        var targetPage = defaultPage === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml"
                                        var component = Qt.createComponent(targetPage)
                                        if (component.status === Component.Ready) {
                                            var props = { stackView: bottomBar.stackView }
                                            if (defaultPage === "mediaPlayer") {
                                                props.mainWindow = stackView.parent.Window.window
                                            }
                                            var page = component.createObject(stackView, props)
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Sensor Button
                        Control {
                            id: sensorButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaSensor.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSensor.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaSensor.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSensor.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: sensorButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/sensor_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: sensorButtonImage
                                    source: sensorButtonImage
                                    color: App.Style.bottomBarSensorButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaSensor
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: bottomBar.openSensorSection()
                            }
                        }

                        // Settings Button
                        Control {
                            id: settingsButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaSettings.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSettings.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaSettings.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSettings.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: settingsButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/settings_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: settingsButtonImage
                                    source: settingsButtonImage
                                    color: App.Style.bottomBarSettingsButton
                                }
                            }

                            // Update notification dot
                            Rectangle {
                                width: dp(8)
                                height: dp(8)
                                radius: width / 2
                                color: "#FF9800"
                                z: 10
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: dp(-1)
                                anchors.rightMargin: dp(-1)
                                visible: networkManager && networkManager.updateStatus === "update-available"

                                SequentialAnimation on opacity {
                                    running: networkManager && networkManager.updateStatus === "update-available"
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                                    NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                                }
                            }

                            MouseArea {
                                id: mouseAreaSettings
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onPressAndHold: settingsVisibilityPopup.open()
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    if (currentItem && currentItem.objectName === "settingsMenu") {
                                        if (typeof currentItem.navigateToHub === "function")
                                            currentItem.navigateToHub()
                                    } else {
                                        // Pop back to root first, then push fresh settings page
                                        while (stackView.depth > 1)
                                            stackView.pop()
                                        var page = Qt.createComponent("SettingsMenu.qml").createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window,
                                            initialSection: lastSettingsSection
                                        })
                                        stackView.push(page)
                                    }
                                }
                            }
                        }

                        // Android Auto Button
                        Control {
                            id: androidAutoButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            visible: (typeof isAndroid === "undefined" || !isAndroid) && (settingsManager ? settingsManager.androidAutoEnabled : false)

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaAndroidAuto.pressed ? 0.8 : 1.0
                                opacity: mouseAreaAndroidAuto.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaAndroidAuto.pressed ? 0.8 : 1.0
                                opacity: mouseAreaAndroidAuto.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: androidAutoButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/android_auto_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: androidAutoButtonImage
                                    source: androidAutoButtonImage
                                    color: App.Style.bottomBarAndroidAutoButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaAndroidAuto
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Navigate to AndroidAutoView and launch seamless DHU
                                    var component = Qt.createComponent("AndroidAutoView.qml")
                                    if (component.status === Component.Ready) {
                                        var page = component.createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window
                                        })
                                        if (page) {
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Phone Mirror Button
                        Control {
                            id: phoneMirrorButton
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            visible: (typeof isAndroid === "undefined" || !isAndroid) && (settingsManager ? settingsManager.phoneMirrorEnabled : false)

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaPhoneMirror.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPhoneMirror.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaPhoneMirror.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPhoneMirror.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: phoneMirrorButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/phone_mirror_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: phoneMirrorButtonImage
                                    source: phoneMirrorButtonImage
                                    color: App.Style.bottomBarPhoneMirrorButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaPhoneMirror
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Check if PhoneMirrorView is already on the stack - if so, pop back to it
                                    for (var i = 0; i < stackView.depth; i++) {
                                        var item = stackView.get(i)
                                        if (item && item.objectName === "phoneMirrorView") {
                                            console.log("PhoneMirrorView found on stack at index", i, "- popping to it")
                                            // Pop all items above it
                                            while (stackView.depth > i + 1) {
                                                stackView.pop()
                                            }
                                            return
                                        }
                                    }

                                    // Navigate to PhoneMirrorView
                                    var component = Qt.createComponent("PhoneMirrorView.qml")
                                    if (component.status === Component.Ready) {
                                        var page = component.createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window,
                                            objectName: "phoneMirrorView"
                                        })
                                        if (page) {
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item { // SECTION 3: Right section - Clock and other controls
                    id: clockSection
                    property bool showClock: settingsManager ? settingsManager.showClock : true
                    Layout.preferredWidth: showClock ? parent.width * 0.2 : 0 // Collapse when clock hidden so nav row re-centers
                    Layout.fillHeight: true

                    RowLayout {
                        id: rightControls
                        anchors {
                            right: parent.right
                            rightMargin: App.Spacing.overallMargin
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: App.Spacing.bottomBarBetweenButtonMargin
                        

                        Item {
                            id: clockContainer
                            Layout.preferredWidth: clockText.implicitWidth + dp(20) // Add some padding
                            Layout.preferredHeight: clockText.implicitHeight + dp(10)
                            
                            Rectangle {
                                anchors.fill: parent
                                color: mouseAreaClock.pressed ? App.Style.hoverColor : "transparent"
                                radius: 4
                            }
                            
                            Text {
                                id: clockText
                                anchors.centerIn: parent
                                visible: settingsManager ? settingsManager.showClock : true
                                font.pixelSize: settingsManager ? settingsManager.clockSize : 18
                                font.family: bottomBar.globalFont
                                color: App.Style.clockTextColor
                                text: bottomBar.clockTimeText
                            }

                            MouseArea {
                                id: mouseAreaClock
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var page = Qt.createComponent("SensorHome.qml").createObject(stackView, {
                                        stackView: bottomBar.stackView,
                                        mainWindow: bottomBar.mainWindow
                                    })
                                    stackView.push(page)
                                }
                            }
                        }
                    }
                }

                // Connections and Signal handlers
                Connections {
                    target: mediaManager
                    function onPlayStateChanged(playing) {
                        // Only update play button if not using Spotify
                        if (!useSpotify) {
                            var src = playing ? "./assets/pause_button.svg" : "./assets/play_button.svg"
                            playButtonImage.source = src
                            if (typeof playButtonImageVertical !== "undefined" && playButtonImageVertical) {
                                playButtonImageVertical.source = src
                            }
                        }
                    }
                    function onMuteChanged(muted) {
                        muteButton.isMuted = muted
                        updateMuteButtonImage()
                    }
                    function onVolumeChanged(volume) {
                        // Only update if not being changed by the slider itself
                        if (!volumeSlider.pressed) {
                            // Convert from raw volume to percentage (0-100)
                            var volumePercentage = Math.round(Math.sqrt(volume) * 100)
                            volumeControl.currentValue = volumePercentage
                            volumeSlider.value = volumePercentage
                        }
                        updateMuteButtonImage()
                    }
                    function onShuffleStateChanged(enabled) {
                        if (!useSpotify) {
                            bottomBar.isShuffleEnabled = enabled
                        }
                    }
                }

                // Spotify connection, play state, and shuffle state
                Connections {
                    target: spotifyManager
                    function onConnectionStateChanged(connected) {
                        // When Spotify connects, apply Octave's current volume to it
                        if (connected && settingsManager) {
                            spotifyManager.set_volume(settingsManager.currentVolume)
                        }
                    }
                    function onPlayStateChanged(playing) {
                        // Only update play button if using Spotify
                        if (useSpotify) {
                            var src = playing ? "./assets/pause_button.svg" : "./assets/play_button.svg"
                            playButtonImage.source = src
                            playButtonImageVertical.source = src
                        }
                    }
                    function onShuffleStateChanged(enabled) {
                        if (useSpotify) {
                            bottomBar.isShuffleEnabled = enabled
                        }
                    }
                }

                // Update shuffle state when media source changes
                Connections {
                    target: settingsManager
                    function onMediaSourceChanged(source) {
                        var nowUseSpotify = (source === "spotify" && spotifyManager && spotifyManager.is_connected())
                        if (nowUseSpotify) {
                            bottomBar.isShuffleEnabled = spotifyManager.is_shuffled()
                        } else if (mediaManager) {
                            bottomBar.isShuffleEnabled = mediaManager.is_shuffled()
                        }
                    }
                }

                // Helper functions
                function getUpdatedMuteSource() {
                    if (mediaManager.is_muted() || muteButton.isMuted || volumeControl.currentValue === 0) {
                        return "./assets/mute_on.svg"
                    }
                    var volume = volumeControl.currentValue
                    if (volume < 33) {
                        return "./assets/mute_off_low.svg"
                    } else if (volume < 66) {
                        return "./assets/mute_off_med.svg"
                    } else {
                        return "./assets/mute_off_high.svg"
                    }
                }

                function updateMuteButtonImage() {
                    muteButtonImage.source = getUpdatedMuteSource()
                }
            }
        }

        Component { //Vertical
            id: verticalLayoutComponent

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                
                // SECTION 1: Media Controls (Top section when vertical)
                Item {
                    id: mediaControlsSectionVertical
                    property bool showControls: settingsManager ? settingsManager.showBottomBarMediaControls : true
                    // When hidden, mirror the clock section's height so nav buttons sit at the true vertical center
                    Layout.preferredHeight: showControls ? parent.height * 0.4 : parent.height * 0.1
                    Layout.fillWidth: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: mediaControlsSectionVertical.showControls
                        spacing: App.Spacing.bottomBarBetweenButtonMargin * 4
                        
                        // Previous, Play/Pause, and Next buttons
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            spacing: App.Spacing.bottomBarBetweenButtonMargin * 2

                            Item {
                                Layout.fillWidth: true
                            }

                            // Previous button
                            Control {
                                id: previousButtonControlVertical
                                Layout.preferredWidth: App.Spacing.bottomBarPreviousButtonWidth
                                Layout.preferredHeight: App.Spacing.bottomBarPreviousButtonHeight
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

                                scale: mouseAreaPrevVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPrevVertical.pressed ? 0.7 : 1.0

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }

                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }

                                background: Rectangle { color: "transparent" }

                                contentItem: Item {
                                    Image {
                                        id: previousButtonImageVertical
                                        anchors.centerIn: parent
                                        width: parent.width
                                        height: parent.height
                                        source: "./assets/previous_button.svg"
                                        sourceSize: Qt.size(width * 2, height * 2)
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        antialiasing: true
                                        mipmap: true
                                        visible: false
                                    }

                                    ColorOverlay {
                                        anchors.fill: previousButtonImageVertical
                                        source: previousButtonImageVertical
                                        color: App.Style.bottomBarPreviousButton
                                    }
                                }

                                Item {
                                    anchors.centerIn: parent
                                    width: parent.width * 2
                                    height: parent.height * 2.5

                                    MouseArea {
                                        id: mouseAreaPrevVertical
                                        anchors.fill: parent
                                        onClicked: useSpotify ? spotifyManager.previous_track() : mediaManager.previous_track()
                                    }
                                }
                            }

                            // Play/Pause button
                            Control {
                                id: playButtonControlVertical
                                Layout.preferredWidth: App.Spacing.bottomBarPlayButtonWidth
                                Layout.preferredHeight: App.Spacing.bottomBarPlayButtonHeight
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

                                scale: mouseAreaPlayVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPlayVertical.pressed ? 0.7 : 1.0

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }

                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }

                                background: Rectangle { color: "transparent" }

                                contentItem: Item {
                                    Image {
                                        id: playButtonImageVertical
                                        anchors.centerIn: parent
                                        width: parent.height
                                        height: parent.height
                                        source: {
                                            var isPlaying = useSpotify ?
                                                (spotifyManager && spotifyManager.is_playing()) :
                                                (mediaManager && mediaManager.is_playing())
                                            return isPlaying ? "./assets/pause_button.svg" : "./assets/play_button.svg"
                                        }
                                        sourceSize: Qt.size(width * 2, height * 2)
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        antialiasing: true
                                        mipmap: true
                                        visible: false
                                    }

                                    ColorOverlay {
                                        anchors.fill: playButtonImageVertical
                                        source: playButtonImageVertical
                                        color: App.Style.bottomBarPlayButton
                                    }
                                }

                                Item {
                                    anchors.centerIn: parent
                                    width: parent.width * 1.5
                                    height: parent.height * 2.5

                                    MouseArea {
                                        id: mouseAreaPlayVertical
                                        anchors.fill: parent
                                        onClicked: useSpotify ? spotifyManager.toggle_play() : mediaManager.toggle_play()
                                    }
                                }
                            }

                            // Next button
                            Control {
                                id: nextButtonControlVertical
                                Layout.preferredWidth: App.Spacing.bottomBarNextButtonWidth
                                Layout.preferredHeight: App.Spacing.bottomBarNextButtonHeight
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

                                scale: mouseAreaNextVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaNextVertical.pressed ? 0.7 : 1.0

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }

                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }

                                background: Rectangle { color: "transparent" }

                                contentItem: Item {
                                    Image {
                                        id: nextButtonImageVertical
                                        anchors.centerIn: parent
                                        width: parent.width
                                        height: parent.height
                                        source: "./assets/next_button.svg"
                                        sourceSize: Qt.size(width * 2, height * 2)
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        antialiasing: true
                                        mipmap: true
                                        visible: false
                                    }

                                    ColorOverlay {
                                        anchors.fill: nextButtonImageVertical
                                        source: nextButtonImageVertical
                                        color: App.Style.bottomBarNextButton
                                    }
                                }

                                Item {
                                    anchors.centerIn: parent
                                    width: parent.width * 2
                                    height: parent.height * 2.5

                                    MouseArea {
                                        id: mouseAreaNextVertical
                                        anchors.fill: parent
                                        onClicked: useSpotify ? spotifyManager.next_track() : mediaManager.next_track()
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        
                        // Shuffle button
                        Control {
                            id: shuffleButtonVertical
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: App.Spacing.bottomBarShuffleButtonWidth
                            Layout.preferredHeight: App.Spacing.bottomBarShuffleButtonHeight

                            scale: mouseAreaShuffleVertical.pressed ? 0.8 : 1.0
                            opacity: mouseAreaShuffleVertical.pressed ? 0.7 : 1.0

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack
                                    easing.overshoot: 1.1
                                }
                            }

                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle {
                                color: bottomBar.isShuffleEnabled ? App.Style.bottomBarToggleShade : "transparent"
                                radius: 4
                            }

                            contentItem: Item {
                                Image {
                                    id: shuffleButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/shuffle_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: shuffleButtonImageVertical
                                    source: shuffleButtonImageVertical
                                    color: bottomBar.isShuffleEnabled ?
                                        App.Style.bottomBarActiveToggleButton :
                                        App.Style.bottomBarShuffleButton
                                }
                            }

                            Item {
                                anchors.centerIn: parent
                                width: parent.width * 3
                                height: parent.height

                                MouseArea {
                                    id: mouseAreaShuffleVertical
                                    anchors.fill: parent
                                    onClicked: {
                                        if (useSpotify) {
                                            spotifyManager.toggle_shuffle()
                                        } else {
                                            mediaManager.toggle_shuffle()
                                        }
                                    }
                                }
                            }
                        }
                        
                        // Mute button
                        Control {
                            id: muteButtonVertical
                            property bool isMuted: false
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: App.Spacing.bottomBarMuteButtonWidth
                            Layout.preferredHeight: App.Spacing.bottomBarMuteButtonHeight
                            
                            scale: mouseAreaMuteVertical.pressed ? 0.8 : 1.0
                            opacity: mouseAreaMuteVertical.pressed ? 0.7 : 1.0
                            
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 200
                                    easing.type: Easing.OutBack  
                                    easing.overshoot: 1.1
                                }
                            }
                            
                            Behavior on opacity {
                                NumberAnimation { duration: 150 }
                            }

                            background: Rectangle { color: "transparent" }
                            
                            contentItem: Item {
                                Image {
                                    id: muteButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.height
                                    source: getUpdatedMuteSourceVertical()
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: muteButtonImageVertical
                                    source: muteButtonImageVertical
                                    color: App.Style.bottomBarVolumeButton
                                }
                            }
                            
                            Item {
                                anchors.centerIn: parent
                                width: parent.width * 3
                                height: parent.height 
                            
                                MouseArea {
                                    id: mouseAreaMuteVertical
                                    anchors.fill: parent
                                    onClicked: {
                                        // If volume is 0, restore to startup volume instead of toggling mute
                                        if (volumeControlVertical.currentValue === 0 && settingsManager) {
                                            var startupVol = Math.round(settingsManager.startUpVolume * 100)
                                            volumeSliderVertical.value = startupVol
                                        } else {
                                            mediaManager.toggle_mute()
                                        }
                                    }
                                }
                            }
                        }
                        
                        // Volume control with better alignment
                        Control {
                            id: volumeControlVertical
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: App.Spacing.bottomBarVolumeSliderWidth * 0.8
                            Layout.preferredHeight: App.Spacing.bottomBarVolumeSliderHeight
                            
                            property int currentValue: 0
                            
                            Text {
                                id: volumeTextVertical
                                anchors.centerIn: parent
                                text: volumeControlVertical.currentValue + "%"
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.bottomBarVolumeText
                                font.bold: true
                                font.family: bottomBar.globalFont
                                anchors.horizontalCenterOffset: -2  // Further adjust text position
                            }
                            
                            Component.onCompleted: {
                                if (settingsManager) {
                                    currentValue = settingsManager.currentVolume
                                    // Route startup volume to every audio output via
                                    // the shared VolumeController (see backend/volume_utils.py).
                                    volumeController.applyVolume(currentValue)
                                } else {
                                    currentValue = 10
                                    mediaManager.setVolume(0.1)
                                }

                                updateMuteButtonImageVertical()

                                if (mediaManager) {
                                    bottomBar.isShuffleEnabled = mediaManager.is_shuffled()
                                }
                            }
                            Item {
                                anchors.centerIn: parent
                                width: parent.width*3 
                                height: parent.height*2

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: popupSliderVertical.open()
                                }
                            }

                            Popup {
                                id: popupSliderVertical
                                width: parent.Window.width
                                height: dp(140)
                                anchors.centerIn: Overlay.overlay
                                modal: true
                                focus: true
                                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                
                                // Simple background
                                background: Rectangle {
                                    color: App.Style.backgroundColor
                                    radius: dpMin(8, 2)
                                    border.color: App.Style.accent
                                    border.width: 1
                                }
                                
                                // Content with centered slider
                                contentItem: Item {
                                    anchors.fill: parent
                                    
                                    // Larger percentage display
                                    Label {
                                        id: percentLabelVertical
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top
                                        anchors.topMargin: dp(15)
                                        text: Math.round(volumeSliderVertical.value) + "%"
                                        color: App.Style.accent
                                        font.pixelSize: dp(32)
                                        font.bold: true
                                    }

                                    // Container for the slider - centered in the popup
                                    Item {
                                        id: sliderContainerVertical
                                        anchors.top: percentLabelVertical.bottom
                                        anchors.topMargin: dp(15)
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: dp(15)

                                        // Simplified slider
                                        Slider {
                                            id: volumeSliderVertical
                                            anchors.centerIn: parent
                                            width: parent.width * 0.95
                                            height: dp(50)
                                            from: 0
                                            to: 100
                                            stepSize: 1
                                            value: volumeControlVertical.currentValue
                                            
                                            // Simple slider background
                                            background: Rectangle {
                                                x: volumeSliderVertical.leftPadding
                                                y: volumeSliderVertical.topPadding + volumeSliderVertical.availableHeight / 2 - height / 2
                                                width: volumeSliderVertical.availableWidth
                                                height: dp(16)
                                                radius: dpMin(8, 2)
                                                color: App.Style.hoverColor
                                                
                                                // Filled portion
                                                Rectangle {
                                                    width: volumeSliderVertical.visualPosition * parent.width
                                                    height: parent.height
                                                    color: App.Style.volumeSliderColor
                                                    radius: dpMin(8, 2)
                                                }
                                            }
                                            
                                            // Larger handle for better touch
                                            handle: Rectangle {
                                                x: volumeSliderVertical.leftPadding + volumeSliderVertical.visualPosition * (volumeSliderVertical.availableWidth - width)
                                                y: volumeSliderVertical.topPadding + volumeSliderVertical.availableHeight / 2 - height / 2
                                                width: dp(40)
                                                height: dp(40)
                                                radius: dpMin(20, 2)  // Circular handle
                                                color: App.Style.accent
                                            }
                                            
                                            // The core functionality
                                            onValueChanged: {
                                                volumeControlVertical.currentValue = value
                                                // Single dispatch to settings + every audio output.
                                                volumeController.applyVolume(Math.round(value))

                                                if (value > 0 && muteButtonVertical.isMuted) {
                                                    muteButtonVertical.isMuted = false
                                                    mediaManager.toggle_mute()
                                                }
                                                updateMuteButtonImageVertical()
                                            }
                                        }

                                        // Circular touch area
                                        Item {
                                            id: touchAreaContainerVertical
                                            anchors.centerIn: sliderContainerVertical
                                            width: sliderContainerVertical.width
                                            height: sliderContainerVertical.height
                                            
                                            // Create multiple circular touch areas along the slider track
                                            Repeater {
                                                model: 9  // Create 9 touch areas along the slider
                                                
                                                // Each touch area is a circular MouseArea
                                                MouseArea {
                                                    // Position touch areas evenly along the slider
                                                    x: (index * (volumeSliderVertical.width / 8)) - width/2 + volumeSliderVertical.x
                                                    y: volumeSliderVertical.y + volumeSliderVertical.height/2 - height/2
                                                    width: dp(80)  // Large circular area
                                                    height: dp(80)
                                                    
                                                    // Handle touch interactions
                                                    onMouseXChanged: {
                                                        if (pressed) {
                                                            // Calculate global position relative to the slider
                                                            var globalX = mapToItem(volumeSliderVertical, mouseX, mouseY).x
                                                            var newPosition = Math.max(0, Math.min(1, 
                                                                (globalX - volumeSliderVertical.leftPadding) / volumeSliderVertical.availableWidth))
                                                            volumeSliderVertical.value = newPosition * 100
                                                        }
                                                    }
                                                    
                                                    // Allow slider to receive events too
                                                    preventStealing: false
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // SECTION 2: Navigation (Middle section when vertical)
                Item {
                    property bool mediaControlsVisible: settingsManager ? settingsManager.showBottomBarMediaControls : true
                    Layout.fillHeight: true // Fill remaining space (centers content properly)
                    Layout.fillWidth: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: App.Spacing.bottomBarBetweenButtonMargin * 3

                        // Home Button
                        Control {
                            id: homeButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            Layout.alignment: Qt.AlignHCenter
                            
                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaHomeVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaHomeVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaHomeVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaHomeVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: homeButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/home_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: homeButtonImageVertical
                                    source: homeButtonImageVertical
                                    color: App.Style.bottomBarHomeButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaHomeVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    while (stackView.depth > 1) {
                                        stackView.pop();
                                    }
                                }
                            }
                        }
                        // OBD Button
                        Control {
                            id: obdButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            Layout.alignment: Qt.AlignHCenter

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaOBDVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaOBDVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaOBDVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaOBDVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: obdButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/obd_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: obdButtonImageVertical
                                    source: obdButtonImageVertical
                                    color: App.Style.bottomBarOBDButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaOBDVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    var name = currentItem ? currentItem.objectName : ""

                                    if (name === "obdHome") {
                                        // Already on OBD Home �� do nothing
                                        return
                                    }

                                    if (name.indexOf("obd") === 0) {
                                        // On an OBD subpage — go back to OBDHome
                                        var found = false
                                        while (stackView.depth > 1) {
                                            var item = stackView.currentItem
                                            if (item && item.objectName === "obdHome") {
                                                found = true
                                                break
                                            }
                                            stackView.pop()
                                        }
                                        if (!found) {
                                            var component = Qt.createComponent("OBDHome.qml")
                                            if (component.status === Component.Ready) {
                                                var page = component.createObject(stackView, {
                                                    stackView: bottomBar.stackView,
                                                    mainWindow: stackView.parent.Window.window
                                                })
                                                if (page) stackView.push(page)
                                            }
                                        }
                                    } else {
                                        // From a non-OBD page — check if we have a remembered subpage
                                        var lastPage = settingsManager ? settingsManager.get_setting_with_default("lastOBDPage", "") : ""
                                        var target = (lastPage !== "") ? lastPage : "OBDHome.qml"

                                        var component = Qt.createComponent(target)
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView,
                                                mainWindow: stackView.parent.Window.window
                                            })
                                            if (page) stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Media Button
                        Control {
                            id: mediaButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            Layout.alignment: Qt.AlignHCenter
                            
                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaMediaVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaMediaVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaMediaVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaMediaVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: mediaButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/media_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }
                                
                                ColorOverlay {
                                    anchors.fill: mediaButtonImageVertical
                                    source: mediaButtonImageVertical
                                    color: App.Style.bottomBarMediaButton
                                }
                            }
                            
                            MouseArea {
                                id: mouseAreaMediaVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    var defaultPage = settingsManager ? settingsManager.musicButtonDefaultPage : "mediaRoom"

                                    if (currentItem && currentItem.objectName === "mediaRoom") {
                                        // On MediaRoom, go to MediaPlayer
                                        var component = Qt.createComponent("MediaPlayer.qml")
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView,
                                                mainWindow: stackView.parent.Window.window
                                            })
                                            stackView.push(page)
                                        }
                                    } else if (currentItem && currentItem.objectName === "mediaPlayer") {
                                        // On MediaPlayer, go to MediaRoom
                                        var component = Qt.createComponent("MediaRoom.qml")
                                        if (component.status === Component.Ready) {
                                            var page = component.createObject(stackView, {
                                                stackView: bottomBar.stackView
                                            })
                                            stackView.push(page)
                                        }
                                    } else {
                                        // From other pages, go to the default page based on setting
                                        var targetPage = defaultPage === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml"
                                        var component = Qt.createComponent(targetPage)
                                        if (component.status === Component.Ready) {
                                            var props = { stackView: bottomBar.stackView }
                                            if (defaultPage === "mediaPlayer") {
                                                props.mainWindow = stackView.parent.Window.window
                                            }
                                            var page = component.createObject(stackView, props)
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Sensor Button
                        Control {
                            id: sensorButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            Layout.alignment: Qt.AlignHCenter

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaSensorVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSensorVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaSensorVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSensorVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: sensorButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/sensor_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: sensorButtonImageVertical
                                    source: sensorButtonImageVertical
                                    color: App.Style.bottomBarSensorButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaSensorVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: bottomBar.openSensorSection()
                            }
                        }

                        // Settings Button
                        Control {
                            id: settingsButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            Layout.alignment: Qt.AlignHCenter

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaSettingsVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSettingsVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaSettingsVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaSettingsVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: settingsButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/settings_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: settingsButtonImageVertical
                                    source: settingsButtonImageVertical
                                    color: App.Style.bottomBarSettingsButton
                                }
                            }

                            // Update notification dot
                            Rectangle {
                                width: dp(8)
                                height: dp(8)
                                radius: width / 2
                                color: "#FF9800"
                                z: 10
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: dp(-1)
                                anchors.rightMargin: dp(-1)
                                visible: networkManager && networkManager.updateStatus === "update-available"

                                SequentialAnimation on opacity {
                                    running: networkManager && networkManager.updateStatus === "update-available"
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                                    NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                                }
                            }

                            MouseArea {
                                id: mouseAreaSettingsVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onPressAndHold: settingsVisibilityPopup.open()
                                onClicked: {
                                    var currentItem = stackView.currentItem
                                    if (currentItem && currentItem.objectName === "settingsMenu") {
                                        if (typeof currentItem.navigateToHub === "function")
                                            currentItem.navigateToHub()
                                    } else {
                                        // Pop back to root first, then push fresh settings page
                                        while (stackView.depth > 1)
                                            stackView.pop()
                                        var page = Qt.createComponent("SettingsMenu.qml").createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window,
                                            initialSection: lastSettingsSection
                                        })
                                        stackView.push(page)
                                    }
                                }
                            }
                        }

                        // Android Auto Button
                        Control {
                            id: androidAutoButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            visible: (typeof isAndroid === "undefined" || !isAndroid) && (settingsManager ? settingsManager.androidAutoEnabled : false)
                            Layout.alignment: Qt.AlignHCenter

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaAndroidAutoVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaAndroidAutoVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaAndroidAutoVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaAndroidAutoVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: androidAutoButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/android_auto_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: androidAutoButtonImageVertical
                                    source: androidAutoButtonImageVertical
                                    color: App.Style.bottomBarAndroidAutoButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaAndroidAutoVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Navigate to AndroidAutoView and launch seamless DHU
                                    var component = Qt.createComponent("AndroidAutoView.qml")
                                    if (component.status === Component.Ready) {
                                        var page = component.createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window
                                        })
                                        if (page) {
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }

                        // Phone Mirror Button (Vertical)
                        Control {
                            id: phoneMirrorButtonVertical
                            implicitWidth: App.Spacing.bottomBarNavButtonWidth*1.5
                            implicitHeight: App.Spacing.bottomBarNavButtonHeight
                            visible: (typeof isAndroid === "undefined" || !isAndroid) && (settingsManager ? settingsManager.phoneMirrorEnabled : false)
                            Layout.alignment: Qt.AlignHCenter

                            background: Rectangle {
                                color: "transparent"
                                radius: dpMin(8, 2)
                                border.color: App.Style.accent
                                border.width: 1
                                scale: mouseAreaPhoneMirrorVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPhoneMirrorVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            contentItem: Item {
                                scale: mouseAreaPhoneMirrorVertical.pressed ? 0.8 : 1.0
                                opacity: mouseAreaPhoneMirrorVertical.pressed ? 0.7 : 1.0
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.1
                                    }
                                }
                                Behavior on opacity {
                                    NumberAnimation { duration: 150 }
                                }
                                Image {
                                    id: phoneMirrorButtonImageVertical
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: parent.height * 0.7
                                    source: "./assets/phone_mirror_button.svg"
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    antialiasing: true
                                    mipmap: true
                                    visible: false
                                }

                                ColorOverlay {
                                    anchors.fill: phoneMirrorButtonImageVertical
                                    source: phoneMirrorButtonImageVertical
                                    color: App.Style.bottomBarPhoneMirrorButton
                                }
                            }

                            MouseArea {
                                id: mouseAreaPhoneMirrorVertical
                                width: parent.width * 1.5
                                height: parent.height * 1.5
                                anchors.centerIn: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Check if PhoneMirrorView is already on the stack - if so, pop back to it
                                    for (var i = 0; i < stackView.depth; i++) {
                                        var item = stackView.get(i)
                                        if (item && item.objectName === "phoneMirrorView") {
                                            console.log("PhoneMirrorView found on stack at index", i, "- popping to it")
                                            // Pop all items above it
                                            while (stackView.depth > i + 1) {
                                                stackView.pop()
                                            }
                                            return
                                        }
                                    }

                                    // Navigate to PhoneMirrorView
                                    var component = Qt.createComponent("PhoneMirrorView.qml")
                                    if (component.status === Component.Ready) {
                                        var page = component.createObject(stackView, {
                                            stackView: bottomBar.stackView,
                                            mainWindow: stackView.parent.Window.window,
                                            objectName: "phoneMirrorView"
                                        })
                                        if (page) {
                                            stackView.push(page)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // SECTION 3: Clock (Bottom section when vertical)
                Item {
                    Layout.preferredHeight: parent.height * 0.1
                    Layout.fillWidth: true
                    
                    Item {
                        anchors {
                            bottom: parent.bottom
                            bottomMargin: App.Spacing.overallMargin
                            horizontalCenter: parent.horizontalCenter
                        }
                        width: parent.width
                        height: clockTextVertical.implicitHeight + dp(20)

                        Item {
                            id: clockContainerVertical
                            anchors.centerIn: parent
                            width: clockTextVertical.implicitWidth + dp(20)
                            height: parent.height
                            
                            Rectangle {
                                anchors.fill: parent
                                color: mouseAreaClockVertical.pressed ? App.Style.hoverColor : "transparent"
                                radius: 4
                            }
                            
                            Text {
                                id: clockTextVertical
                                anchors.centerIn: parent
                                visible: settingsManager ? settingsManager.showClock : true
                                font.pixelSize: settingsManager ? settingsManager.clockSize : 18
                                font.family: bottomBar.globalFont
                                color: App.Style.clockTextColor
                                text: bottomBar.clockTimeText
                            }
                            
                            MouseArea {
                                id: mouseAreaClockVertical
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var page = Qt.createComponent("SensorHome.qml").createObject(stackView, {
                                        stackView: bottomBar.stackView,
                                        mainWindow: bottomBar.mainWindow
                                    })
                                    stackView.push(page)
                                }
                            }
                        }
                    }
                }
                
                // Helper functions for vertical layout
                function updateMuteButtonImageVertical() {
                    muteButtonImageVertical.source = getUpdatedMuteSourceVertical()
                }
                
                function getUpdatedMuteSourceVertical() {
                    if (mediaManager.is_muted() || muteButtonVertical.isMuted || volumeControlVertical.currentValue === 0) {
                        return "./assets/mute_on.svg"
                    }
                    const volume = volumeControlVertical.currentValue
                    if (volume < 33) {
                        return "./assets/mute_off_low.svg"
                    } else if (volume < 66) {
                        return "./assets/mute_off_med.svg"
                    } else {
                        return "./assets/mute_off_high.svg"
                    }
                }
                
                // Connections for vertical layout
                Connections {
                    target: mediaManager
                    function onPlayStateChanged(playing) {
                        playButtonImageVertical.source = playing ?
                            "./assets/pause_button.svg" : "./assets/play_button.svg"
                    }
                    function onMuteChanged(muted) {
                        muteButtonVertical.isMuted = muted
                        updateMuteButtonImageVertical()
                    }
                    function onVolumeChanged(volume) {
                        // Only update if not being changed by the slider itself
                        if (!volumeSliderVertical.pressed) {
                            // Convert from raw volume to percentage (0-100)
                            var volumePercentage = Math.round(Math.sqrt(volume) * 100)
                            volumeControlVertical.currentValue = volumePercentage
                            volumeSliderVertical.value = volumePercentage
                        }
                        updateMuteButtonImageVertical()
                    }
                    function onShuffleStateChanged(enabled) {
                        if (!useSpotify) {
                            bottomBar.isShuffleEnabled = enabled
                        }
                    }
                }

                // Spotify connection and shuffle state for vertical layout
                Connections {
                    target: spotifyManager
                    function onConnectionStateChanged(connected) {
                        // When Spotify connects, apply Octave's current volume to it
                        if (connected && settingsManager) {
                            spotifyManager.set_volume(settingsManager.currentVolume)
                        }
                    }
                    function onShuffleStateChanged(enabled) {
                        if (useSpotify) {
                            bottomBar.isShuffleEnabled = enabled
                        }
                    }
                }

                // Update shuffle state when media source changes (vertical)
                Connections {
                    target: settingsManager
                    function onMediaSourceChanged(source) {
                        var nowUseSpotify = (source === "spotify" && spotifyManager && spotifyManager.is_connected())
                        if (nowUseSpotify) {
                            bottomBar.isShuffleEnabled = spotifyManager.is_shuffled()
                        } else if (mediaManager) {
                            bottomBar.isShuffleEnabled = mediaManager.is_shuffled()
                        }
                    }
                }

            }
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