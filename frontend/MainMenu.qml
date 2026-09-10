import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: mainMenu
    objectName: "mainMenu"
    property StackView stackView
    property ApplicationWindow mainWindow
    property real windowWidth
    property real windowHeight
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Media source property - determines if Spotify is active
    property bool useSpotify: settingsManager && settingsManager.mediaSource === "spotify" &&
                              spotifyManager && spotifyManager.is_connected()

    // Spotify track info cache (updated from signals)
    property string spotifyTrackName: ""
    property string spotifyArtist: ""
    property string spotifyAlbum: ""
    property string spotifyAlbumArt: ""

    // Media content properties - unified for both local and Spotify
    property string currentFile: ""
    property string currentArt: {
        if (useSpotify && spotifyAlbumArt) {
            return spotifyAlbumArt
        }
        return _localArt || "./assets/missing_art.png"
    }
    property string currentTitle: {
        if (useSpotify && spotifyTrackName) {
            return spotifyTrackName
        }
        return _localTitle || ""
    }
    property string currentArtist: {
        if (useSpotify && spotifyArtist) {
            return spotifyArtist
        }
        return _localArtist || ""
    }
    property string currentAlbum: {
        if (useSpotify && spotifyAlbum) {
            return spotifyAlbum
        }
        return _localAlbum || ""
    }

    // Internal properties for local media (to avoid binding loops)
    property string _localArt: ""
    property string _localTitle: ""
    property string _localArtist: ""
    property string _localAlbum: ""

    function formatTime(ms) {
        var minutes = Math.floor(ms / 60000)
        var seconds = Math.floor((ms % 60000) / 1000)
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    // Dark background with subtle gradient
    Rectangle {
        id: backgroundRect
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.darker(App.Style.backgroundColor, 1.2) }
            GradientStop { position: 1.0; color: App.Style.backgroundColor }
        }
    }

    // Main content area - Horizontal layout: Media on left (1/3), OBD on right (2/3)
    RowLayout {
        anchors.fill: parent
        anchors.margins: dp(10)
        spacing: dp(10)

        // ========== LEFT SECTION: Media Controls (1/3 of width) ==========
        Rectangle {
            id: mediaSection
            Layout.preferredWidth: parent.width * 0.33
            Layout.fillHeight: true
            color: "transparent"
            radius: dpMin(8, 2)
            clip: true

            // Album art blur background (like MediaRoom)
            Item {
                id: backgroundContainer
                anchors.fill: parent
                z: -1

                Image {
                    id: backgroundArtImage
                    anchors.fill: parent
                    source: mainMenu.currentArt || "./assets/missing_art.png"
                    fillMode: Image.PreserveAspectCrop
                    opacity: 1
                    layer.enabled: status === Image.Ready
                    layer.effect: MultiEffect {
                        blurEnabled: true
                        blurMax: 64
                        blur: settingsManager ? Math.min(1.0, settingsManager.backgroundBlurRadius / 64) : 0.625
                    }
                }

                // Dark overlay for readability
                Rectangle {
                    id: colorOverlay
                    anchors.fill: parent
                    color: "#C0000000"
                    opacity: 1.0
                }
            }

            // Border overlay (on top of blur)
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: App.Style.accent
                border.width: 2
                radius: dpMin(8, 2)
                z: 10
            }

            // Media content - vertical layout for narrow panel
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: dp(15)
                spacing: dp(10)

                // Album Art (top, centered)
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.width + dp(40)
                    Layout.alignment: Qt.AlignHCenter

                    Image {
                        id: albumArtImage
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height)
                        height: width
                        source: mainMenu.currentArt || "./assets/missing_art.png"
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true

                        layer.enabled: status === Image.Ready
                        layer.effect: DropShadow {
                            transparentBorder: true
                            horizontalOffset: 4
                            verticalOffset: 4
                            radius: 12.0
                            samples: 25
                            color: "#A0000000"
                        }
                    }
                }

                // Song title with scrolling (like MediaRoom)
                Item {
                    id: songTitleContainer
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    height: Math.ceil(App.Spacing.mainMenuSongTextSize * 1.4)

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
                            text: mainMenu.currentTitle || "No track playing"
                            color: App.Style.metadataColor
                            font.pixelSize: App.Spacing.mainMenuSongTextSize
                            font.bold: true
                            font.family: mainMenu.globalFont
                        }

                        Timer {
                            id: songScrollTimer
                            interval: 3000
                            running: songTitleText.width > songTitleContainer.width
                            repeat: true
                            onTriggered: {
                                if (songTitleFlickable.contentX === 0) {
                                    songScrollAnimation.to = songTitleText.width - songTitleFlickable.width
                                    songScrollAnimation.start()
                                } else {
                                    songScrollAnimation.to = 0
                                    songScrollAnimation.start()
                                }
                            }
                        }

                        NumberAnimation {
                            id: songScrollAnimation
                            target: songTitleFlickable
                            property: "contentX"
                            duration: 5000
                            easing.type: Easing.InOutQuad
                            onFinished: songScrollTimer.restart()
                        }
                    }
                }

                // Artist & Album with scrolling (like MediaRoom)
                Item {
                    id: metadataContainer
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    height: Math.ceil(App.Spacing.mainMenuArtistTextSize * 1.4)

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
                            spacing: dp(8)

                            Text {
                                text: mainMenu.currentFile ? mainMenu.currentArtist : "Select a song"
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.7
                            }
                            Text {
                                text: mainMenu.currentFile ? "•" : ""
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.8
                            }
                            Text {
                                text: mainMenu.currentFile ? mainMenu.currentAlbum : ""
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.8
                            }
                        }

                        Timer {
                            id: metadataScrollTimer
                            interval: 3000
                            running: metadataRow.width > metadataContainer.width
                            repeat: true
                            onTriggered: {
                                if (metadataFlickable.contentX === 0) {
                                    metadataScrollAnimation.to = metadataRow.width - metadataFlickable.width
                                    metadataScrollAnimation.start()
                                } else {
                                    metadataScrollAnimation.to = 0
                                    metadataScrollAnimation.start()
                                }
                            }
                        }

                        NumberAnimation {
                            id: metadataScrollAnimation
                            target: metadataFlickable
                            property: "contentX"
                            duration: 5000
                            easing.type: Easing.InOutQuad
                            onFinished: metadataScrollTimer.restart()
                        }
                    }
                }

                // Spacer
                Item { Layout.fillHeight: true }

                // Progress Bar
                RowLayout {
                    Layout.fillWidth: true
                    spacing: dp(8)

                    Text {
                        id: positionText
                        text: "0:00"
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.mainMenuTimeTextSize
                        font.family: mainMenu.globalFont
                        Layout.minimumWidth: App.Spacing.mainMenuTimeTextSize * 2.5
                    }

                    Slider {
                        id: progressSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: 0
                        enabled: mediaManager && mediaManager.get_duration() > 0

                        property bool userSeeking: false

                        background: Rectangle {
                            x: progressSlider.leftPadding
                            y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                            width: progressSlider.availableWidth
                            height: App.Spacing.mainMenuSliderHeight
                            radius: height / 2
                            color: App.Style.secondaryTextColor

                            Rectangle {
                                width: progressSlider.visualPosition * parent.width
                                height: parent.height
                                radius: height / 2
                                color: App.Style.accent
                            }
                        }

                        handle: Rectangle {
                            x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                            y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                            width: App.Spacing.mainMenuSliderHandleSize
                            height: App.Spacing.mainMenuSliderHandleSize
                            radius: width / 2
                            color: progressSlider.pressed ? App.Style.accent : App.Style.primaryTextColor
                            visible: true
                        }

                        onPressedChanged: {
                            if (pressed) {
                                userSeeking = true
                            } else {
                                userSeeking = false
                                if (mediaManager) {
                                    mediaManager.set_position(value)
                                }
                            }
                        }
                    }

                    Text {
                        id: durationText
                        text: "0:00"
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.mainMenuTimeTextSize
                        font.family: mainMenu.globalFont
                        Layout.minimumWidth: App.Spacing.mainMenuTimeTextSize * 2.5
                    }
                }
            }

            // Click to open MediaRoom/MediaPlayer based on setting.
            // TapHandler (not MouseArea) because the ColumnLayout above fills
            // this Rectangle with a Flickable song title, a Flickable metadata
            // row, and a Slider — all of which consume MouseArea clicks.
            // TapHandler observes taps without interfering with those drag
            // interactions, so the progress slider + scrolling title stay
            // usable while still letting an empty-space tap open the player.
            TapHandler {
                onTapped: {
                    var defaultPage = settingsManager ? settingsManager.musicButtonDefaultPage : "mediaRoom"
                    var targetPage = defaultPage === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml"
                    var props = { stackView: mainMenu.stackView }
                    if (defaultPage === "mediaPlayer") {
                        props.mainWindow = mainWindow
                    }
                    stackView.push(targetPage, props)
                }
            }
        }

        // ========== RIGHT SECTION: OBD (2/3 of width) ==========
        Rectangle {
            visible: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            border.color: App.Style.accent
            border.width: 2
            radius: dpMin(8, 2)

            // Use the HomeOBDView component (stacked vertically)
            HomeOBDView {
                anchors.fill: parent
                anchors.margins: dp(5)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    stackView.push("OBDMenu.qml", {
                        stackView: stackView,
                        mainWindow: mainWindow
                    })
                }
            }
        }
    }

    // Timer for initial delayed loading of media data
    Timer {
        id: initialLoadTimer
        interval: 0
        repeat: false
        running: false
        onTriggered: {
            updateMedia()
        }
    }

    // Function to update media information from local media manager
    function updateLocalMedia() {
        if (mediaManager) {
            var filename = mediaManager.get_current_file()
            if (filename) {
                mainMenu.currentFile = filename
                mainMenu._localTitle = mediaManager.get_display_name(filename)
                mainMenu._localArtist = mediaManager.get_band(filename)
                mainMenu._localAlbum = mediaManager.get_album(filename)
                mainMenu._localArt = mediaManager.get_album_art(filename)
            }
        }
    }

    // Function to update media information from Spotify
    function updateSpotifyMedia() {
        if (spotifyManager) {
            mainMenu.spotifyTrackName = spotifyManager.get_current_track_name() || ""
            mainMenu.spotifyArtist = spotifyManager.get_current_artist() || ""
            mainMenu.spotifyAlbum = spotifyManager.get_current_album() || ""
            mainMenu.spotifyAlbumArt = spotifyManager.get_current_album_art() || ""
        }
    }

    // Function to update media based on current source
    function updateMedia() {
        if (useSpotify) {
            updateSpotifyMedia()
        } else {
            updateLocalMedia()
        }
    }

    Component.onCompleted: {
        initialLoadTimer.start()
        if (obdManager && obdManager.refresh_values) {
            obdManager.refresh_values()
        }
    }

    // Local Media Connections (only apply when not using Spotify)
    Connections {
        target: mediaManager

        function onMetadataChanged(title, artist, album) {
            if (!useSpotify) {
                mainMenu._localTitle = title
                mainMenu._localArtist = artist
                mainMenu._localAlbum = album
                updateLocalMedia()
            }
        }

        function onPositionChanged(position) {
            if (!useSpotify && !progressSlider.userSeeking) {
                progressSlider.value = position
                positionText.text = formatTime(position)
            }
        }

        function onDurationChanged(duration) {
            if (!useSpotify) {
                progressSlider.to = duration > 0 ? duration : 1
                durationText.text = formatTime(duration)
            }
        }

        function onCurrentMediaChanged(filename) {
            if (!useSpotify && mediaManager) {
                var duration = mediaManager.get_duration()
                durationText.text = formatTime(duration)
                positionText.text = "0:00"
                updateLocalMedia()
            }
        }
    }

    // Spotify Connections
    Connections {
        target: spotifyManager

        function onCurrentTrackChanged(title, artist, album, artUrl) {
            // Always update the cached Spotify track info
            mainMenu.spotifyTrackName = title
            mainMenu.spotifyArtist = artist
            mainMenu.spotifyAlbum = album
            mainMenu.spotifyAlbumArt = artUrl

            // Only update progress UI if we're in Spotify mode
            if (useSpotify) {
                progressSlider.value = 0
                positionText.text = "0:00"
                if (spotifyManager) {
                    var duration = spotifyManager.get_duration()
                    progressSlider.to = duration > 0 ? duration : 1
                    durationText.text = formatTime(duration)
                }
            }
        }

        function onPositionChanged(position) {
            if (useSpotify && !progressSlider.userSeeking) {
                progressSlider.value = position
                positionText.text = formatTime(position)
            }
        }

        function onDurationChanged(duration) {
            if (useSpotify) {
                progressSlider.to = duration > 0 ? duration : 1
                durationText.text = formatTime(duration)
            }
        }
    }

    // Handle media source changes
    Connections {
        target: settingsManager
        function onMediaSourceChanged(source) {
            // When source changes, update media display accordingly
            updateMedia()
        }
        function onHomeOBDParametersChanged() {
            if (obdManager && obdManager.refresh_values) {
                obdManager.refresh_values()
            }
        }
    }
}
