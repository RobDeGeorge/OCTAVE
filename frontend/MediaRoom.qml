import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Controls.Basic 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: mediaRoom
    objectName: "mediaRoom"
    property StackView stackView
    property ApplicationWindow mainWindow

    // MediaRoom is built once and re-pushed by the StackView page cache (see
    // Main.qml); re-read the now-playing state on every visit so nothing that
    // changed while the page was hidden is shown stale.
    StackView.onActivated: mainContent.syncNowPlaying()

    // Demo a track-transition without actually changing the track. Used by
    // the Now Playing studio's chip selection so users see the chosen
    // transition fire immediately. `style` overrides the carousel's bound
    // value for this one animation; we re-bind right after so future
    // settings changes still propagate live.
    function previewTrackTransition(direction, style) {
        if (!albumArtStack) return
        if (style && style !== "")
            albumArtStack.transitionStyle = style
        albumArtStack.direction = (direction === -1) ? -1 : 1
        albumArtStack.triggerSlide()
        if (musicDeviceLoader.item) musicDeviceLoader.item.loadMedia(false)
        // Restore live binding so subsequent settings changes still flow.
        albumArtStack.transitionStyle = Qt.binding(function() {
            return settingsManager ? settingsManager.albumArtTransition : "Crossfade"
        })
    }

    // Navigate to cached DownloadPage, optionally pre-selecting a playlist
    function openDownloadPage(targetPlaylist) {
        var sv = mediaRoom.stackView
        if (sv.currentItem && sv.currentItem.objectName === "downloadPage") return

        if (sv._cachedDownloadPage) {
            // Always refresh playlist list from backend (may have changed
            // while the page was cached — new playlists, deletions, etc.)
            if (mediaManager)
                sv._cachedDownloadPage.downloadPlaylists = mediaManager.get_movable_playlist_names()
            if (targetPlaylist) {
                sv._cachedDownloadPage.selectedPlaylist = targetPlaylist
                downloadManager.set_download_playlist(targetPlaylist)
            }
            for (var i = 0; i < sv.depth; i++) {
                if (sv.get(i) === sv._cachedDownloadPage) {
                    sv.pop(sv._cachedDownloadPage)
                    return
                }
            }
            sv.push(sv._cachedDownloadPage)
            return
        }

        var component = Qt.createComponent("DownloadPage.qml")
        if (component.status === Component.Ready) {
            sv._cachedDownloadPage = component.createObject(null, {
                stackView: sv,
                mainWindow: mediaRoom.mainWindow
            })
            if (sv._cachedDownloadPage) {
                if (targetPlaylist) {
                    sv._cachedDownloadPage.selectedPlaylist = targetPlaylist
                    downloadManager.set_download_playlist(targetPlaylist)
                }
                sv.push(sv._cachedDownloadPage)
            }
        }
    }

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Global colors
    property color backgroundColor: "black"
    property color transparentColor: "transparent"

    property real buttonPressedOpacity: 0.7
    property real buttonNormalOpacity: 1.0

    property int duration: 0
    property int position: 0
    property bool userSeeking: false
    property bool devicePlaying: false
    function refreshDevicePlaying() {
        var manager = useSpotify ? spotifyManager : mediaManager
        devicePlaying = manager ? manager.is_playing() : false
    }
    onUseSpotifyChanged: refreshDevicePlaying()
    Connections {
        target: mediaManager
        function onPlayStateChanged(playing) { if (!mediaRoom.useSpotify) mediaRoom.devicePlaying = playing }
    }
    Connections {
        target: spotifyManager
        function onPlayStateChanged(playing) { if (mediaRoom.useSpotify) mediaRoom.devicePlaying = playing }
    }
    readonly property bool showMusicDevice: App.MusicDevicePreference.enabled && App.MusicDevicePreference.isAvailable()

    property bool isShuffleEnabled: false

    // Empty playlist detection — drives download button vs controls swap
    property int localSongCount: 0
    property bool playlistEmpty: !useSpotify && localSongCount === 0

    // Spotify integration - use Spotify when user chooses it AND it's connected
    property bool useSpotify: settingsManager && settingsManager.mediaSource === "spotify" &&
                              spotifyManager && spotifyManager.is_connected()

    // Check if Spotify is available (connected) for showing toggle
    property bool spotifyAvailable: spotifyManager && spotifyManager.is_connected()

    // Spotify track info (updated directly from signals)
    property string spotifyTrackName: ""
    property string spotifyArtist: ""
    property string spotifyAlbum: ""
    property string spotifyAlbumArt: ""

    // Track info that works for both local and Spotify
    property string currentTrackName: {
        if (useSpotify && spotifyTrackName) {
            return spotifyTrackName
        }
        return currentSongText.text ? (mediaManager ? mediaManager.get_display_name(currentSongText.text) : currentSongText.text.replace('.mp3', '')) : ""
    }

    property string currentArtist: {
        if (useSpotify && spotifyArtist) {
            return spotifyArtist
        }
        return currentSongText.text ? (mediaManager ? mediaManager.get_band(currentSongText.text) : "Unknown Artist") : "Unknown Artist"
    }

    property string currentAlbum: {
        if (useSpotify && spotifyAlbum) {
            return spotifyAlbum
        }
        return currentSongText.text ? (mediaManager ? mediaManager.get_album(currentSongText.text) : "Unknown Album") : "Unknown Album"
    }

    property string currentAlbumArt: {
        if (useSpotify && spotifyAlbumArt) {
            return spotifyAlbumArt
        }
        return currentSongText.text ? (mediaManager ? mediaManager.get_album_art(currentSongText.text) || "./assets/missing_art.png" : "./assets/missing_art.png") : "./assets/missing_art.png"
    }

    // Card stack -- peek at adjacent tracks
    property int _slideDirection: 1      // 1 = next (slide from right), -1 = prev (slide from left)
    property bool _userInitiatedChange: false // true when click handler already started animation

    // Resolved album art source — tracks currentAlbumArt but falls back to
    // missing_art.png if the image fails to load (e.g. corrupted extract, stale cache)
    property string _displayAlbumArt: currentAlbumArt
    onCurrentAlbumArtChanged: _displayAlbumArt = currentAlbumArt

    // Side-card art refresh — fires on the next event-loop tick after any
    // track or shuffle change so the Python slot calls never compete with
    // animation frames for frame budget.  Uses a single batched backend
    // call (get_neighbor_album_arts) to halve PySide6 bridge crossings.
    // Also feeds hidden preloader Images so the QML cache is warm.
    Timer {
        id: sideCardRefreshTimer
        interval: 0
        onTriggered: {
            var prevUrl = "", nextUrl = ""
            if (useSpotify && spotifyManager) {
                var prevInfo = spotifyManager.get_previous_track_info()
                try { prevUrl = prevInfo ? JSON.parse(prevInfo).image || "" : "" } catch(e) {}
                var nextInfo = spotifyManager.get_next_track_info()
                try { nextUrl = nextInfo ? JSON.parse(nextInfo).image || "" : "" } catch(e) {}
            } else if (mediaManager) {
                var arts = mediaManager.get_neighbor_album_arts()
                prevUrl = arts[0] || ""
                nextUrl = arts[1] || ""
            }
            albumArtStack.prevArtSource = prevUrl
            albumArtStack.nextArtSource = nextUrl
            // Full-res background preloaders — warm the QML image cache
            prevArtPreloader.source = prevUrl
            nextArtPreloader.source = nextUrl
            // Restore side card opacity now that sources are correct
            albumArtStack.showSideCards()
        }
    }

    // Hidden preloaders — decode next/prev art at full resolution in the
    // background so the center card gets an instant cache hit on track change.
    Image {
        id: prevArtPreloader
        visible: false; width: 1; height: 1
        sourceSize: Qt.size(albumArtStack ? albumArtStack.artSize * 2 : 800, albumArtStack ? albumArtStack.artSize * 2 : 800)
        asynchronous: true; cache: true
    }
    Image {
        id: nextArtPreloader
        visible: false; width: 1; height: 1
        sourceSize: Qt.size(albumArtStack ? albumArtStack.artSize * 2 : 800, albumArtStack ? albumArtStack.artSize * 2 : 800)
        asynchronous: true; cache: true
    }

    function formatTime(ms) {
        var minutes = Math.floor(ms / 60000)
        var seconds = Math.floor((ms % 60000) / 1000)
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    // True while card animation is running + a brief settle buffer after,
    // so the blur re-render and visualizer bar Behaviors don't all resume
    // on the exact frame the bounce lands (avoids a one-frame stutter).
    // Driven by AlbumArtCarousel.animBusy via onAnimBusyChanged binding.
    property bool _cardAnimBusy: false

    // Heavy work deferred until after animation settles — FFT analysis
    // doesn't compete with animation frames.  Called by carousel's settled signal.
    function _doTrackChangeWork() {
        if (audioAnalyzer && mediaManager && !useSpotify) {
            var currentFile = mediaManager.get_current_file()
            if (currentFile) {
                var fullPath = mediaManager.get_full_file_path(currentFile)
                if (fullPath) {
                    audioAnalyzer.analyze_file(fullPath)
                    waveformContainer.lastAnalyzedFile = currentFile
                }
            }
        }
    }

    function animateTrackChange() {
        // Snap bars to zero before the card animation begins so the waveform
        // doesn't hang at stale levels. _cardAnimBusy disables bar Behaviors
        // so the drop is instant. Visualizer stays visible — bars just sit
        // at zero until the new analysis lands (avoids a multi-second hide
        // on Android where MediaCodec decode is slower than desktop ffmpeg).
        vizFadeInTimer.stop()
        _cardAnimBusy = true
        waveformContainer.dropBars()

        // Always trigger the carousel — it picks single-card or 3D mode
        // based on previewEnabled.  Heavy work deferred via settled signal.
        albumArtStack.triggerSlide()
        if (musicDeviceLoader.item) musicDeviceLoader.item.loadMedia(false)
    }

    Rectangle {
        id: mainContent        
        anchors.fill: parent
        color: transparentColor

        Item { //background
            id: backgroundContainer
            anchors.fill: parent
            z: -1  

            Grid {
                id: albumArtGrid
                anchors.fill: parent
                columns: {
                    if (settingsManager) {
                        switch(settingsManager.backgroundGrid) {
                            case "Normal": return 1;
                            case "2x2": return 2;
                            case "4x4": return 4;
                            default: return 4;
                        }
                    }
                    return 4; // default fallback
                }
                spacing: 0

                // Single blur on the entire grid instead of per-cell (16x cheaper on 4x4)
                // Freeze blur texture during card animation — avoids a full-screen
                // multi-pass re-render when grid images change source mid-transition
                layer.enabled: true
                layer.live: !mediaRoom._cardAnimBusy
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 64
                    blur: settingsManager ? Math.min(1.0, settingsManager.backgroundBlurRadius / 64) : 0.625
                    // Without this, MultiEffect auto-pads the source to fit the
                    // blur halo, and the halo bleeds past the page's bottom edge
                    // into the BottomBar (StackView doesn't clip). Clipping the
                    // halo to the source bounds keeps it inside the page.
                    autoPaddingEnabled: false
                }

                Repeater {
                    id: gridRepeater
                    model: {
                        if (settingsManager) {
                            switch(settingsManager.backgroundGrid) {
                                case "Normal": return 1;
                                case "2x2": return 4;
                                case "4x4": return 16;
                                default: return 16;
                            }
                        }
                        return 16; // default fallback
                    }

                    delegate: Item {
                        width: backgroundContainer.width / albumArtGrid.columns
                        height: backgroundContainer.height / albumArtGrid.columns

                        Image {
                            id: gridImage
                            anchors.fill: parent
                            source: mediaRoom._displayAlbumArt || "./assets/missing_art.png"
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                }
            }
            Rectangle { // Black layer — alpha is driven directly by the
                // overlay opacity slider (0% = fully transparent = no overlay).
                id: colorOverlay
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, (settingsManager ? settingsManager.backgroundOverlayOpacity : 80) / 100)
                layer.enabled: true
            }
        }


        Component.onCompleted: syncNowPlaying()

        function syncNowPlaying() {
            if (useSpotify && spotifyManager) {
                mediaRoom.duration = spotifyManager.get_duration()
                mediaRoom.position = spotifyManager.get_position()
                isShuffleEnabled = spotifyManager.is_shuffled()
                // Initialize Spotify track info
                mediaRoom.spotifyTrackName = spotifyManager.get_current_track_name()
                mediaRoom.spotifyArtist = spotifyManager.get_current_artist()
                mediaRoom.spotifyAlbum = spotifyManager.get_current_album()
                mediaRoom.spotifyAlbumArt = spotifyManager.get_current_album_art()
            } else if (mediaManager) {
                mediaRoom.duration = mediaManager.get_duration()
                mediaRoom.position = mediaManager.get_position()
                var currentFile = mediaManager.get_current_file()
                if (currentFile) {
                    currentSongText.text = currentFile
                }
                isShuffleEnabled = mediaManager.is_shuffled()
            }
            // Force initial evaluation of side card art
            sideCardRefreshTimer.restart()

            // Initialize local song count from the active playlist (not raw
            // directory scan, which misses "All Music" and other combined lists)
            if (mediaManager) {
                localSongCount = mediaManager.get_current_song_list().length
            }
        }

        Rectangle { // Volume control at top
            id: topVolumeControl
            width: parent.width * 0.75
            height: App.Spacing.mediaRoomDurationBarHeight
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                margins: App.Spacing.mediaRoomMargin
            }
            color: transparentColor
            
            RowLayout {
                anchors.fill: parent
                spacing: dp(10)

                // Volume icon control
                Control {
                    id: volumeIconControl
                    implicitWidth: App.Spacing.bottomBarMuteButtonWidth
                    implicitHeight: App.Spacing.bottomBarMuteButtonHeight
                    Layout.alignment: Qt.AlignVCenter
                    
                    background: Rectangle { color: "transparent" }
                    
                    contentItem: Item {
                        Image {
                            id: volumeIconImage
                            anchors.centerIn: parent
                            width: parent.width
                            height: parent.height
                            source: getVolumeIconSource()
                            sourceSize: Qt.size(width * 2, height * 2)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            antialiasing: true
                            mipmap: false
                            visible: false
                        }
                        
                        ColorOverlay {
                            anchors.fill: volumeIconImage
                            source: volumeIconImage
                            color: App.Style.mediaRoomSeekColor
                            
                            layer.enabled: true
                            layer.effect: DropShadow {
                                transparentBorder: true
                                horizontalOffset: 4       
                                verticalOffset: 4         
                                radius: 8.0               
                                samples: 17               
                                color: "#B0000000"        
                            }
                        }
                    }
                    
                    MouseArea {
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
                
                // Volume slider
                Slider {
                    id: volumeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: volumeControl.currentValue
                    
                    // Increase touch area
                    implicitHeight: App.Spacing.mediaRoomProgressSliderHeight * 4
                    
                    background: Rectangle {
                        x: volumeSlider.leftPadding
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                        width: volumeSlider.availableWidth
                        height: App.Spacing.mediaRoomProgressSliderHeight
                        radius: height / 2
                        color: App.Style.hoverColor
                        
                        Rectangle {
                            width: volumeSlider.visualPosition * parent.width
                            height: parent.height
                            radius: height / 2
                            color: App.Style.volumeSliderColor
                        }
                    }
                    
                    // Handle styling
                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                        width: App.Spacing.mediaRoomSliderButtonWidth
                        height: App.Spacing.mediaRoomSliderButtonHeight
                        radius: App.Spacing.mediaRoomSliderButtonRadius
                        color: App.Style.accent
                        visible: true
                    }
                    
                    // Enhanced touch area
                    MouseArea {
                        anchors.fill: parent
                        anchors.topMargin: -dp(10)
                        anchors.bottomMargin: -dp(10)

                        onPressed: function(mouse) {
                            var newPos = Math.max(0, Math.min(1, (mouseX - volumeSlider.leftPadding) / volumeSlider.availableWidth))
                            volumeSlider.value = volumeSlider.from + newPos * (volumeSlider.to - volumeSlider.from)
                            volumeSlider.pressed = true
                            mouse.accepted = false
                        }
                        
                        onReleased: function(mouse) {
                            volumeSlider.pressed = false
                            mouse.accepted = false
                        }
                    }
                    
                    // Volume change logic
                    onValueChanged: {
                        volumeControl.currentValue = value
                        // Single dispatch to settings + every audio output.
                        volumeController.applyVolume(Math.round(value))

                        // Unmute if volume was raised while user is dragging the slider
                        if (mediaManager && volumeSlider.pressed && value > 0 && volumeControl.isMuted) {
                            mediaManager.toggle_mute()
                        }

                        // Update icon
                        topVolumeControl.updateVolumeIcon()
                    }
                }
                
                // Volume percentage text
                Text {
                    id: volumePercentText
                    text: Math.round(volumeSlider.value) + "%"
                    color: App.Style.mediaRoomSeekColor
                    font.pixelSize: App.Spacing.mediaRoomSliderDurationText
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: dp(40)
                }
            }

            // Volume control state properties
            QtObject {
                id: volumeControl
                property int currentValue: 0
                property bool isMuted: mediaManager ? mediaManager.is_muted() : false

                Component.onCompleted: {
                    // Use the unified volume from Octave settings
                    if (settingsManager) {
                        currentValue = settingsManager.currentVolume
                    } else if (mediaManager) {
                        var volume = mediaManager.getVolume()
                        currentValue = Math.round(Math.sqrt(volume) * 100)
                    }
                    isMuted = mediaManager ? mediaManager.is_muted() : false
                    topVolumeControl.updateVolumeIcon()
                }
            }
            
            // Functions
            function getVolumeIconSource() {
                if (volumeControl.isMuted || volumeControl.currentValue === 0) {
                    return "./assets/mute_on.svg"
                }
                if (volumeControl.currentValue < 20) return "./assets/mute_off_med.svg"
                if (volumeControl.currentValue > 90) return "./assets/mute_off_low.svg"
                return "./assets/mute_off_low.svg"
            }
            
            function updateVolumeIcon() {
                volumeIconImage.source = getVolumeIconSource()
            }
            
            // Connections for volume sync
            Connections {
                target: mediaManager
                function onMuteChanged(muted) {
                    volumeControl.isMuted = muted
                    topVolumeControl.updateVolumeIcon()
                }
                
                function onVolumeChanged(volume) {
                    if (!volumeSlider.pressed) {
                        var volumePercent = Math.round(Math.sqrt(volume) * 100)
                        volumeControl.currentValue = volumePercent
                        volumeSlider.value = volumePercent
                    }
                    topVolumeControl.updateVolumeIcon()
                }
            }
        }

        // Download Button (top right corner)
        Control {
            id: downloadButton
            visible: true
            width: App.Spacing.bottomBarNavButtonWidth
            height: App.Spacing.bottomBarNavButtonHeight
            z: 3
            anchors {
                top: parent.top
                right: parent.right
                topMargin: App.Spacing.mediaRoomMargin
                rightMargin: App.Spacing.mediaRoomMargin
            }

            background: Rectangle {
                color: "transparent"
                radius: dpMin(8, 2)
                border.color: App.Style.accent
                border.width: 1
                scale: dlMouseArea.pressed ? 0.8 : 1.0
                opacity: dlMouseArea.pressed ? 0.7 : 1.0
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
                scale: dlMouseArea.pressed ? 0.8 : 1.0
                opacity: dlMouseArea.pressed ? 0.7 : 1.0
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
                    id: dlButtonImage
                    anchors.centerIn: parent
                    width: parent.width * 0.7
                    height: parent.height * 0.7
                    source: "./assets/download_button.svg"
                    sourceSize: Qt.size(width * 2, height * 2)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    antialiasing: true
                    mipmap: true
                    visible: false
                }

                ColorOverlay {
                    anchors.fill: dlButtonImage
                    source: dlButtonImage
                    color: App.Style.accent
                }
            }

            MouseArea {
                id: dlMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: mediaRoom.openDownloadPage()
            }
        }

        Rectangle { //media controls container
            id: mediaControlsContainer
            visible: !playlistEmpty
            width: App.Spacing.applicationWidth * App.Spacing.mediaRoomControlsContainerWidth
            height: App.Spacing.applicationHeight * App.Spacing.mediaRoomControlsContainerHeight
            anchors {
                top: topVolumeControl.bottom
                bottom: durationBar.top
                horizontalCenter: parent.horizontalCenter
                // Use verticalCenter to ensure it's centered between the two elements
                margins: App.Spacing.mediaRoomMargin
            }
            color: transparentColor
        

            RowLayout {
                anchors.fill: parent
                spacing: App.Spacing.mediaRoomSpacing

                // Left side - Controls and Metadata
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * (mediaRoom.showMusicDevice ? 0.56 : 0.6)
                    Layout.maximumWidth: parent.width * (mediaRoom.showMusicDevice ? 0.56 : 0.6)
                    Layout.leftMargin: dp(20)
                    spacing: App.Spacing.mediaRoomSpacing * 2

                    // Spacer to push controls toward center
                    Item { Layout.fillHeight: true }

                    // Media Controls Row
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        spacing: App.Spacing.mediaRoomBetweenButton

                        Control { //Previous button
                            id: previousControl
                            implicitHeight: App.Spacing.mediaRoomPreviousButtonHeight
                            implicitWidth: App.Spacing.mediaRoomPreviousButtonWidth

                            property bool _tiltEnabled: settingsManager && settingsManager.show3DButtonTilt
                            property real tiltAngle: (_tiltEnabled && prevMouseArea.pressed) ? 15 : 0
                            scale: (_tiltEnabled && prevMouseArea.pressed) ? 0.9 : 1.0
                            transform: Rotation {
                                axis { x: 1; y: 0; z: 0 }
                                angle: previousControl.tiltAngle
                                origin.x: previousControl.width / 2
                                origin.y: previousControl.height / 2
                            }
                            Behavior on tiltAngle { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                            Behavior on scale { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }

                            background: Rectangle {
                                color: "transparent"
                            }
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
                                    mipmap: false
                                    visible: false
                                }
                                ColorOverlay {
                                    anchors.fill: previousButtonImage
                                    source: previousButtonImage
                                    color: App.Style.mediaRoomPreviousButton
                                    opacity: prevMouseArea.pressed ? buttonPressedOpacity : buttonNormalOpacity
                                    layer.enabled: true
                                    layer.effect: DropShadow {
                                        transparentBorder: true
                                        horizontalOffset: 4
                                        verticalOffset: 4
                                        radius: 8.0
                                        samples: 17
                                        color: "#B0000000"
                                    }
                                }
                            }
                            MouseArea {
                                id: prevMouseArea
                                anchors.fill: parent
                                anchors.margins: -App.Spacing.mediaRoomBetweenButton / 2
                                onClicked: {
                                    mediaRoom._slideDirection = -1
                                    // Fire animation BEFORE the Python call — enter card
                                    // starts at opacity 0 so stale art is invisible.
                                    // Backend data arrives while card is still transparent.
                                    mediaRoom._userInitiatedChange = true
                                    mediaRoom.animateTrackChange()
                                    if (useSpotify) {
                                        // Optimistically swap art to the previous track's image
                                        if (albumArtStack.prevArtSource != "")
                                            mediaRoom._displayAlbumArt = albumArtStack.prevArtSource.toString()
                                        spotifyManager.previous_track()
                                    } else {
                                        mediaManager.previous_track()
                                    }
                                }
                            }
                        }

                        Control { //Play Button
                            id: playControl
                            implicitHeight: App.Spacing.mediaRoomPlayButtonHeight
                            implicitWidth: App.Spacing.mediaRoomPlayButtonWidth

                            property bool _tiltEnabled: settingsManager && settingsManager.show3DButtonTilt
                            property real tiltAngle: (_tiltEnabled && playMouseArea.pressed) ? 25 : 0
                            scale: (_tiltEnabled && playMouseArea.pressed) ? 0.85 : 1.0
                            transform: Rotation {
                                axis { x: 1; y: 0; z: 0 }
                                angle: playControl.tiltAngle
                                origin.x: playControl.width / 2
                                origin.y: playControl.height / 2
                            }
                            Behavior on tiltAngle { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }
                            Behavior on scale { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }

                            background: Rectangle {
                                color: "transparent"
                            }
                            contentItem: Item {
                                Image {
                                    id: playButtonImage
                                    anchors.centerIn: parent
                                    width: parent.width
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
                                    anchors.fill: playButtonImage
                                    source: playButtonImage
                                    color: App.Style.mediaRoomPlayButton
                                    opacity: playMouseArea.pressed ? buttonPressedOpacity : buttonNormalOpacity
                                    layer.enabled: true
                                    layer.effect: DropShadow {
                                        transparentBorder: true
                                        horizontalOffset: 4
                                        verticalOffset: 4
                                        radius: 8.0
                                        samples: 17
                                        color: "#B0000000"
                                    }
                                }
                            }
                            MouseArea {
                                id: playMouseArea
                                anchors.fill: parent
                                anchors.margins: -App.Spacing.mediaRoomBetweenButton / 2
                                onClicked: {
                                    if (useSpotify) {
                                        spotifyManager.toggle_play()
                                    } else {
                                        mediaManager.toggle_play()
                                    }
                                }
                            }
                        }
                        
                        Control { //Next Button
                            id: nextControl
                            implicitHeight: App.Spacing.mediaRoomNextButtonHeight
                            implicitWidth: App.Spacing.mediaRoomNextButtonWidth

                            property bool _tiltEnabled: settingsManager && settingsManager.show3DButtonTilt
                            property real tiltAngle: (_tiltEnabled && nextMouseArea.pressed) ? 15 : 0
                            scale: (_tiltEnabled && nextMouseArea.pressed) ? 0.9 : 1.0
                            transform: Rotation {
                                axis { x: 1; y: 0; z: 0 }
                                angle: nextControl.tiltAngle
                                origin.x: nextControl.width / 2
                                origin.y: nextControl.height / 2
                            }
                            Behavior on tiltAngle { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
                            Behavior on scale { NumberAnimation { duration: settingsManager ? settingsManager.buttonTiltDuration : 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }

                            background: Rectangle {
                                color: "transparent"
                            }
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
                                    color: App.Style.mediaRoomNextButton
                                    opacity: nextMouseArea.pressed ? buttonPressedOpacity : buttonNormalOpacity
                                    layer.enabled: true
                                    layer.effect: DropShadow {
                                        transparentBorder: true
                                        horizontalOffset: 4
                                        verticalOffset: 4
                                        radius: 8.0
                                        samples: 17
                                        color: "#B0000000"
                                    }
                                }
                            }
                            MouseArea {
                                id: nextMouseArea
                                anchors.fill: parent
                                anchors.margins: -App.Spacing.mediaRoomBetweenButton / 2
                                onClicked: {
                                    mediaRoom._slideDirection = 1
                                    mediaRoom._userInitiatedChange = true
                                    mediaRoom.animateTrackChange()
                                    if (useSpotify) {
                                        // Optimistically swap art to the next track's image
                                        // so it's ready when the coverflow animation lands
                                        if (albumArtStack.nextArtSource != "")
                                            mediaRoom._displayAlbumArt = albumArtStack.nextArtSource.toString()
                                        spotifyManager.next_track()
                                    } else {
                                        mediaManager.next_track()
                                    }
                                }
                            }
                        }
                    }

                    // Song metadata section (below controls)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        spacing: dp(4)

                        // Song title with scrolling
                        Item {
                            id: songTitleContainer
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            height: Math.ceil(App.Spacing.mediaRoomMetaDataSongText * 1.4)

                            Flickable {
                                id: songTitleFlickable
                                anchors.centerIn: parent
                                width: Math.min(songTitleText.width, parent.width)
                                height: parent.height
                                contentWidth: songTitleText.width
                                contentHeight: parent.height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick

                                Text {
                                    id: songTitleText
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: currentTrackName || "No track selected"
                                    color: App.Style.metadataColor
                                    font.pixelSize: App.Spacing.mediaRoomMetaDataSongText
                                    font.bold: true
                                    font.family: mediaRoom.globalFont
                                }

                                Timer {
                                    id: songScrollTimer
                                    property real containerWidth: songTitleFlickable.parent ? songTitleFlickable.parent.width : 200
                                    interval: 3000
                                    running: songTitleText.width > containerWidth
                                    repeat: true
                                    onTriggered: {
                                        if (songTitleFlickable.contentX === 0) {
                                            songScrollAnimation.to = songTitleText.width - songTitleFlickable.width;
                                            songScrollAnimation.start();
                                        } else {
                                            songScrollAnimation.to = 0;
                                            songScrollAnimation.start();
                                        }
                                    }
                                }

                                NumberAnimation {
                                    id: songScrollAnimation
                                    target: songTitleFlickable
                                    property: "contentX"
                                    duration: settingsManager ? settingsManager.textScrollSpeed : 5000
                                    easing.type: Easing.InOutQuad
                                    onFinished: songScrollTimer.restart()
                                }
                            }
                        }

                        // Artist and album info
                        Item {
                            id: metadataContainer
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            height: Math.ceil(App.Spacing.mediaRoomMetaDataBandText * 1.4)

                            Flickable {
                                id: metadataFlickable
                                anchors.centerIn: parent
                                width: Math.min(metadataRow.width, parent.width)
                                height: parent.height
                                contentWidth: metadataRow.width
                                contentHeight: parent.height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick

                                Row {
                                    id: metadataRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: dp(10)

                                    Text {
                                        text: currentArtist
                                        color: App.Style.metadataColor
                                        font.pixelSize: App.Spacing.mediaRoomMetaDataBandText
                                        font.family: mediaRoom.globalFont
                                        opacity: 0.7
                                    }
                                    Text {
                                        text: "\u2022"
                                        color: App.Style.metadataColor
                                        font.pixelSize: App.Spacing.mediaRoomMetaDataAlbumText
                                        font.family: mediaRoom.globalFont
                                        opacity: 0.8
                                    }
                                    Text {
                                        text: currentAlbum
                                        color: App.Style.metadataColor
                                        font.pixelSize: App.Spacing.mediaRoomMetaDataAlbumText
                                        font.family: mediaRoom.globalFont
                                        opacity: 0.8
                                    }
                                }

                                Timer {
                                    id: metadataScrollTimer
                                    property real containerWidth: metadataFlickable.parent ? metadataFlickable.parent.width : 200
                                    interval: 3000
                                    running: metadataRow.width > containerWidth
                                    repeat: true
                                    onTriggered: {
                                        if (metadataFlickable.contentX === 0) {
                                            metadataScrollAnimation.to = metadataRow.width - metadataFlickable.width;
                                            metadataScrollAnimation.start();
                                        } else {
                                            metadataScrollAnimation.to = 0;
                                            metadataScrollAnimation.start();
                                        }
                                    }
                                }

                                NumberAnimation {
                                    id: metadataScrollAnimation
                                    target: metadataFlickable
                                    property: "contentX"
                                    duration: settingsManager ? settingsManager.textScrollSpeed : 5000
                                    easing.type: Easing.InOutQuad
                                    onFinished: metadataScrollTimer.restart()
                                }
                            }
                        }
                    }

                    // Spacer to push content toward center
                    Item { Layout.fillHeight: true }

                    // Hidden container for current song text (used by other components)
                    Item {
                        id: currentSongTextContainer
                        visible: false
                        Text { id: currentSongText; text: "" }
                    }

                }


                Item {
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * (mediaRoom.showMusicDevice ? 0.44 : 0.4)
                    Layout.maximumWidth: parent.width * (mediaRoom.showMusicDevice ? 0.44 : 0.4)
                    Layout.alignment: Qt.AlignVCenter

                    Loader {
                        id: musicDeviceLoader
                        anchors.fill: parent
                        active: mediaRoom.showMusicDevice && mediaRoom.visible
                        source: active ? "MusicDeviceScene.qml" : ""
                        onLoaded: mediaRoom.refreshDevicePlaying()
                    }
                    Binding { target: musicDeviceLoader.item; property: "device"; value: App.MusicDevicePreference.mode; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "playing"; value: mediaRoom.devicePlaying; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "artSource"; value: mediaRoom._displayAlbumArt; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "trackTitle"; value: mediaRoom.currentTrackName; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "artist"; value: mediaRoom.currentArtist; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "playbackPosition"; value: mediaRoom.position; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "playbackDuration"; value: mediaRoom.duration; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "hasTrack"; value: !mediaRoom.playlistEmpty && mediaRoom.currentTrackName !== ""; when: musicDeviceLoader.status === Loader.Ready }
                    Binding { target: musicDeviceLoader.item; property: "progress"; value: mediaRoom.duration > 0 ? mediaRoom.position / mediaRoom.duration : 0; when: musicDeviceLoader.status === Loader.Ready }

                    AlbumArtCarousel {
                        id: albumArtStack
                        anchors.fill: parent
                        // Keep the carousel's transition/settled lifecycle alive for track analysis.
                        // Only its presentation is hidden while a device is successfully loaded.
                        visible: !(musicDeviceLoader.item && musicDeviceLoader.item.ready)

                        // Settings bindings
                        previewEnabled: settingsManager && settingsManager.show3DAlbumPreview
                        roundedArt: settingsManager && settingsManager.roundedAlbumArt
                        artRadius: dp(settingsManager ? settingsManager.albumArtCornerRadius : 16)
                        vinylMode: settingsManager && settingsManager.vinylRecordMode
                        sideCardAngle: settingsManager ? settingsManager.sideCardAngle : 30
                        sideCardOpacity: settingsManager ? settingsManager.sideCardOpacity : 0.4
                        showShadow: settingsManager && settingsManager.showAlbumArtShadow
                        transitionStyle: settingsManager ? settingsManager.albumArtTransition : "Crossfade"

                        // Media state
                        artSource: mediaRoom._displayAlbumArt
                        direction: mediaRoom._slideDirection
                        playbackPosition: mediaRoom.position
                        playbackDuration: mediaRoom.duration

                        // Sync busy flag back to MediaRoom (drives blur freeze, viz pause, etc.)
                        onAnimBusyChanged: mediaRoom._cardAnimBusy = animBusy

                        onAnimationFinished: sideCardRefreshTimer.restart()

                        onSettled: mediaRoom._doTrackChangeWork()

                        onArtClicked: albumArtPopup.open()

                        onArtLoadFailed: mediaRoom._displayAlbumArt = "./assets/missing_art.png"
                    }
                    Text {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        visible: mediaRoom.showMusicDevice && (musicDeviceLoader.status === Loader.Error || (musicDeviceLoader.item && musicDeviceLoader.item.error !== ""))
                        text: "Device view unavailable. Showing album art."
                        color: App.Style.metadataColor
                        font.pixelSize: dp(12)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }

        }

        // Empty playlist message
        Text {
            visible: playlistEmpty
            anchors.centerIn: parent
            text: "No songs in this playlist yet"
            color: App.Style.metadataColor
            font.pixelSize: dp(16)
            font.family: mediaRoom.globalFont
            opacity: 0.6
        }

        // Waveform Visualizer
        Item {
            id: waveformContainer
            objectName: "waveformContainer"
            width: parent.width * 0.75
            height: dp(40)
            anchors {
                bottom: durationBar.top
                horizontalCenter: parent.horizontalCenter
                bottomMargin: dp(10)
            }
            visible: settingsManager && settingsManager.showWaveformVisualizer && !mediaRoom.useSpotify && !playlistEmpty
            // Kept for back-compat with animateTrackChange() / onAnalysisComplete
            // handler below. No longer hides the visualizer — bars just drop
            // to zero on track change (via dropBars()) and animate back up
            // when the new analysis lands. Previously we faded the whole
            // visualizer out for up to a few seconds on Android (MediaCodec
            // decode is slower than desktop ffmpeg), which looked broken.
            property bool _vizFadedOut: false
            opacity: visible ? 1.0 : 0.0

            // Track playback state reactively (method calls aren't reactive in bindings)
            property bool isPlaying: false
            property string lastAnalyzedFile: ""

            // 96 bars at 60 FPS with pill-shaped fancy bars
            property int timerInterval: 16
            property bool smoothBars: true
            property int animDuration: 120
            property int animEasing: Easing.InOutSine
            property bool fancyBars: true

            Behavior on opacity {
                // Disabled during _cardAnimBusy so the drop is instant;
                // enabled for the smooth fade-in after analysis completes.
                enabled: !mediaRoom._cardAnimBusy
                NumberAnimation { duration: 500; easing.type: Easing.InOutQuad }
            }

            // Tiny delay so the backend's initial zero-frame emission clears
            // before we start showing anything — then the opacity fade and
            // bar-height growth animate together on the very next data tick.
            Timer {
                id: vizFadeInTimer
                interval: 50
                onTriggered: waveformContainer._vizFadedOut = false
            }

            // Waveform bars
            Row {
                id: waveformBars
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                height: parent.height
                spacing: dp(1)

                // Cache all bars as a single texture during card animation —
                // collapses 96 individual gradient draw calls into one texture blit
                layer.enabled: true
                layer.live: !mediaRoom._cardAnimBusy

                property int numBars: audioAnalyzer ? audioAnalyzer.get_num_bars() : 96

                Repeater {
                    id: waveformRepeater
                    model: waveformBars.numBars

                    Rectangle {
                        property int barLevel: 0
                        property bool fancy: waveformContainer.fancyBars

                        width: Math.max(2, (waveformContainer.width - (waveformBars.numBars - 1) * waveformBars.spacing) / waveformBars.numBars)
                        height: Math.max(2, (barLevel / 8) * waveformContainer.height)
                        anchors.bottom: parent.bottom

                        radius: fancy ? width * 0.4 : 0

                        // Always set color as fallback — Qt ignores color when gradient is active,
                        // but if the gradient fails to render (Windows GPU quirk) the bar stays visible
                        color: App.Style.accent

                        gradient: fancy ? barGradient : null

                        // Breathing opacity — quiet bars fade, loud bars pop
                        opacity: fancy ? (0.4 + (barLevel / 8) * 0.6) : 0.8

                        Gradient {
                            id: barGradient
                            GradientStop { position: 0.0; color: Qt.darker(App.Style.accent, 1.4) }
                            GradientStop { position: 0.5; color: App.Style.accent }
                            GradientStop { position: 1.0; color: Qt.lighter(App.Style.accent, 1.6) }
                        }

                        Behavior on height {
                            enabled: waveformContainer.smoothBars && !mediaRoom._cardAnimBusy
                            NumberAnimation { duration: waveformContainer.animDuration; easing.type: waveformContainer.animEasing }
                        }

                        Behavior on opacity {
                            enabled: fancy && !mediaRoom._cardAnimBusy
                            NumberAnimation { duration: waveformContainer.animDuration; easing.type: waveformContainer.animEasing }
                        }
                    }
                }
            }

            // Connect to audio analyzer — update each bar's level directly
            Connections {
                target: audioAnalyzer
                enabled: waveformContainer.visible && !mediaRoom._cardAnimBusy

                function onFftDataChanged(levels) {
                    for (var i = 0; i < waveformRepeater.count; i++) {
                        var item = waveformRepeater.itemAt(i)
                        if (item) {
                            item.barLevel = (levels && levels[i]) || 0
                        }
                    }
                }
            }

            // Fade the visualizer back in once the new track's analysis lands.
            // Separate block so it isn't gated by _cardAnimBusy (analysis
            // finishes after the card animation has already settled).
            Connections {
                target: audioAnalyzer
                enabled: waveformContainer.visible

                function onAnalysisComplete() {
                    // Only arm the fade-in if we're actually waiting for it
                    // (guards against stale signals from a superseded analysis)
                    if (waveformContainer._vizFadedOut && !mediaRoom._cardAnimBusy) {
                        vizFadeInTimer.restart()
                    }
                }
            }

            // Update position periodically at the visualizer frame rate
            Timer {
                id: waveformUpdateTimer
                interval: waveformContainer.timerInterval
                running: waveformContainer.visible && waveformContainer.isPlaying && !mediaRoom._cardAnimBusy
                repeat: true

                onTriggered: {
                    if (audioAnalyzer && audioAnalyzer.is_analyzed() && mediaManager) {
                        audioAnalyzer.update_position(mediaManager.get_position() / 1000.0)
                    }
                }
            }

            // Handle media manager signals
            Connections {
                target: mediaManager
                enabled: settingsManager && settingsManager.showWaveformVisualizer

                function onCurrentMediaChanged(filename) {
                    // analyze_file is handled by _doTrackChangeWork() (deferred until after card animation)
                }

                function onPlayStateChanged(playing) {
                    waveformContainer.isPlaying = playing
                    if (audioAnalyzer) {
                        audioAnalyzer.set_active(playing)
                    }
                }
            }

            // Immediately zero all bars (snaps because Behaviors are disabled when _cardAnimBusy)
            function dropBars() {
                for (var i = 0; i < waveformRepeater.count; i++) {
                    var item = waveformRepeater.itemAt(i)
                    if (item) item.barLevel = 0
                }
            }

            // Initialize waveform when needed
            function initializeWaveform() {
                if (audioAnalyzer && mediaManager && !mediaRoom.useSpotify) {
                    var currentFile = mediaManager.get_current_file()
                    if (currentFile) {
                        var fullPath = mediaManager.get_full_file_path(currentFile)
                        if (fullPath) {
                            audioAnalyzer.analyze_file(fullPath)
                            waveformContainer.lastAnalyzedFile = currentFile
                        }
                    }
                    // Set active state based on current playback
                    waveformContainer.isPlaying = mediaManager.is_playing()
                    audioAnalyzer.set_active(waveformContainer.isPlaying)
                }
            }

            // Re-initialize when becoming visible
            onVisibleChanged: {
                if (visible) {
                    initializeWaveform()
                }
            }

            // Also initialize on component load
            Component.onCompleted: {
                if (waveformContainer.visible) {
                    initializeWaveform()
                }
            }
        }

        Rectangle { //duration bar
            id: durationBar
            visible: !playlistEmpty
            width: parent.width * 0.75
            height: App.Spacing.mediaRoomDurationBarHeight
            anchors {
                bottom: parent.bottom
                horizontalCenter: parent.horizontalCenter
                margins: dp(20)
            }
            color: transparentColor

            RowLayout {
                anchors.fill: parent
                spacing: dp(20)

                Text {
                    id: positionText
                    text: formatTime(mediaRoom.position)
                    color: App.Style.mediaRoomSeekColor
                    font.pixelSize: App.Spacing.mediaRoomSliderDurationText
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: dp(40)  // Added minimum width for consistent layout
                }

                Slider {
                    id: progressSlider
                    Layout.fillWidth: true
                    from: 0
                    to: mediaRoom.duration > 0 ? mediaRoom.duration : 1
                    value: mediaRoom.position
                    enabled: mediaRoom.duration > 0
                    
                    // Increase the implicit height to provide a larger touch area
                    implicitHeight: App.Spacing.mediaRoomProgressSliderHeight * 4 // Increased touch area height
                    
                    // Improved slider styling
                    background: Rectangle {
                        x: progressSlider.leftPadding
                        y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                        width: progressSlider.availableWidth
                        height: App.Spacing.mediaRoomProgressSliderHeight
                        radius: height / 2
                        color: Qt.rgba(App.Style.mediaRoomSeekColor.r, App.Style.mediaRoomSeekColor.g, App.Style.mediaRoomSeekColor.b, 0.15)

                        Rectangle {
                            width: progressSlider.visualPosition * parent.width
                            height: parent.height
                            radius: height / 2
                            color: App.Style.mediaRoomSeekColor
                        }
                    }

                    // Fixed handle visibility
                    handle: Item {
                        x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                        y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                        width: App.Spacing.mediaRoomSliderButtonWidth
                        height: App.Spacing.mediaRoomSliderButtonHeight

                        // Glow rectangle behind handle (environment-gated)
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width + 6
                            height: parent.height + 6
                            radius: App.EnvironmentTheme.active.sliderHandleRadius === -1
                                ? (width / 2) : dpMin(App.EnvironmentTheme.active.sliderHandleRadius + 3, 2)
                            color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                            visible: App.EnvironmentTheme.active.sliderHandleGlow
                        }

                        // Actual handle
                        Rectangle {
                            id: seekHandle
                            anchors.fill: parent
                            radius: App.EnvironmentTheme.active.sliderHandleRadius === -1
                                ? (width / 2) : dpMin(App.EnvironmentTheme.active.sliderHandleRadius, 2)
                            color: progressSlider.pressed ? Qt.darker(App.Style.accent, 1.15) : App.Style.accent

                            Behavior on color { ColorAnimation { duration: 150 } }

                            layer.enabled: true
                            layer.effect: DropShadow {
                                transparentBorder: true
                                horizontalOffset: 2
                                verticalOffset: 2
                                radius: 6.0
                                samples: 13
                                color: "#80000000"
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        // Add extra padding to make it easier to touch
                        anchors.topMargin: -dp(10)
                        anchors.bottomMargin: -dp(10)

                        onPressed: function(mouse) {
                            // Calculate value based on mouse position
                            var newPos = Math.max(0, Math.min(1, (mouseX - progressSlider.leftPadding) / progressSlider.availableWidth))
                            progressSlider.value = progressSlider.from + newPos * (progressSlider.to - progressSlider.from)
                            progressSlider.pressed = true
                            userSeeking = true
                            mouse.accepted = false  // Allow the event to propagate to the Slider
                        }
                        onReleased: function(mouse) {
                            progressSlider.pressed = false
                            userSeeking = false
                            if (useSpotify) {
                                spotifyManager.set_position(progressSlider.value)
                            } else {
                                mediaManager.set_position(progressSlider.value)
                            }
                            mediaRoom.position = progressSlider.value
                            mouse.accepted = false
                        }
                    }

                    onPressedChanged: {
                        if (pressed) {
                            userSeeking = true
                        } else {
                            userSeeking = false
                            if (useSpotify) {
                                spotifyManager.set_position(value)
                            } else {
                                mediaManager.set_position(value)
                            }
                            mediaRoom.position = value
                        }
                    }

                    onMoved: {
                        if (userSeeking) {
                            mediaRoom.position = value
                        }
                    }
                }

                Text {
                    id: durationText
                    text: formatTime(mediaRoom.duration)
                    color: App.Style.mediaRoomSeekColor
                    font.pixelSize: App.Spacing.mediaRoomSliderDurationText
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: dp(40)  // Added minimum width for consistent layout
                }

                // Source toggle button (Local/Spotify)
                Control {
                    id: sourceToggleButton
                    visible: spotifyAvailable  // Only show when Spotify is connected
                    implicitWidth: App.Spacing.mediaRoomShuffleButtonWidth * 1.8
                    implicitHeight: App.Spacing.mediaRoomShuffleButtonHeight
                    background: Rectangle {
                        color: useSpotify ? "#1DB954" : App.Style.mediaRoomToggleShade
                        radius: height / 2
                        border.color: useSpotify ? "#1DB954" : App.Style.secondaryTextColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: useSpotify ? "Spotify" : "Local"
                        color: useSpotify ? "white" : App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.mediaRoomSliderDurationText * 0.9
                        font.bold: true
                        font.family: mediaRoom.globalFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: sourceToggleMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            // Pause current source before switching
                            if (useSpotify) {
                                // Currently on Spotify, pause it before switching to local
                                if (spotifyManager && spotifyManager.is_playing()) {
                                    spotifyManager.pause()
                                }
                            } else {
                                // Currently on local, pause it before switching to Spotify
                                if (mediaManager && mediaManager.is_playing()) {
                                    mediaManager.pause()
                                }
                            }

                            // Toggle the source
                            if (settingsManager) {
                                settingsManager.toggle_media_source()
                            }
                        }
                    }
                    ToolTip.visible: sourceToggleMouseArea.containsMouse
                    ToolTip.text: useSpotify ? "Switch to local files" : "Switch to Spotify"
                    ToolTip.delay: 500
                }

                // Spotify connect button (show when credentials exist but not connected)
                Control {
                    id: spotifyConnectButton
                    visible: !spotifyAvailable && spotifyManager && spotifyManager.has_credentials()
                    implicitWidth: App.Spacing.mediaRoomShuffleButtonWidth * 1.8
                    implicitHeight: App.Spacing.mediaRoomShuffleButtonHeight
                    background: Rectangle {
                        color: spotifyConnectMouseArea.containsMouse ? "#1DB954" : "#1a1a1a"
                        radius: height / 2
                        border.color: "#1DB954"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: "Connect"
                        color: spotifyConnectMouseArea.containsMouse ? "white" : "#1DB954"
                        font.pixelSize: App.Spacing.mediaRoomSliderDurationText * 0.9
                        font.bold: true
                        font.family: mediaRoom.globalFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: spotifyConnectMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (spotifyManager) {
                                spotifyManager.authenticate()
                            }
                        }
                    }
                    ToolTip.visible: spotifyConnectMouseArea.containsMouse
                    ToolTip.text: "Connect to Spotify (opens browser)"
                    ToolTip.delay: 500
                }
            }
        }

        // Shuffle Button — bottom left corner, centered between edge and duration bar
        Control {
            id: shuffleButton
            visible: !playlistEmpty
            width: durationBar.height * 1.96875
            height: durationBar.height * 1.96875
            anchors.verticalCenter: durationBar.verticalCenter
            anchors.verticalCenterOffset: -dp(15)
            x: (durationBar.x - width) / 2
            background: Rectangle {
                color: isShuffleEnabled ? App.Style.mediaRoomToggleShade : "transparent"
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
                    color: isShuffleEnabled ?
                        App.Style.bottomBarActiveToggleButton :
                        App.Style.bottomBarVolumeButton
                }
            }
            MouseArea {
                id: shuffleMouseArea
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

    // Track local song count for empty-state detection (always active).
    // When a playlist transitions from empty → has songs (first download
    // landed), auto-play the first track so the room comes alive.
    Connections {
        target: mediaManager
        function onMediaListChanged(files) {
            var wasEmpty = mediaRoom.localSongCount === 0
            mediaRoom.localSongCount = files.length
            if (wasEmpty && files.length > 0 && !mediaRoom.useSpotify) {
                mediaManager.play_file(files[0])
            }
        }
    }

    // Local media manager connections (only apply when not using Spotify)
    Connections {
        target: mediaManager
        enabled: !useSpotify

        function onPlayStateChanged(playing) {
            playButtonImage.source = playing ?
                "./assets/pause_button.svg" : "./assets/play_button.svg"
            if (playing) {
                mediaRoom.duration = mediaManager.get_duration()
                mediaRoom.position = mediaManager.get_position()
            }
        }

        function onDurationChanged(duration) {
            mediaRoom.duration = duration
        }

        function onPositionChanged(position) {
            if (!userSeeking) {
                mediaRoom.position = position
                progressSlider.value = position
            }
        }

        function onCurrentMediaChanged(filename) {
            playButtonImage.source = "./assets/pause_button.svg"
            mediaRoom.position = 0
            progressSlider.value = 0
            currentSongText.text = filename
            sideCardRefreshTimer.restart()

            if (mediaRoom._userInitiatedChange) {
                // Animation already started by click handler — carousel handles
                // busy flag release and deferred work via settled signal.
                mediaRoom._userInitiatedChange = false
            } else {
                // Auto-advance or external track change — trigger animation
                mediaRoom.animateTrackChange()
            }
        }
        function onShuffleStateChanged(enabled) {
            if (!useSpotify) {
                isShuffleEnabled = enabled
                // Shuffle reorders the playlist — neighbors changed
                sideCardRefreshTimer.restart()
            }
        }
    }

    Connections {
        target: mediaManager
        function onMuteChanged(muted) {
            volumeControl.isMuted = muted
            topVolumeControl.updateVolumeIcon()
        }

        function onVolumeChanged(volume) {
            if (!volumeSlider.pressed) {
                var volumePercent = Math.round(Math.sqrt(volume) * 100)
                volumeControl.currentValue = volumePercent
                volumeSlider.value = volumePercent
            }
            topVolumeControl.updateVolumeIcon()
        }
    }

    // Spotify playback connections (always active to receive updates)
    Connections {
        target: spotifyManager

        function onPlayStateChanged(playing) {
            // Only update UI if we're in Spotify mode
            if (useSpotify) {
                playButtonImage.source = playing ?
                    "./assets/pause_button.svg" : "./assets/play_button.svg"
            }
        }

        function onDurationChanged(duration) {
            if (useSpotify) {
                mediaRoom.duration = duration
            }
        }

        function onPositionChanged(position) {
            if (useSpotify && !userSeeking) {
                mediaRoom.position = position
                progressSlider.value = position
            }
        }

        function onCurrentTrackChanged(title, artist, album, artUrl) {
            // Always update the cached Spotify track info
            mediaRoom.spotifyTrackName = title
            mediaRoom.spotifyArtist = artist
            mediaRoom.spotifyAlbum = album
            mediaRoom.spotifyAlbumArt = artUrl

            // Only update UI elements if we're in Spotify mode
            if (useSpotify) {
                // Reset position for new track
                mediaRoom.position = 0
                progressSlider.value = 0
                sideCardRefreshTimer.restart()

                // Update duration
                if (spotifyManager) {
                    mediaRoom.duration = spotifyManager.get_duration()
                }

                if (mediaRoom._userInitiatedChange) {
                    // Animation already started by click handler — carousel handles cleanup.
                    mediaRoom._userInitiatedChange = false
                } else {
                    // External Spotify change (phone, another device) — animate
                    mediaRoom.animateTrackChange()
                }
            }
        }

        function onVolumeChanged(volume) {
            if (useSpotify && !volumeSlider.pressed) {
                volumeControl.currentValue = volume
                volumeSlider.value = volume
                topVolumeControl.updateVolumeIcon()
            }
        }
    }

    // Spotify connection state (always active to track availability)
    Connections {
        target: spotifyManager

        function onConnectionStateChanged(connected) {
            // Update spotifyAvailable when connection state changes
            mediaRoom.spotifyAvailable = connected

            // If Spotify disconnects while in Spotify mode, switch to local
            if (!connected && settingsManager && settingsManager.mediaSource === "spotify") {
                settingsManager.set_media_source("local")
            }
        }

        function onShuffleStateChanged(enabled) {
            if (useSpotify) {
                isShuffleEnabled = enabled
                // Shuffle reorders the playlist — neighbors changed
                sideCardRefreshTimer.restart()
            }
        }

        function onQueueUpdated() {
            // Queue-based neighbor art is ready — refresh side cards
            if (useSpotify) {
                sideCardRefreshTimer.restart()
            }
        }
    }

    // Settings manager connection for media source changes
    Connections {
        target: settingsManager
        function onMediaSourceChanged(source) {
            var nowUseSpotify = (source === "spotify" && spotifyManager && spotifyManager.is_connected())

            // Update play button to show paused state (since we paused before switching)
            playButtonImage.source = "./assets/play_button.svg"

            // Update duration, position, and shuffle from the new source
            if (nowUseSpotify) {
                mediaRoom.duration = spotifyManager.get_duration()
                mediaRoom.position = spotifyManager.get_position()
                progressSlider.value = mediaRoom.position
                isShuffleEnabled = spotifyManager.is_shuffled()

                // Initialize Spotify track info when switching to Spotify
                mediaRoom.spotifyTrackName = spotifyManager.get_current_track_name()
                mediaRoom.spotifyArtist = spotifyManager.get_current_artist()
                mediaRoom.spotifyAlbum = spotifyManager.get_current_album()
                mediaRoom.spotifyAlbumArt = spotifyManager.get_current_album_art()
            } else if (mediaManager) {
                mediaRoom.duration = mediaManager.get_duration()
                mediaRoom.position = mediaManager.get_position()
                progressSlider.value = mediaRoom.position
                isShuffleEnabled = mediaManager.is_shuffled()

                // Also update the local song text if we have a current file
                var currentFile = mediaManager.get_current_file()
                if (currentFile) {
                    currentSongText.text = currentFile
                }
            }

            // Refresh side cards for the new source's neighbors
            sideCardRefreshTimer.restart()

            // Volume stays unified from Octave settings - no need to change it when switching sources
            // The unified volume is already applied to both sources
            topVolumeControl.updateVolumeIcon()
        }
    }

    // Album Art Popup — Save Theme
    Popup {
        id: albumArtPopup
        anchors.centerIn: Overlay.overlay
        width: dp(340)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property bool themeSaved: false

        onOpened: {
            themeSaved = false
            // Always extract fresh colors for the current song so the theme
            // matches what's playing now, not a stale previous extraction
            if (mediaManager) {
                var currentFile = mediaManager.get_current_file()
                if (currentFile) {
                    mediaManager.extract_colors_from_album_art(currentFile)
                }
            }
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            radius: dpMin(8, 2)
            border.color: App.Style.accent
            border.width: 1

            layer.enabled: true
            layer.effect: DropShadow {
                horizontalOffset: 0
                verticalOffset: 4
                radius: 30.0
                samples: 31
                color: Qt.rgba(0, 0, 0, 0.5)
            }
        }

        contentItem: ColumnLayout {
            spacing: dp(16)

            // Header — album name
            Text {
                Layout.fillWidth: true
                text: mediaRoom.currentAlbum
                color: App.Style.primaryTextColor
                font.pixelSize: dp(16)
                font.bold: true
                font.family: mediaRoom.globalFont
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            // Thin divider
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(App.Style.primaryTextColor.r,
                               App.Style.primaryTextColor.g,
                               App.Style.primaryTextColor.b, 0.15)
            }

            // Save as Theme button
            Rectangle {
                Layout.fillWidth: true
                height: App.Spacing.formElementHeight
                radius: dpMin(6, 2)
                color: saveThemeMA.containsMouse ? App.Style.accent : App.Style.hoverColor
                opacity: App.Style.albumArtTheme ? 1.0 : 0.4

                Text {
                    anchors.centerIn: parent
                    text: albumArtPopup.themeSaved ? "Theme Saved" : "Save as Theme"
                    color: saveThemeMA.containsMouse ? "white" : App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: mediaRoom.globalFont
                }

                MouseArea {
                    id: saveThemeMA
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: App.Style.albumArtTheme !== null && !albumArtPopup.themeSaved
                    onClicked: {
                        var themeName = mediaRoom.currentAlbum
                        if (!themeName || themeName === "Unknown Album") {
                            themeName = mediaRoom.currentTrackName || "Custom Theme"
                        }
                        var themeJson = JSON.stringify(App.Style.albumArtTheme)
                        settingsManager.save_custom_theme(themeName, themeJson, mediaRoom.currentAlbumArt)
                        albumArtPopup.themeSaved = true
                    }
                }
            }
        }
    }
}