import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Controls.Basic 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import QtQuick.Dialogs
import "." as App
import "settings" as Settings

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

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Global colors
    property color backgroundColor: "black"
    property color transparentColor: "transparent"

    property real buttonPressedOpacity: 0.7
    property real buttonNormalOpacity: 1.0

    // What is playing, whichever source has the media controls (the phone
    // while mirroring, else local media): nowPlaying (C++) follows it, routes
    // the controls and owns the phone's position clock
    readonly property var _np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    // The phone (mirroring) or Android Auto has the music: no local playlist,
    // its shuffle belongs to its app
    readonly property bool useRemote: !!_np && _np.source !== "local"
    readonly property string remoteName: !!_np && _np.source === "androidauto" ? "Android Auto" : "Phone"
    readonly property bool devicePlaying: !!_np && _np.playing
    readonly property string playIcon: devicePlaying ? "./assets/pause_button.svg" : "./assets/play_button.svg"
    readonly property int duration: _np ? _np.durationMs : 0
    property int position: 0
    property bool userSeeking: false
    // Follow the source's position, except while the user drags, and not
    // while hidden: this page is alive off-screen from startup, and 10 Hz
    // ticks re-laid out the time text, slider and carousel for nothing on
    // screen (~9% of a Pi core). Back on screen it catches up at once.
    Binding {
        target: mediaRoom
        property: "position"
        value: mediaRoom._np ? mediaRoom._np.positionMs : 0
        when: mediaRoom.visible && !mediaRoom.userSeeking
        restoreMode: Binding.RestoreNone
    }

    // Track-change animation for a change made on the phone itself (local
    // changes animate in onCurrentMediaChanged, with the carousel's
    // bookkeeping). A source switch re-reads everything without animating.
    property bool _skipTrackAnimation: false
    Connections {
        target: mediaRoom._np
        function onSourceChanged() {
            mediaRoom._skipTrackAnimation = true
            mainContent.syncNowPlaying()
        }
        function onTrackChanged() {
            if (mediaRoom._skipTrackAnimation) {
                mediaRoom._skipTrackAnimation = false
                return
            }
            if (!mediaRoom.useRemote)
                return
            if (mediaRoom._userInitiatedChange)
                mediaRoom._userInitiatedChange = false   // the click handler animated already
            else
                mediaRoom.animateTrackChange()
        }
    }
    readonly property bool showMusicDevice: App.MusicDevicePreference.enabled && App.MusicDevicePreference.isAvailable()

    // Local or Spotify. Another phone app's shuffle belongs to that app: off
    // (and dimmed) meanwhile
    readonly property bool isShuffleEnabled: !!_np && _np.canShuffle && _np.shuffled

    // The visualizer is on screen (its setting, and something to play)
    readonly property bool _waveformShown: !!settingsManager && settingsManager.showWaveformVisualizer && !playlistEmpty

    // Empty playlist detection — swaps the controls for the empty message
    property int localSongCount: 0
    property bool playlistEmpty: !useRemote && localSongCount === 0

    // Track info from nowPlaying (the phone while it has the controls, else
    // local media); this page only picks the wording
    property string currentTrackName: !_np ? "" : (_np.hasTrack ? _np.title : (useRemote ? remoteName : ""))
    property string currentArtist: !_np ? "" : (_np.hasTrack ? _np.artist : (useRemote ? "Nothing playing" : "Unknown Artist"))
    property string currentAlbum: !_np ? "" : (_np.hasTrack ? _np.album : (useRemote ? "" : "Unknown Album"))
    property string currentAlbumArt: (_np && _np.artUrl) ? _np.artUrl : "./assets/missing_art.png"

    // Card stack -- peek at adjacent tracks
    property int _slideDirection: 1      // 1 = next (slide from right), -1 = prev (slide from left)
    property bool _userInitiatedChange: false // true when click handler already started animation

    // Resolved album art source — tracks currentAlbumArt but falls back to
    // missing_art.png if the image fails to load (e.g. corrupted extract, stale cache)
    property string _displayAlbumArt: currentAlbumArt
    onCurrentAlbumArtChanged: _displayAlbumArt = currentAlbumArt

    // Side-card art refresh — fires on the next event-loop tick after any
    // track or shuffle change so the backend slot calls never compete with
    // animation frames for frame budget.  Uses a single batched backend
    // call (getNeighborAlbumArts) to halve QML-to-C++ calls.
    // Also feeds hidden preloader Images so the QML cache is warm.
    Timer {
        id: sideCardRefreshTimer
        interval: 0
        onTriggered: {
            var prevUrl = "", nextUrl = ""
            if (mediaRoom.useRemote) {
                // The phone reports only what is playing now: no side cards
            } else if (mediaManager) {
                var arts = mediaManager.getNeighborAlbumArts()
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

    function formatTime(ms) { return units.formatDuration(ms) }

    // True while card animation is running + a brief settle buffer after,
    // so the blur re-render and visualizer bar Behaviors don't all resume
    // on the exact frame the bounce lands (avoids a one-frame stutter).
    // Driven by AlbumArtCarousel.animBusy via onAnimBusyChanged binding.
    property bool _cardAnimBusy: false

    function animateTrackChange() {
        // Snap bars to zero before the card animation begins so the waveform
        // doesn't hang at stale levels. _cardAnimBusy disables bar Behaviors
        // so the drop is instant. Visualizer stays visible — bars just sit
        // at zero until the new analysis lands (avoids a multi-second hide
        // on Android where MediaCodec decode is slower than desktop ffmpeg).
        vizFadeInTimer.stop()
        _cardAnimBusy = true
        waveformContainer.dropBars()

        // Always trigger the carousel (hidden under a 3D player it still
        // times the change).  Heavy work deferred via settled signal.
        albumArtStack.triggerSlide()
        if (musicDeviceLoader.item) musicDeviceLoader.item.loadMedia(false)
    }

    // ── The look editor (NowPlayingStudio.qml) ──────────────────────────
    // The customize button at the top right shrinks this page into a mirror
    // at the top right and shows the editor's panels in the room that frees;
    // a tap on the mirror brings the page back. The mirror is the page
    // itself (mainContent, scaled), so every change is seen on the real thing.
    property bool studioOpen: false
    property real studioProgress: studioOpen ? 1.0 : 0.0
    Behavior on studioProgress {
        // Leaving the page shuts the editor at once (see onDeactivated)
        enabled: mediaRoom.StackView.status === StackView.Active
        NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
    }
    StackView.onDeactivated: studioOpen = false

    // The album art's corners, here and on the card in the art picker's ring
    readonly property int albumArtRadius: dp(16)

    readonly property int studioMargin: dp(10)
    readonly property int studioGap: dp(10)
    // Taller than wide: the output strip goes beside the mirror, the panel below
    readonly property bool studioStacked: height > width
    // The mirror keeps the page's shape. Wide: half the height, as long as
    // the main panel beside it keeps 300 dp. Tall: a third of the height,
    // at most half the width.
    readonly property real studioMirrorWidth: {
        if (width <= 0 || height <= 0) return 1
        var aspect = width / height
        if (studioStacked)
            return Math.max(1, Math.min(height * 0.36 * aspect, width * 0.5 - studioMargin))
        var natural = Math.max(dp(180), height * 0.5) * aspect
        var room = width - 2 * studioMargin - studioGap - dp(300)
        return Math.max(dp(120), Math.min(natural, room))
    }
    readonly property real studioMirrorScale: width > 0 ? studioMirrorWidth / width : 1

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor
        visible: mediaRoom.studioProgress > 0.001
    }

    Rectangle {
        id: mainContent        
        anchors.fill: parent
        color: transparentColor
        // The mirror: the whole page scaled down towards the top right
        readonly property real mirrorScale: 1.0 + (mediaRoom.studioMirrorScale - 1.0) * mediaRoom.studioProgress
        clip: mediaRoom.studioProgress > 0.001
        transform: [
            Scale { xScale: mainContent.mirrorScale; yScale: mainContent.mirrorScale },
            Translate {
                x: (mediaRoom.width - mediaRoom.studioMargin - mediaRoom.studioMirrorWidth) * mediaRoom.studioProgress
                y: mediaRoom.studioMargin * mediaRoom.studioProgress
            }
        ]

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
                // layer.live is set through a Binding, not declared as `layer.live:`.
                // It is a revisioned property (Qt 6.5), and on Qt 6.7 whether the QML
                // compiler accepts the declaration depends on which file first compiled
                // the layer type: sometimes it fails with '".live" is not available due
                // to component versioning', which made MediaRoom unloadable (seen when
                // pages are pre-built at startup). A Binding sets it by name at runtime.
                Binding { target: albumArtGrid.layer; property: "live"; value: !mediaRoom._cardAnimBusy }
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

        // The carousel's local bookkeeping; what is playing comes from nowPlaying
        function syncNowPlaying() {
            if (!mediaRoom.useRemote && mediaManager) {
                var currentFile = mediaManager.getCurrentFile()
                if (currentFile) {
                    currentSongText.text = currentFile
                }
            }
            // Force initial evaluation of side card art
            sideCardRefreshTimer.restart()

            // Initialize local song count from the active playlist (not raw
            // directory scan, which misses "All Music" and other combined lists)
            if (mediaManager) {
                localSongCount = mediaManager.getCurrentSongList().length
            }
        }

        Rectangle { // Volume control at top
            id: topVolumeControl
            // Portrait: the customize button takes the right end of the row
            readonly property int portraitButtonRoom: App.Spacing.isPortrait ? customizeButton.width + dp(8) : 0
            width: parent.width * (App.Spacing.isPortrait ? 0.9 : 0.75) - portraitButtonRoom
            height: App.Spacing.mediaRoomDurationBarHeight
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                horizontalCenterOffset: -topVolumeControl.portraitButtonRoom / 2
                margins: App.Spacing.mediaRoomMargin
            }
            color: transparentColor
            
            RowLayout {
                anchors.fill: parent
                spacing: dp(10)

                // Volume icon control
                Control {
                    id: volumeIconControl
                    // 1.4 of the bottom bar's mute button: it reads small up here
                    implicitWidth: Math.round(App.Spacing.bottomBarMuteButtonWidth * 1.4)
                    implicitHeight: Math.round(App.Spacing.bottomBarMuteButtonHeight * 1.4)
                    Layout.alignment: Qt.AlignVCenter
                    // Portrait: as wide as the % text, so the slider is centred
                    Layout.preferredWidth: App.Spacing.isPortrait
                        ? Math.max(implicitWidth, volumePercentText.implicitWidth) : -1
                    
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
                        // At 0 % this restores the startup volume instead
                        onClicked: volumeController.toggleMute()
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
                            // Ends at the handle's center, which travels the width
                            // less one handle, so the fill's rounded end stays
                            // tucked under the handle (no sliver of pill at 0%)
                            width: volumeSlider.visualPosition * (parent.width - App.Spacing.mediaRoomSliderButtonWidth)
                                   + App.Spacing.mediaRoomSliderButtonWidth / 2
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

                        // A press on the slider itself is the slider's (it sets its
                        // own `pressed`). One in the margin above or below never
                        // reaches it, so this area drags for it. Setting `pressed`
                        // and then passing every press on, as this used to, left it
                        // stuck true after a margin press: no release ever came.
                        property bool own: false
                        function valueAt(x) {
                            var pos = Math.max(0, Math.min(1, (x - volumeSlider.leftPadding) / volumeSlider.availableWidth))
                            return volumeSlider.from + pos * (volumeSlider.to - volumeSlider.from)
                        }
                        onPressed: function(mouse) {
                            own = mouse.y < dp(10) || mouse.y > height - dp(10)
                            if (!own) {
                                mouse.accepted = false
                                return
                            }
                            volumeSlider.pressed = true
                            volumeSlider.value = valueAt(mouse.x)
                        }
                        onPositionChanged: function(mouse) {
                            if (own && pressed)
                                volumeSlider.value = valueAt(mouse.x)
                        }
                        onReleased: { if (own) volumeSlider.pressed = false; own = false }
                        onCanceled: { if (own) volumeSlider.pressed = false; own = false }
                    }
                    
                    // Volume change logic
                    onValueChanged: {
                        // Single dispatch to settings + every audio output;
                        // raising it while muted unmutes (VolumeController)
                        // null at shutdown: the binding to percent drops to 0 as the managers go
                        if (volumeController)
                            volumeController.applyVolume(Math.round(value))

                        // Update icon
                        topVolumeControl.updateVolumeIcon()
                    }
                }
                
                // Volume percentage text
                Text {
                    id: volumePercentText
                    text: Math.round(volumeSlider.value) + "%"
                    color: App.Style.mediaRoomSeekColor
                    // The seek bar's times match it (positionText / durationText)
                    font.pixelSize: Math.round(App.Spacing.mediaRoomSliderDurationText * 1.35)
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: App.Spacing.isPortrait ? 0 : dp(40)
                    Layout.preferredWidth: App.Spacing.isPortrait ? volumeIconControl.Layout.preferredWidth : -1
                    horizontalAlignment: App.Spacing.isPortrait ? Text.AlignRight : Text.AlignLeft
                }
            }

            // Volume control state properties
            QtObject {
                id: volumeControl
                // null at shutdown, when the managers go before the engine
                readonly property int currentValue: volumeController ? volumeController.percent : 0

                Component.onCompleted: topVolumeControl.updateVolumeIcon()
            }
            
            // Functions
            function getVolumeIconSource() {
                if (!volumeController || volumeController.effectivelyMuted) {   // null at shutdown
                    return "./assets/mute_on.svg"
                }
                // The same steps as the nav bar's volume button (BottomBar.qml)
                if (volumeControl.currentValue < 33) return "./assets/mute_off_low.svg"
                if (volumeControl.currentValue < 66) return "./assets/mute_off_med.svg"
                return "./assets/mute_off_high.svg"
            }
            
            function updateVolumeIcon() {
                volumeIconImage.source = getVolumeIconSource()
            }
            
            // Connections for volume sync
            Connections {
                target: volumeController
                function onPercentChanged(percent) {
                    if (!volumeSlider.pressed)
                        volumeSlider.value = percent
                    topVolumeControl.updateVolumeIcon()
                }
                function onEffectivelyMutedChanged() { topVolumeControl.updateVolumeIcon() }
            }
        }

        Rectangle { //media controls container
            id: mediaControlsContainer
            visible: !playlistEmpty
            // Portrait: the full width, from a little under the volume row
            // down to the visualizer (or the seek bar when it is off)
            width: App.Spacing.isPortrait ? parent.width
                 : App.Spacing.applicationWidth * App.Spacing.mediaRoomControlsContainerWidth
            height: App.Spacing.applicationHeight * App.Spacing.mediaRoomControlsContainerHeight
            anchors {
                top: topVolumeControl.bottom
                bottom: App.Spacing.isPortrait && mediaRoom._waveformShown ? waveformContainer.top : durationBar.top
                horizontalCenter: parent.horizontalCenter
                // Use verticalCenter to ensure it's centered between the two elements
                margins: App.Spacing.mediaRoomMargin
                topMargin: App.Spacing.mediaRoomMargin + (App.Spacing.isPortrait ? dp(8) : 0)
            }
            color: transparentColor
        

            // Controls and metadata on the left, art on the right; in
            // portrait the art on top and the controls under it
            GridLayout {
                id: mediaControlsGrid
                readonly property bool portrait: App.Spacing.isPortrait
                anchors.fill: parent
                columns: portrait ? 1 : 2
                rowSpacing: App.Spacing.mediaRoomSpacing
                columnSpacing: App.Spacing.mediaRoomSpacing

                // Left side - Controls and Metadata
                ColumnLayout {
                    id: mediaControlsColumn
                    Layout.row: mediaControlsGrid.portrait ? 1 : 0
                    Layout.column: 0
                    // Portrait: the height the art leaves (see the art item)
                    Layout.fillHeight: true
                    Layout.fillWidth: mediaControlsGrid.portrait
                    Layout.preferredWidth: mediaControlsGrid.portrait ? -1 : parent.width * (mediaRoom.showMusicDevice ? 0.56 : 0.6)
                    Layout.maximumWidth: mediaControlsGrid.portrait ? Number.POSITIVE_INFINITY : parent.width * (mediaRoom.showMusicDevice ? 0.56 : 0.6)
                    Layout.leftMargin: dp(20)
                    Layout.rightMargin: mediaControlsGrid.portrait ? dp(20) : 0
                    spacing: mediaControlsGrid.portrait ? dp(10) : App.Spacing.mediaRoomSpacing * 2

                    // Spacer to push controls toward center
                    Item { Layout.fillHeight: true; visible: !mediaControlsGrid.portrait }

                    // Portrait: the title block (reparented here) above the
                    // transport row, both under the art
                    Item {
                        id: portraitMetaSlot
                        visible: mediaControlsGrid.portrait
                        Layout.fillWidth: true
                        Layout.preferredHeight: songMeta.implicitHeight
                    }

                    // Portrait: the keys sit midway between the title and the
                    // visualizer (this spacer and the one at the bottom)
                    Item { Layout.fillHeight: true; visible: mediaControlsGrid.portrait }

                    // Media Controls Row
                    RowLayout {
                        id: transportRow
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        // Portrait: spread the keys across more of the page width.
                        // From the container's width (the column is that less its
                        // two margins), not the column's own: a spacing that follows
                        // the width the layout is just handing out makes it start
                        // over ("Detected recursive rearrange")
                        // Landscape: the three keys grow to fill the column
                        // (previous, play at 1.25 of a key, next, and a gap of
                        // 0.6 of a key each side of play: 4.45 keys across),
                        // held by the height so the title keeps its room and
                        // never under the Spacing sizes. Portrait keeps those.
                        readonly property real columnRoom:
                            mediaControlsContainer.width * (mediaRoom.showMusicDevice ? 0.56 : 0.6) - dp(28)
                        readonly property int keySize: mediaControlsGrid.portrait
                            ? App.Spacing.mediaRoomKeySize
                            : Math.max(App.Spacing.mediaRoomKeySize,
                                       Math.round(Math.min(columnRoom / 4.45,
                                                           mediaControlsContainer.height * 0.45 / 1.25)))
                        readonly property int playKeySize: mediaControlsGrid.portrait
                            ? App.Spacing.mediaRoomPlayKeySize
                            : Math.max(App.Spacing.mediaRoomPlayKeySize, Math.round(keySize * 1.25))
                        spacing: mediaControlsGrid.portrait
                            ? Math.max(App.Spacing.mediaRoomBetweenButton, (mediaControlsContainer.width - dp(40)) * 0.07)
                            : Math.max(App.Spacing.mediaRoomBetweenButton,
                                       Math.min(keySize * 0.6, (columnRoom - 2 * keySize - playKeySize) / 2))
                        // Touch targets reach past the keys: sideways to the
                        // middle of each gap, so the three meet with nothing dead
                        // between them, and up and down into the empty space
                        // (less in portrait, where the title sits close above)
                        readonly property real hitSide: spacing / 2
                        readonly property real hitVertical: mediaControlsGrid.portrait
                            ? App.Spacing.mediaRoomBetweenButton / 2
                            : App.Spacing.mediaRoomSpacing

                        // Portrait: shuffle joins this row (it is reparented
                        // here), balanced by an empty slot after Next
                        Item {
                            id: portraitShuffleSlot
                            visible: mediaControlsGrid.portrait
                            Layout.preferredWidth: shuffleButton.keyBox
                            Layout.preferredHeight: Layout.preferredWidth
                        }

                        Control { //Previous button
                            id: previousControl
                            implicitHeight: transportRow.keySize
                            implicitWidth: transportRow.keySize

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
                                    // A solid glyph that fills its box: drawn at 0.6 of
                                    // the key so it doesn't outweigh the play circle
                                    width: parent.width * 0.6
                                    height: parent.height * 0.6
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
                                anchors.leftMargin: -transportRow.hitSide
                                anchors.rightMargin: -transportRow.hitSide
                                anchors.topMargin: -transportRow.hitVertical
                                anchors.bottomMargin: -transportRow.hitVertical
                                onClicked: {
                                    mediaRoom._slideDirection = -1
                                    // Fire animation BEFORE the backend call — enter card
                                    // starts at opacity 0 so stale art is invisible.
                                    // Backend data arrives while card is still transparent.
                                    mediaRoom._userInitiatedChange = true
                                    mediaRoom.animateTrackChange()
                                    if (mediaRoom._np) mediaRoom._np.previous()
                                }
                            }
                        }

                        Control { //Play Button
                            id: playControl
                            implicitHeight: transportRow.playKeySize
                            implicitWidth: transportRow.playKeySize

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
                                    source: mediaRoom.playIcon
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
                                anchors.leftMargin: -transportRow.hitSide
                                anchors.rightMargin: -transportRow.hitSide
                                anchors.topMargin: -transportRow.hitVertical
                                anchors.bottomMargin: -transportRow.hitVertical
                                onClicked: {
                                    if (mediaRoom._np) mediaRoom._np.togglePlay()
                                }
                            }
                        }
                        
                        Control { //Next Button
                            id: nextControl
                            implicitHeight: transportRow.keySize
                            implicitWidth: transportRow.keySize

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
                                    // A solid glyph that fills its box: drawn at 0.6 of
                                    // the key so it doesn't outweigh the play circle
                                    width: parent.width * 0.6
                                    height: parent.height * 0.6
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
                                anchors.leftMargin: -transportRow.hitSide
                                anchors.rightMargin: -transportRow.hitSide
                                anchors.topMargin: -transportRow.hitVertical
                                anchors.bottomMargin: -transportRow.hitVertical
                                onClicked: {
                                    mediaRoom._slideDirection = 1
                                    mediaRoom._userInitiatedChange = true
                                    mediaRoom.animateTrackChange()
                                    if (mediaRoom._np) mediaRoom._np.next()
                                }
                            }
                        }

                        Item {
                            visible: mediaControlsGrid.portrait
                            Layout.preferredWidth: portraitShuffleSlot.Layout.preferredWidth
                            Layout.preferredHeight: portraitShuffleSlot.Layout.preferredHeight
                        }
                    }

                    // Song metadata section (below controls; portrait: above
                    // the seek bar, in portraitMetaSlot)
                    Item {
                        id: landscapeMetaSlot
                        visible: !mediaControlsGrid.portrait
                        Layout.fillWidth: true
                        Layout.preferredHeight: songMeta.implicitHeight

                        ColumnLayout {
                            id: songMeta
                            parent: mediaControlsGrid.portrait ? portraitMetaSlot : landscapeMetaSlot
                            anchors.left: parent.left
                            anchors.right: parent.right
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

                                    // Title marquee. Runs only while mediaRoom is visible: this page stays alive

                                    // hidden (StackView cache / pre-building), and a hidden marquee kept a 5 s

                                    // animation cycling forever, costing ~15% of a Pi core at idle. restart()

                                    // below bypasses the running binding, hence the visible checks there too.

                                    Timer {
                                        id: songScrollTimer
                                        property real containerWidth: songTitleFlickable.parent ? songTitleFlickable.parent.width : 200
                                        interval: 3000
                                        running: songTitleText.width > containerWidth && mediaRoom.visible
                                        repeat: true
                                        onTriggered: {
                                            if (!mediaRoom.visible) return
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
                                        onFinished: if (mediaRoom.visible) songScrollTimer.restart()
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
                                        running: metadataRow.width > containerWidth && mediaRoom.visible
                                        repeat: true
                                        onTriggered: {
                                            if (!mediaRoom.visible) return
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
                                        onFinished: if (mediaRoom.visible) metadataScrollTimer.restart()
                                    }
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
                    Layout.row: 0
                    Layout.column: mediaControlsGrid.portrait ? 0 : 1
                    Layout.fillWidth: mediaControlsGrid.portrait
                    Layout.preferredWidth: mediaControlsGrid.portrait ? -1 : parent.width * (mediaRoom.showMusicDevice ? 0.44 : 0.4)
                    Layout.maximumWidth: mediaControlsGrid.portrait ? Number.POSITIVE_INFINITY : parent.width * (mediaRoom.showMusicDevice ? 0.44 : 0.4)
                    // Portrait: a square box as wide as the page, cut down to
                    // what the title and keys under it leave, so it shrinks
                    // rather than pushing them into the bottom bar
                    Layout.fillHeight: !mediaControlsGrid.portrait
                    Layout.preferredHeight: mediaControlsGrid.portrait
                        ? Math.max(0, Math.min(parent.width,
                              parent.height - mediaControlsColumn.implicitHeight - parent.rowSpacing))
                        : -1
                    Layout.minimumHeight: 0
                    Layout.alignment: Qt.AlignVCenter

                    // Stays loaded while MediaRoom is hidden: rebuilding the
                    // Quick3D scene (models, materials, pipelines) on every
                    // visit cost ~300 ms per nav press. The scene pauses its own
                    // motion timer and re-runs its arrival animation off
                    // root.visible, so a hidden instance does no work.
                    Loader {
                        id: musicDeviceLoader
                        anchors.fill: parent
                        active: mediaRoom.showMusicDevice
                        source: active ? "MusicDeviceScene.qml" : ""
                    }
                    // A tap on the 3D device opens the same menu as a tap on the art
                    Connections {
                        target: musicDeviceLoader.item
                        ignoreUnknownSignals: true
                        function onClicked() { albumArtPopup.openPicker() }
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
                        // Portrait: a square box at the top, right under the volume row
                        anchors.fill: mediaControlsGrid.portrait ? undefined : parent
                        anchors.top: mediaControlsGrid.portrait ? parent.top : undefined
                        anchors.horizontalCenter: mediaControlsGrid.portrait ? parent.horizontalCenter : undefined
                        width: parent.width
                        height: mediaControlsGrid.portrait ? Math.min(parent.width, parent.height) : parent.height
                        // Keep the carousel's transition/settled lifecycle alive for track analysis.
                        // Only its presentation is hidden while a device is successfully loaded.
                        visible: !(musicDeviceLoader.item && musicDeviceLoader.item.ready)

                        // Settings bindings
                        // Portrait: nearly the page width, no side cards (no room for them)
                        artFill: mediaControlsGrid.portrait ? 0.92 : 0.85
                        previewEnabled: !mediaControlsGrid.portrait && settingsManager && settingsManager.show3DAlbumPreview
                        roundedArt: true   // always, 16 dp (the ring's card has the same)
                        artRadius: mediaRoom.albumArtRadius
                        vinylMode: settingsManager && settingsManager.vinylRecordMode
                        sideCardAngle: settingsManager ? settingsManager.sideCardAngle : 30
                        sideCardOpacity: settingsManager ? settingsManager.sideCardOpacity : 0.4
                        showShadow: true

                        // Media state
                        artSource: mediaRoom._displayAlbumArt
                        direction: mediaRoom._slideDirection
                        playbackPosition: mediaRoom.position
                        playbackDuration: mediaRoom.duration

                        // Sync busy flag back to MediaRoom (drives blur freeze, viz pause, etc.)
                        onAnimBusyChanged: mediaRoom._cardAnimBusy = animBusy

                        onAnimationFinished: sideCardRefreshTimer.restart()

                        onArtClicked: albumArtPopup.openPicker()

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

        // No music: the prompt Home's music card shows, so an empty library
        // is one tap from music (the manager rescans when the folder changes)
        App.MusicEmptyPrompt {
            visible: playlistEmpty
            anchors.centerIn: parent
            width: Math.min(parent.width - dp(32), dp(560))
            stackView: mediaRoom.stackView
            mainWindow: mediaRoom.mainWindow
            fontFamily: mediaRoom.globalFont
        }

        // Waveform Visualizer
        Item {
            id: waveformContainer
            objectName: "waveformContainer"
            width: durationBar.width
            height: dp(40)
            anchors {
                bottom: durationBar.top
                horizontalCenter: parent.horizontalCenter
                bottomMargin: dp(10)
            }
            visible: mediaRoom._waveformShown
            // Kept for back-compat with animateTrackChange() / onAnalysisComplete
            // handler below. No longer hides the visualizer — bars just drop
            // to zero on track change (via dropBars()) and animate back up
            // when the new analysis lands. Previously we faded the whole
            // visualizer out for up to a few seconds on Android (MediaCodec
            // decode is slower than desktop ffmpeg), which looked broken.
            property bool _vizFadedOut: false
            opacity: visible ? 1.0 : 0.0

            // The analyzer follows what is playing (phone live, a local track
            // pre-analysed) and runs only while bars are on screen; held
            // during the card animation so a new track's decode starts after
            // it settles instead of competing with its frames
            readonly property bool _barsOn: visible && !mediaRoom._cardAnimBusy
            on_BarsOnChanged: if (audioAnalyzer) audioAnalyzer.setViewActive(_barsOn)
            Component.onCompleted: if (audioAnalyzer) audioAnalyzer.setViewActive(_barsOn)
            Component.onDestruction: if (audioAnalyzer) audioAnalyzer.setViewActive(false)

            // 96 bars at 60 FPS with pill-shaped fancy bars
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
                // Via Binding, see albumArtGrid above.
                Binding { target: waveformBars.layer; property: "live"; value: !mediaRoom._cardAnimBusy }

                property int numBars: audioAnalyzer ? audioAnalyzer.numBars : 96

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

            // Immediately zero all bars (snaps because Behaviors are disabled when _cardAnimBusy)
            function dropBars() {
                for (var i = 0; i < waveformRepeater.count; i++) {
                    var item = waveformRepeater.itemAt(i)
                    if (item) item.barLevel = 0
                }
            }

        }

        Rectangle { //duration bar
            id: durationBar
            visible: !playlistEmpty
            width: parent.width * (App.Spacing.isPortrait ? 0.9 : 0.75)
            // Portrait: the slider across the page, the times under it
            height: App.Spacing.isPortrait ? seekGrid.implicitHeight : App.Spacing.mediaRoomDurationBarHeight
            anchors {
                bottom: parent.bottom
                horizontalCenter: parent.horizontalCenter
                margins: dp(20)
            }
            color: transparentColor

            GridLayout {
                id: seekGrid
                readonly property bool portrait: App.Spacing.isPortrait
                anchors.fill: parent
                columns: 3
                columnSpacing: portrait ? dp(8) : dp(20)
                rowSpacing: 0

                Text {
                    id: positionText
                    Layout.row: seekGrid.portrait ? 1 : 0
                    Layout.column: 0
                    text: formatTime(mediaRoom.position)
                    color: App.Style.mediaRoomSeekColor
                    // The size of the volume percentage at the top
                    font.pixelSize: volumePercentText.font.pixelSize
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: dp(40)  // Added minimum width for consistent layout
                }

                Slider {
                    id: progressSlider
                    Layout.row: 0
                    Layout.column: seekGrid.portrait ? 0 : 1
                    Layout.columnSpan: seekGrid.portrait ? 3 : 1
                    Layout.fillWidth: true
                    from: 0
                    to: mediaRoom.duration > 0 ? mediaRoom.duration : 1
                    enabled: mediaRoom.duration > 0 && !!mediaRoom._np && mediaRoom._np.canSeek
                    // Follows the position, except while the user drags (a
                    // plain value binding would break on the first touch)
                    Binding on value {
                        when: !mediaRoom.userSeeking
                        value: mediaRoom.position
                        restoreMode: Binding.RestoreNone
                    }
                    
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
                            // Ends at the handle's center, which travels the width
                            // less one handle, so the fill's rounded end stays
                            // tucked under the handle (no sliver of pill at 0%)
                            width: progressSlider.visualPosition * (parent.width - App.Spacing.mediaRoomSliderButtonWidth)
                                   + App.Spacing.mediaRoomSliderButtonWidth / 2
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

                        // A press on the slider itself is the slider's (it sets its
                        // own `pressed`). One in the margin above or below never
                        // reaches it, so this area drags for it. Setting `pressed`
                        // and then passing every press on, as this used to, left it
                        // stuck true after a margin press: no release ever came.
                        property bool own: false
                        function valueAt(x) {
                            var pos = Math.max(0, Math.min(1, (x - progressSlider.leftPadding) / progressSlider.availableWidth))
                            return progressSlider.from + pos * (progressSlider.to - progressSlider.from)
                        }
                        onPressed: function(mouse) {
                            own = mouse.y < dp(10) || mouse.y > height - dp(10)
                            if (!own) {
                                mouse.accepted = false
                                return
                            }
                            progressSlider.pressed = true
                            progressSlider.value = valueAt(mouse.x)
                        }
                        onPositionChanged: function(mouse) {
                            if (own && pressed)
                                progressSlider.value = valueAt(mouse.x)
                        }
                        onReleased: { if (own) progressSlider.pressed = false; own = false }
                        onCanceled: { if (own) progressSlider.pressed = false; own = false }
                    }

                    onPressedChanged: {
                        if (pressed) {
                            userSeeking = true
                        } else {
                            mediaRoom.position = value
                            if (mediaRoom._np)
                                mediaRoom._np.seek(value)
                            userSeeking = false
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
                    Layout.row: seekGrid.portrait ? 1 : 0
                    Layout.column: 2
                    // Portrait: pushed to the right edge, under the slider's end
                    Layout.fillWidth: seekGrid.portrait
                    horizontalAlignment: seekGrid.portrait ? Text.AlignRight : Text.AlignLeft
                    text: formatTime(mediaRoom.duration)
                    color: App.Style.mediaRoomSeekColor
                    // The size of the volume percentage at the top
                    font.pixelSize: volumePercentText.font.pixelSize
                    font.family: mediaRoom.globalFont
                    Layout.minimumWidth: dp(40)  // Added minimum width for consistent layout
                }
            }
        }

        // Shuffle Button — bottom left corner, centered between edge and duration bar.
        // Portrait: the duration bar spans the page, so shuffle moves into the
        // transport row, left of Previous.
        Control {
            id: shuffleButton
            parent: App.Spacing.isPortrait ? portraitShuffleSlot : mainContent
            visible: !playlistEmpty
            opacity: mediaRoom._np && mediaRoom._np.canShuffle ? 1.0 : 0.35
            // Portrait: the same box as previous / next (keyBox). Landscape:
            // half as large again, as far as the strip beside the seek bar allows
            readonly property int keyBox: App.Spacing.mediaRoomKeySize
            width: App.Spacing.isPortrait ? keyBox
                 : Math.max(dp(48), Math.min(Math.round(keyBox * 1.5), durationBar.x - dp(12)))
            height: width
            y: App.Spacing.isPortrait ? 0 : durationBar.y + (durationBar.height - height) / 2 - dp(15)
            x: App.Spacing.isPortrait ? 0 : (durationBar.x - width) / 2
            background: Rectangle {
                color: isShuffleEnabled ? App.Style.mediaRoomToggleShade : "transparent"
                radius: 4
            }
            contentItem: Item {
                Image {
                    id: shuffleButtonImage
                    anchors.centerIn: parent
                    width: parent.width * 0.75
                    height: parent.height * 0.75
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
                    if (mediaRoom._np)
                        mediaRoom._np.toggleShuffle()   // does nothing without canShuffle
                }
            }
        }

        // Customize: opens the look editor around this page. Landscape: in
        // the strip right of the volume row (as shuffle sits left of the
        // seek bar). Portrait: the right end of the volume row.
        Control {
            id: customizeButton
            objectName: "customizeButton"
            width: Math.max(dp(44), Math.round(App.Spacing.bottomBarMuteButtonWidth * 1.4))
            height: width
            readonly property real rowRight: topVolumeControl.x + topVolumeControl.width
            x: App.Spacing.isPortrait ? rowRight + dp(8)
                                      : rowRight + (parent.width - rowRight - width) / 2
            y: Math.max(dp(4), topVolumeControl.y + (topVolumeControl.height - height) / 2)
            opacity: (customizeArea.pressed ? buttonPressedOpacity : buttonNormalOpacity)
                     * (1.0 - mediaRoom.studioProgress)
            visible: opacity > 0.01
            background: Rectangle {
                color: customizeArea.containsMouse ? App.Style.mediaRoomToggleShade : "transparent"
                radius: 4
            }
            contentItem: Item {
                App.ThemedIcon {
                    anchors.centerIn: parent
                    width: parent.width * 0.7
                    height: parent.height * 0.7
                    source: "./assets/customize_button.svg"
                    color: App.Style.bottomBarVolumeButton
                    shadow: false
                }
            }
            MouseArea {
                id: customizeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: mediaRoom.studioOpen = true
            }
        }

        // While the editor is open the page is a mirror: nothing on it takes
        // input, a tap anywhere on it closes the editor
        MouseArea {
            id: mirrorArea
            objectName: "studioMirror"
            anchors.fill: parent
            z: 1000
            enabled: mediaRoom.studioOpen || mediaRoom.studioProgress > 0.001
            visible: enabled
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mediaRoom.studioOpen = false
            onWheel: function(wheel) { wheel.accepted = true }

            // Accent frame, 2 px on screen whatever the mirror's scale
            Rectangle {
                anchors.fill: parent
                color: mirrorArea.pressed ? Qt.rgba(0, 0, 0, 0.25) : "transparent"
                border.width: Math.ceil((mirrorArea.containsMouse ? 3 : 2) / Math.max(0.05, mainContent.mirrorScale))
                border.color: App.Style.accent
                opacity: mediaRoom.studioProgress
            }
        }
    }

    // The editor's panels, around the mirror. Built on the first open and
    // kept; by url and asynchronous, so the page itself loads no slower.
    Loader {
        id: studioLoader
        anchors.fill: parent
        property bool wanted: false
        active: wanted
        asynchronous: true
        source: active ? "NowPlayingStudio.qml" : ""
        visible: mediaRoom.studioProgress > 0.001
        enabled: mediaRoom.studioOpen
        // The page shrinks first, the controls resolve once there is room
        opacity: Math.max(0.0, (mediaRoom.studioProgress - 0.45) / 0.55)
        Binding { target: studioLoader.item; property: "mirrorWidth"; value: mediaRoom.studioMirrorWidth; when: studioLoader.status === Loader.Ready }
        Binding { target: studioLoader.item; property: "mirrorHeight"; value: mediaRoom.height * mediaRoom.studioMirrorScale; when: studioLoader.status === Loader.Ready }
        Binding { target: studioLoader.item; property: "margin"; value: mediaRoom.studioMargin; when: studioLoader.status === Loader.Ready }
        Binding { target: studioLoader.item; property: "gap"; value: mediaRoom.studioGap; when: studioLoader.status === Loader.Ready }
        Binding { target: studioLoader.item; property: "stacked"; value: mediaRoom.studioStacked; when: studioLoader.status === Loader.Ready }
    }
    onStudioOpenChanged: if (studioOpen) studioLoader.wanted = true

    // Track local song count for empty-state detection (always active).
    // When a playlist transitions from empty → has songs (files appeared
    // after a rescan), auto-play the first track so the room comes alive.
    Connections {
        target: mediaManager
        function onMediaListChanged(files) {
            var wasEmpty = mediaRoom.localSongCount === 0
            mediaRoom.localSongCount = files.length
            if (wasEmpty && files.length > 0 && !mediaRoom.useRemote) {
                mediaManager.playFile(files[0])
            }
        }
    }

    // Local media manager connections (only apply when the phone does not have the controls)
    Connections {
        target: mediaManager
        enabled: !useRemote

        // Play state, duration and position come from nowPlaying; this keeps
        // the carousel and its track-change animation in step
        function onCurrentMediaChanged(filename) {
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
            // Shuffle reorders the playlist — neighbors changed
            sideCardRefreshTimer.restart()
        }
    }

    // Album Art Popup: a carousel to pick what this corner shows (the art or a 3D player)
    Popup {
        id: albumArtPopup
        anchors.centerIn: Overlay.overlay
        width: Math.round(Math.min(mediaRoom.width * 0.92, dp(900)))
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // Asked on open, not bound: the first call compiles the 3D scene
        property bool devicesAvailable: false

        // Nothing to pick from without the Quick3D modules: stay shut
        function openPicker() {
            devicesAvailable = App.MusicDevicePreference.isAvailable()
            if (devicesAvailable) open()
        }
        onAboutToShow: {
            if (viewCarousel.item) {
                viewCarousel.item.show(albumArtPopup.currentChoice())
                viewCarousel.item.forceActiveFocus()
            }
        }
        // The ring's choices: the Music Player Object options, with "Vinyl"
        // after "Album art". Vinyl is the album art with the vinyl record
        // setting on, so the two of them set that switch as well.
        readonly property var choices: {
            var list = App.MusicDevicePreference.options.slice()
            list.splice(list.indexOf("Album art") + 1, 0, "Vinyl")
            return list
        }
        function currentChoice() {
            var mode = App.MusicDevicePreference.mode
            return mode === "Album art" && settingsManager && settingsManager.vinylRecordMode ? "Vinyl" : mode
        }
        function use(value) {
            if (value === "") return
            var vinyl = value === "Vinyl"
            if (settingsManager && (vinyl || value === "Album art"))
                settingsManager.vinylRecordMode = vinyl
            App.MusicDevicePreference.select(vinyl ? "Album art" : value)
            close()
        }

        // No panel: the ring floats over the page, which is dimmed well
        // down so the title and keys behind don't read through it
        background: Item {}
        padding: 0
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.8) }

        contentItem: ColumnLayout {
            spacing: dp(16)

            // What the art corner shows: the album art or one of the 3D
            // players (the Music Player Object setting), picked from a
            // carousel. A Loader by url: this file must load without Quick3D.
            Loader {
                id: viewCarousel
                // Built on the first open and kept: the models load once
                active: albumArtPopup.devicesAvailable
                source: active ? "MusicDeviceCarousel.qml" : ""
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(Math.min(dp(480), mediaRoom.height * 0.62))
                readonly property string focusValue: item ? item.focusValue : ""
                onLoaded: {
                    item.options = albumArtPopup.choices
                    item.show(albumArtPopup.currentChoice())
                    item.forceActiveFocus()
                }
                Binding { target: viewCarousel.item; property: "artSource"; value: mediaRoom._displayAlbumArt; when: viewCarousel.status === Loader.Ready }
                Binding { target: viewCarousel.item; property: "artCornerRadius"; value: mediaRoom.albumArtRadius; when: viewCarousel.status === Loader.Ready }
                Binding { target: viewCarousel.item; property: "playbackPosition"; value: mediaRoom.position; when: viewCarousel.status === Loader.Ready }
                Binding { target: viewCarousel.item; property: "playbackDuration"; value: mediaRoom.duration; when: viewCarousel.status === Loader.Ready }
                Connections {
                    target: viewCarousel.item
                    function onChosen(value) { albumArtPopup.use(value) }
                    function onMissed() { albumArtPopup.close() }
                }
            }

            // The name of the one at the front; a tap on it there picks it
            Text {
                Layout.fillWidth: true
                text: viewCarousel.focusValue
                color: App.Style.primaryTextColor
                font.pixelSize: Math.round(App.Spacing.overallText * 1.25)
                font.bold: true
                font.family: mediaRoom.globalFont
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}