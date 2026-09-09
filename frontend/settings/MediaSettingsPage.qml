import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects
import ".." as App

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // Reactive property for Spotify devices
    property var spotifyDevicesList: []

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    property var tileModel: [
        { cardId: "media_library",     title: "Library",     iconSource: App.Style.assetBase + "tile_library.svg",     component: libraryContent },
        { cardId: "media_playback",    title: "Playback",    iconSource: App.Style.assetBase + "tile_playback.svg",    component: playbackContent },
        { cardId: "media_now_playing", title: "Now Playing", iconSource: App.Style.assetBase + "tile_now_playing.svg", component: nowPlayingContent },
        { cardId: "media_spotify",     title: "Spotify",     iconSource: App.Style.assetBase + "tile_spotify.svg",     component: spotifyContent }
    ]

    // Refresh Spotify devices when navigating to media settings
    onCurrentSectionChanged: {
        if (currentSection === "mediaSettings" && spotifyManager && spotifyManager.is_connected()) {
            spotifyManager.refresh_devices()
        }
    }

    Component.onCompleted: {
        if (currentSection === "mediaSettings" && spotifyManager && spotifyManager.is_connected()) {
            spotifyManager.refresh_devices()
        }
        if (spotifyManager && spotifyManager.is_connected()) {
            spotifyDevicesList = spotifyManager.get_devices()
        }
    }

    Connections {
        target: spotifyManager
        function onDevicesChanged(devices) {
            pageRoot.spotifyDevicesList = devices
        }
        function onConnectionStateChanged(connected) {
            if (connected) {
                spotifyManager.refresh_devices()
            } else {
                pageRoot.spotifyDevicesList = []
            }
        }
    }

    // Folder dialog for selecting music library folder.
    // On Android, this opens Storage Access Framework — the selectedFolder is
    // a content:// tree URI, not a filesystem path. We decode the SAF URI for
    // external primary storage back to a /storage/emulated/0/... path.
    FolderDialog {
        id: folderDialog
        title: "Select Music Library Folder"
        onAccepted: {
            var path = selectedFolder.toString()

            if (path.startsWith("content://")) {
                var treeIdx = path.indexOf("/tree/")
                if (treeIdx >= 0) {
                    var encoded = path.substring(treeIdx + 6)
                    var decoded = decodeURIComponent(encoded)
                    if (decoded.indexOf("primary:") === 0) {
                        path = "/storage/emulated/0/" + decoded.substring(8)
                    } else {
                        path = decoded
                    }
                }
            } else if (path.startsWith("file:///")) {
                var afterScheme = path.substring(8)
                if (afterScheme.length > 1 && afterScheme.charAt(1) === ':') {
                    path = afterScheme
                } else {
                    path = path.substring(7)
                }
            }

            if (settingsManager) {
                settingsManager.save_media_folder(path)
            }
        }
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

    // ─────────────────────────────────────────── Library
    Component {
        id: libraryContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Library Folder"
                description: "Subfolders become playlists. Root MP3s go to 'Unsorted'."

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.rowSpacing

                        SettingsTextField {
                            id: mediaFolderField
                            Layout.fillWidth: true
                            text: settingsManager ? settingsManager.mediaFolder : ""

                            Connections {
                                target: settingsManager
                                function onMediaFolderChanged() {
                                    mediaFolderField.text = settingsManager.mediaFolder
                                }
                            }

                            onEditingFinished: {
                                if (text.trim() !== "" && settingsManager) {
                                    settingsManager.save_media_folder(text)
                                }
                            }
                        }

                        SettingsButton {
                            text: "Browse"
                            Layout.preferredHeight: mediaFolderField.height
                            onClicked: folderDialog.open()
                        }

                        SettingsButton {
                            text: "Scan"
                            Layout.preferredHeight: mediaFolderField.height
                            onClicked: {
                                if (mediaManager) mediaManager.scan_library()
                            }
                        }

                        SettingsButton {
                            text: "Strip"
                            Layout.preferredHeight: mediaFolderField.height
                            onClicked: {
                                if (mediaManager) mediaManager.strip_filenames()
                            }
                        }
                    }

                    App.TerminalFeedback {
                        id: scanTerminal
                        Layout.fillWidth: true
                        Layout.preferredHeight: pageRoot.dp(300)
                        Layout.topMargin: App.Spacing.rowSpacing
                        title: "Library Scan Terminal Output"

                        Connections {
                            target: mediaManager
                            function onScanProgress(message) {
                                scanTerminal.appendLine(message)
                            }
                        }
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── Playback
    Component {
        id: playbackContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Startup Volume"
                description: "Volume level applied when the app launches."

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: volumeSlider
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: settingsManager ? settingsManager.startUpVolume : 0.5
                        visualValue: Math.round(Math.sqrt(value) * 100)
                        valueDisplay: visualValue + "%"
                        activeColor: App.Style.volumeSliderColor
                        Layout.fillWidth: true

                        Timer {
                            id: volumeUpdateTimer
                            interval: 100
                            running: false
                            repeat: false
                            onTriggered: {
                                if (settingsManager) {
                                    settingsManager.save_start_volume(volumeSlider.value)
                                }
                            }
                        }

                        onMoved: volumeUpdateTimer.restart()
                    }

                    ValueDisplay {
                        text: volumeSlider.valueDisplay
                        Layout.fillWidth: false
                    }
                }
            }

            SettingCategory {
                title: "Autoplay on Startup"
                description: "Resume the last track automatically when the app launches."

                SettingsToggle {
                    id: autoPlayToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.autoPlayOnStartup : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_auto_play_on_startup(checked)
                    }

                    Connections {
                        target: settingsManager
                        function onAutoPlayOnStartupChanged() {
                            autoPlayToggle.checked = settingsManager.autoPlayOnStartup
                        }
                    }
                }
            }

            SettingCategory {
                title: "Remember Shuffle State"
                description: "Persist shuffle on/off across app restarts."

                SettingsToggle {
                    id: persistShuffleToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.persistShuffleState : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_persist_shuffle_state(checked)
                    }

                    Connections {
                        target: settingsManager
                        function onPersistShuffleStateChanged() {
                            persistShuffleToggle.checked = settingsManager.persistShuffleState
                        }
                    }
                }
            }

            SettingCategory {
                title: "Default to Library View"
                description: "When opening Music, show the library list instead of the now-playing screen."

                SettingsToggle {
                    id: musicButtonDefaultPageToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.musicButtonDefaultPage === "mediaPlayer" : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_music_button_default_page(checked ? "mediaPlayer" : "mediaRoom")
                        }
                    }

                    Connections {
                        target: settingsManager
                        function onMusicButtonDefaultPageChanged() {
                            musicButtonDefaultPageToggle.checked = settingsManager.musicButtonDefaultPage === "mediaPlayer"
                        }
                    }
                }
            }

            SettingCategory {
                title: "Return to Library After Playing"
                description: "Bounce back to the library list once a track has been queued from it."

                SettingsToggle {
                    id: returnToLibraryToggle
                    compact: true
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft
                    text: checked ? "On" : "Off"
                    checked: settingsManager ? settingsManager.returnToLibraryAfterSelection : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_return_to_library_after_selection(checked)
                    }

                    Connections {
                        target: settingsManager
                        function onReturnToLibraryAfterSelectionChanged() {
                            returnToLibraryToggle.checked = settingsManager.returnToLibraryAfterSelection
                        }
                    }
                }
            }

        }
    }

    // ─────────────────────────────────────────── Now Playing
    // L-shape: live MediaRoom PiP locked top-right, two scrollable
    // SettingCategory columns wrapping under and to the left of it.
    // Both scroll columns use the bare Flickable + ColumnLayout recipe
    // shared with every other settings submenu — no rail chrome.
    Component {
        id: nowPlayingContent
        Item {
            id: dashboard
            width: parent ? parent.width : 0
            // Fill the popup's body so the PiP can lock to the top-right
            // corner and the two column flickables get a real bottom anchor.
            // SettingsCardPopup's contentLoader sets height = implicitHeight,
            // so bind height explicitly — Item.height defaults to 0.
            implicitHeight: parent && parent.parent ? parent.parent.height : pageRoot.dp(620)
            height: implicitHeight

            readonly property int gap: pageRoot.dp(8)

            // PiP sized to the running window's aspect so the preview reads
            // as a faithful miniature. Caps at 40% of dashboard height to
            // leave the two scroll columns plenty of room.
            readonly property real windowAspect: (mainWindow && mainWindow.height > 0)
                ? (mainWindow.width / mainWindow.height) : (1280.0 / 720.0)
            readonly property real pipMaxHeight: Math.max(pageRoot.dp(160), dashboard.height * 0.40)
            readonly property real pipNaturalWidth: pipMaxHeight * windowAspect
            readonly property int leftColMin: pageRoot.dp(280)
            readonly property real pipFitWidth: Math.max(pageRoot.dp(160),
                Math.min(pipNaturalWidth, dashboard.width - leftColMin - dashboard.gap))
            readonly property real pipFitHeight: pipFitWidth / windowAspect

            // Bundled looks — apply with one tap, then dial in.
            readonly property var presetDefs: [
                { label: "Minimal", settings: {
                    roundedAlbumArt: true, showAlbumArtShadow: false, vinylRecordMode: false,
                    showWaveformVisualizer: false, show3DButtonTilt: false, show3DAlbumPreview: false,
                    backgroundBlurRadius: 60, backgroundOverlayOpacity: 80,
                    albumArtCornerRadius: 16, albumArtTransition: "Crossfade", backgroundGrid: "Normal"
                }},
                { label: "Vinyl", settings: {
                    roundedAlbumArt: false, showAlbumArtShadow: true, vinylRecordMode: true,
                    showWaveformVisualizer: true, show3DButtonTilt: false, show3DAlbumPreview: false,
                    backgroundBlurRadius: 30, backgroundOverlayOpacity: 60,
                    albumArtTransition: "Vinyl Lift", backgroundGrid: "Normal"
                }},
                { label: "Showcase 3D", settings: {
                    roundedAlbumArt: true, showAlbumArtShadow: true, vinylRecordMode: false,
                    showWaveformVisualizer: false, show3DButtonTilt: true, show3DAlbumPreview: true,
                    backgroundBlurRadius: 50, backgroundOverlayOpacity: 70,
                    albumArtCornerRadius: 16, albumArtTransition: "Coverflow",
                    backgroundGrid: "4x4",
                    sideCardOpacity: 0.4, sideCardAngle: 30, buttonTiltDuration: 200
                }},
                { label: "Cinema", settings: {
                    roundedAlbumArt: true, showAlbumArtShadow: true, vinylRecordMode: false,
                    showWaveformVisualizer: true, show3DButtonTilt: false, show3DAlbumPreview: false,
                    backgroundBlurRadius: 80, backgroundOverlayOpacity: 90,
                    albumArtCornerRadius: 24, albumArtTransition: "Dissolve", backgroundGrid: "4x4"
                }}
            ]

            function applyPreset(preset) {
                if (!settingsManager || !preset || !preset.settings) return
                var s = preset.settings
                if ("roundedAlbumArt" in s)         settingsManager.save_rounded_album_art(s.roundedAlbumArt)
                if ("showAlbumArtShadow" in s)      settingsManager.save_show_album_art_shadow(s.showAlbumArtShadow)
                if ("vinylRecordMode" in s)         settingsManager.save_vinyl_record_mode(s.vinylRecordMode)
                if ("showWaveformVisualizer" in s)  settingsManager.save_show_waveform_visualizer(s.showWaveformVisualizer)
                if ("show3DButtonTilt" in s)        settingsManager.save_show_3d_button_tilt(s.show3DButtonTilt)
                if ("show3DAlbumPreview" in s)      settingsManager.save_show_3d_album_preview(s.show3DAlbumPreview)
                if ("backgroundBlurRadius" in s)    settingsManager.save_background_blur_radius(s.backgroundBlurRadius)
                if ("backgroundOverlayOpacity" in s) settingsManager.save_background_overlay_opacity(s.backgroundOverlayOpacity)
                if ("albumArtCornerRadius" in s)    settingsManager.save_album_art_corner_radius(s.albumArtCornerRadius)
                if ("albumArtTransition" in s)      settingsManager.save_album_art_transition(s.albumArtTransition)
                if ("backgroundGrid" in s)          settingsManager.save_background_grid(s.backgroundGrid)
                if ("sideCardOpacity" in s)         settingsManager.save_side_card_opacity(s.sideCardOpacity)
                if ("sideCardAngle" in s)           settingsManager.save_side_card_angle(s.sideCardAngle)
                if ("buttonTiltDuration" in s)      settingsManager.save_button_tilt_duration(s.buttonTiltDuration)
            }

            // ── PiP locked top-right ──
            MediaRoomLivePreview {
                id: centerPip
                anchors.top: parent.top
                anchors.right: parent.right
                width: dashboard.pipFitWidth
                height: dashboard.pipFitHeight
            }

            // ── Left scroll: same bare Flickable + ColumnLayout of
            //    SettingCategory cards as Library / Playback / Display.
            Flickable {
                id: leftFlick
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: centerPip.left
                anchors.rightMargin: dashboard.gap
                contentWidth: width
                contentHeight: leftCol.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.DragAndOvershootBounds
                flickDeceleration: 1200
                maximumFlickVelocity: 4000
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

                ColumnLayout {
                    id: leftCol
                    width: leftFlick.width
                    spacing: App.Spacing.sectionSpacing

                    SettingCategory {
                        title: "Background Layout"
                        description: "Tile pattern for the album art behind the player."

                        NowPlayingButtonGrid {
                            id: bgGridCtl
                            options: ["Normal", "2x2", "4x4"]
                            currentValue: settingsManager ? settingsManager.backgroundGrid : "4x4"
                            onSelected: function(v) { if (settingsManager) settingsManager.save_background_grid(v) }
                            Connections {
                                target: settingsManager
                                function onBackgroundGridChanged() { bgGridCtl.currentValue = settingsManager.backgroundGrid }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Background Blur"
                        description: "Soften the album art behind the player."

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing

                            SettingsSlider {
                                id: blurSlider
                                from: 0; to: 100; stepSize: 1
                                value: settingsManager ? settingsManager.backgroundBlurRadius : 40
                                activeColor: App.Style.accent
                                Layout.fillWidth: true
                                onMoved: { if (settingsManager) settingsManager.save_background_blur_radius(blurSlider.value) }
                                Connections {
                                    target: settingsManager
                                    function onBackgroundBlurRadiusChanged() { blurSlider.value = settingsManager.backgroundBlurRadius }
                                }
                            }

                            ValueDisplay {
                                text: blurSlider.value.toFixed(0)
                                Layout.fillWidth: false
                            }
                        }
                    }

                    SettingCategory {
                        title: "Dark Overlay"
                        description: "Tint over the blurred background."

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing

                            SettingsSlider {
                                id: darkSlider
                                from: 0; to: 100; stepSize: 1
                                value: settingsManager ? settingsManager.backgroundOverlayOpacity : 80
                                activeColor: App.Style.accent
                                Layout.fillWidth: true
                                onMoved: { if (settingsManager) settingsManager.save_background_overlay_opacity(darkSlider.value) }
                                Connections {
                                    target: settingsManager
                                    function onBackgroundOverlayOpacityChanged() { darkSlider.value = settingsManager.backgroundOverlayOpacity }
                                }
                            }

                            ValueDisplay {
                                text: darkSlider.value.toFixed(0) + "%"
                                Layout.fillWidth: false
                            }
                        }
                    }

                    SettingCategory {
                        title: "Rounded Album Art"
                        description: "Soft corners on the current track's album art."

                        SettingsToggle {
                            id: roundedToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.roundedAlbumArt : true
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_rounded_album_art(c) }
                            Connections {
                                target: settingsManager
                                function onRoundedAlbumArtChanged() { roundedToggle.checked = settingsManager.roundedAlbumArt }
                            }
                        }

                        SettingSubcategory {
                            Layout.fillWidth: true
                            visible: settingsManager && settingsManager.roundedAlbumArt
                            title: "Corner Radius"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing

                                SettingsSlider {
                                    id: radiusSlider
                                    from: 2; to: 32; stepSize: 1
                                    value: settingsManager ? settingsManager.albumArtCornerRadius : 16
                                    activeColor: App.Style.accent
                                    Layout.fillWidth: true
                                    onMoved: { if (settingsManager) settingsManager.save_album_art_corner_radius(radiusSlider.value) }
                                    Connections {
                                        target: settingsManager
                                        function onAlbumArtCornerRadiusChanged() { radiusSlider.value = settingsManager.albumArtCornerRadius }
                                    }
                                }

                                ValueDisplay {
                                    text: radiusSlider.value.toFixed(0) + " dp"
                                    Layout.fillWidth: false
                                }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Album Art Shadow"
                        description: "Drop shadow behind the current track's album art."

                        SettingsToggle {
                            id: shadowToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.showAlbumArtShadow : true
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_album_art_shadow(c) }
                            Connections {
                                target: settingsManager
                                function onShowAlbumArtShadowChanged() { shadowToggle.checked = settingsManager.showAlbumArtShadow }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Vinyl Record Mode"
                        description: "Swap album art for a spinning vinyl while playing."

                        SettingsToggle {
                            id: vinylToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.vinylRecordMode : false
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_vinyl_record_mode(c) }
                            Connections {
                                target: settingsManager
                                function onVinylRecordModeChanged() { vinylToggle.checked = settingsManager.vinylRecordMode }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Waveform Visualizer"
                        description: "FFT bars reacting to the playing audio."

                        SettingsToggle {
                            id: waveformToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.showWaveformVisualizer : true
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_waveform_visualizer(c) }
                            Connections {
                                target: settingsManager
                                function onShowWaveformVisualizerChanged() { waveformToggle.checked = settingsManager.showWaveformVisualizer }
                            }
                        }
                    }

                    SettingCategory {
                        title: "3D Album Preview"
                        description: "Tilted side cards flanking the current track."

                        SettingsToggle {
                            id: preview3dToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.show3DAlbumPreview : false
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_3d_album_preview(c) }
                            Connections {
                                target: settingsManager
                                function onShow3DAlbumPreviewChanged() { preview3dToggle.checked = settingsManager.show3DAlbumPreview }
                            }
                        }

                        SettingSubcategory {
                            Layout.fillWidth: true
                            visible: settingsManager && settingsManager.show3DAlbumPreview
                            title: "Side Cards"

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.rowSpacing

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: App.Spacing.overallSpacing

                                    SettingsSlider {
                                        id: angleSlider
                                        from: 5; to: 60; stepSize: 1
                                        value: settingsManager ? settingsManager.sideCardAngle : 30
                                        activeColor: App.Style.accent
                                        Layout.fillWidth: true
                                        onMoved: { if (settingsManager) settingsManager.save_side_card_angle(angleSlider.value) }
                                        Connections {
                                            target: settingsManager
                                            function onSideCardAngleChanged() { angleSlider.value = settingsManager.sideCardAngle }
                                        }
                                    }

                                    ValueDisplay {
                                        text: angleSlider.value.toFixed(0) + "°"
                                        Layout.fillWidth: false
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: App.Spacing.overallSpacing

                                    SettingsSlider {
                                        id: sideOpSlider
                                        from: 0.1; to: 1.0; stepSize: 0.05
                                        value: settingsManager ? settingsManager.sideCardOpacity : 0.4
                                        activeColor: App.Style.accent
                                        Layout.fillWidth: true
                                        onMoved: { if (settingsManager) settingsManager.save_side_card_opacity(sideOpSlider.value) }
                                        Connections {
                                            target: settingsManager
                                            function onSideCardOpacityChanged() { sideOpSlider.value = settingsManager.sideCardOpacity }
                                        }
                                    }

                                    ValueDisplay {
                                        text: Math.round(sideOpSlider.value * 100) + "%"
                                        Layout.fillWidth: false
                                    }
                                }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Text Scroll Speed"
                        description: "How long marquees take to wipe across one cycle."

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing

                            SettingsSlider {
                                id: textScrollSpeedSlider
                                from: 1000; to: 10000; stepSize: 500
                                value: settingsManager ? settingsManager.textScrollSpeed : 5000
                                activeColor: App.Style.accent
                                Layout.fillWidth: true
                                onMoved: { if (settingsManager) settingsManager.save_text_scroll_speed(textScrollSpeedSlider.value) }
                                Connections {
                                    target: settingsManager
                                    function onTextScrollSpeedChanged() { textScrollSpeedSlider.value = settingsManager.textScrollSpeed }
                                }
                            }

                            ValueDisplay {
                                text: (textScrollSpeedSlider.value / 1000).toFixed(1) + " s"
                                Layout.fillWidth: false
                            }
                        }
                    }

                    SettingCategory {
                        title: "3D Button Tilt"
                        description: "Press effect on the playback control buttons."

                        SettingsToggle {
                            id: tiltToggle
                            compact: true
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            text: checked ? "On" : "Off"
                            checked: settingsManager ? settingsManager.show3DButtonTilt : false
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_3d_button_tilt(c) }
                            Connections {
                                target: settingsManager
                                function onShow3DButtonTiltChanged() { tiltToggle.checked = settingsManager.show3DButtonTilt }
                            }
                        }

                        SettingSubcategory {
                            Layout.fillWidth: true
                            visible: settingsManager && settingsManager.show3DButtonTilt
                            title: "Tilt Duration"

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing

                                SettingsSlider {
                                    id: tiltDurationSlider
                                    from: 50; to: 500; stepSize: 10
                                    value: settingsManager ? settingsManager.buttonTiltDuration : 200
                                    activeColor: App.Style.accent
                                    Layout.fillWidth: true
                                    onMoved: { if (settingsManager) settingsManager.save_button_tilt_duration(tiltDurationSlider.value) }
                                    Connections {
                                        target: settingsManager
                                        function onButtonTiltDurationChanged() { tiltDurationSlider.value = settingsManager.buttonTiltDuration }
                                    }
                                }

                                ValueDisplay {
                                    text: tiltDurationSlider.value.toFixed(0) + " ms"
                                    Layout.fillWidth: false
                                }
                            }
                        }
                    }
                }
            }

            // ── Bottom-right scroll: presets + track transition. Lives
            //    directly under the PiP so picking a preset / chip animates
            //    above. Same bare Flickable recipe as the left column.
            Flickable {
                id: bottomFlick
                anchors.top: centerPip.bottom
                anchors.topMargin: dashboard.gap
                anchors.bottom: parent.bottom
                anchors.left: centerPip.left
                anchors.right: parent.right
                contentWidth: width
                contentHeight: bottomCol.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.DragAndOvershootBounds
                flickDeceleration: 1200
                maximumFlickVelocity: 4000
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

                ColumnLayout {
                    id: bottomCol
                    width: bottomFlick.width
                    spacing: App.Spacing.sectionSpacing

                    SettingCategory {
                        title: "Presets"
                        description: "Apply a curated bundle, then dial in."

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: pageRoot.dp(8)
                            rowSpacing: pageRoot.dp(8)

                            Repeater {
                                model: dashboard.presetDefs
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: pageRoot.dp(40)
                                    radius: pageRoot.dp(8)
                                    color: presetMouse.containsMouse
                                        ? App.Style.accent
                                        : Qt.rgba(App.Style.primaryTextColor.r,
                                                  App.Style.primaryTextColor.g,
                                                  App.Style.primaryTextColor.b, 0.06)
                                    border.width: 1
                                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
                                    scale: presetMouse.pressed ? 0.96 : 1.0
                                    Behavior on color { ColorAnimation { duration: 120 } }
                                    Behavior on scale { NumberAnimation { duration: 80 } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: presetMouse.containsMouse ? "white" : App.Style.primaryTextColor
                                        font.pixelSize: App.Spacing.overallText - 1
                                        font.family: App.Style.fontFamily
                                        elide: Text.ElideRight
                                    }
                                    MouseArea {
                                        id: presetMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: dashboard.applyPreset(modelData)
                                    }
                                }
                            }
                        }
                    }

                    SettingCategory {
                        title: "Track Transition"
                        description: "Animation between songs. Tap a chip to preview."

                        NowPlayingButtonGrid {
                            id: trackTransitionCtl
                            options: ["Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip",
                                      "Coverflow", "Conveyor", "Stack", "Depth", "Swing"]
                            currentValue: settingsManager ? settingsManager.albumArtTransition : "Crossfade"
                            onSelected: function(v) {
                                if (!settingsManager) return
                                settingsManager.save_album_art_transition(v)
                                centerPip.previewTrackTransition(1, v)
                            }
                            Connections {
                                target: settingsManager
                                function onAlbumArtTransitionChanged() {
                                    trackTransitionCtl.currentValue = settingsManager.albumArtTransition
                                }
                            }
                        }
                    }
                }
            }
        }
    }


    // ─────────────────────────────────────────── Spotify
    Component {
        id: spotifyContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Spotify Connect"
                description: "Get credentials from developer.spotify.com"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: pageRoot.dp(4)

                            Text {
                                text: "Client ID"
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText - 2
                                font.family: App.Style.fontFamily
                            }

                            SettingsTextField {
                                id: spotifyClientIdField
                                Layout.fillWidth: true
                                text: settingsManager ? settingsManager.get_spotify_client_id() : ""

                                onEditingFinished: {
                                    if (settingsManager && text.trim() !== "") {
                                        settingsManager.save_spotify_credentials(
                                            text.trim(),
                                            spotifyClientSecretField.text.trim()
                                        )
                                    }
                                }

                                Connections {
                                    target: settingsManager
                                    function onSpotifyCredentialsChanged() {
                                        spotifyClientIdField.text = settingsManager.get_spotify_client_id()
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: pageRoot.dp(4)

                            Text {
                                text: "Client Secret"
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText - 2
                                font.family: App.Style.fontFamily
                            }

                            SettingsTextField {
                                id: spotifyClientSecretField
                                Layout.fillWidth: true
                                text: settingsManager ? settingsManager.get_spotify_client_secret() : ""
                                echoMode: TextInput.Password

                                onEditingFinished: {
                                    if (settingsManager && text.trim() !== "") {
                                        settingsManager.save_spotify_credentials(
                                            spotifyClientIdField.text.trim(),
                                            text.trim()
                                        )
                                    }
                                }

                                Connections {
                                    target: settingsManager
                                    function onSpotifyCredentialsChanged() {
                                        spotifyClientSecretField.text = settingsManager.get_spotify_client_secret()
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        SettingsButton {
                            text: "Connect"
                            Layout.preferredHeight: spotifyClientIdField.height
                            visible: spotifyManager && !spotifyManager.is_connected()
                            tooltipText: "Connect to Spotify"
                            onClicked: {
                                if (spotifyManager) spotifyManager.authenticate()
                            }
                        }

                        SettingsButton {
                            text: "Disconnect"
                            Layout.preferredHeight: spotifyClientIdField.height
                            Layout.minimumWidth: pageRoot.dp(90)
                            visible: spotifyManager && spotifyManager.is_connected()
                            buttonColor: App.Style.statusDanger
                            tooltipText: "Disconnect from Spotify"
                            onClicked: {
                                if (spotifyManager) spotifyManager.disconnect()
                            }
                        }

                        SettingsButton {
                            text: "Refresh"
                            Layout.preferredHeight: spotifyClientIdField.height
                            visible: spotifyManager && spotifyManager.is_connected()
                            tooltipText: "Refresh available devices"
                            onClicked: {
                                if (spotifyManager) spotifyManager.refresh_devices()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    App.TerminalFeedback {
                        id: spotifyTerminal
                        Layout.fillWidth: true
                        Layout.preferredHeight: pageRoot.dp(300)
                        title: "Spotify Connection Terminal Output"

                        Connections {
                            target: spotifyManager
                            function onStatusProgress(message) {
                                spotifyTerminal.appendLine(message)
                            }
                            function onAuthUrlReady(url) {
                                spotifyTerminal.appendLine("[INFO] Auth URL ready - opening browser...")
                                spotifyAuthUrlText.text = url
                                spotifyAuthUrlText.visible = true
                            }
                            function onConnectionStateChanged(connected) {
                                spotifyAuthUrlText.visible = false
                                if (connected && spotifyManager) {
                                    spotifyManager.refresh_devices()
                                }
                            }
                        }
                    }

                    Text {
                        id: spotifyAuthUrlText
                        Layout.fillWidth: true
                        visible: false
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText - 2
                        font.underline: true
                        font.family: App.Style.fontFamily
                        wrapMode: Text.WrapAnywhere
                        elide: Text.ElideMiddle
                        maximumLineCount: 2

                        ToolTip.visible: authUrlMouseArea.containsMouse
                        ToolTip.text: "Click to open in browser"
                        ToolTip.delay: 300

                        MouseArea {
                            id: authUrlMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(spotifyAuthUrlText.text)
                        }
                    }
                }
            }

            SettingCategory {
                title: "Available Devices"
                description: "Tap a device to make it the active Spotify Connect target."
                visible: spotifyManager && spotifyManager.is_connected()

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    Flow {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Repeater {
                            id: spotifyDevicesRepeater
                            model: pageRoot.spotifyDevicesList

                            Rectangle {
                                id: deviceChip
                                width: deviceChipText.width + App.Spacing.overallSpacing * 3
                                height: App.Spacing.formElementHeight * 0.8
                                radius: height / 2
                                color: modelData.is_active ? App.Style.accent : App.Style.hoverColor
                                border.width: modelData.is_active ? 0 : 1
                                border.color: Qt.rgba(App.Style.primaryTextColor.r,
                                                    App.Style.primaryTextColor.g,
                                                    App.Style.primaryTextColor.b, 0.1)

                                Text {
                                    id: deviceChipText
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: modelData.is_active ? "white" : App.Style.primaryTextColor
                                    font.pixelSize: App.Spacing.overallText
                                    font.family: App.Style.fontFamily
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (spotifyManager && !modelData.is_active) {
                                            spotifyManager.set_active_device(modelData.id)
                                        }
                                    }
                                    onEntered: deviceChip.scale = 1.05
                                    onExited: deviceChip.scale = 1.0
                                }

                                Behavior on scale { NumberAnimation { duration: 100 } }

                                layer.enabled: modelData.is_active
                                layer.effect: DropShadow {
                                    horizontalOffset: 0
                                    verticalOffset: 2
                                    radius: 4.0
                                    samples: 9
                                    color: Qt.rgba(0, 0, 0, 0.2)
                                }
                            }
                        }
                    }

                    Text {
                        visible: spotifyDevicesRepeater.count === 0
                        text: "No devices found. Open Spotify on a device first."
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                        font.italic: true
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

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
