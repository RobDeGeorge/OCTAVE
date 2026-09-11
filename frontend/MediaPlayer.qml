import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "." as App

Item {
    id: mediaPlayer
    objectName: "mediaPlayer"
    required property StackView stackView
    required property ApplicationWindow mainWindow

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Local dp() / dpMin() helpers — Qt on Android loads QML singletons from
    // assets:/ URLs with functions un-callable (properties still work). These
    // inline wrappers use only App.Spacing.effectiveScale (a property), so they
    // work everywhere. Keep identical math to Spacing.qml's dp/dpMin.
    function dp(size) {
        return Math.round(size * (App.Spacing.effectiveScale || 1.0))
    }
    function dpMin(size, floor) {
        return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0)))
    }

    // Navigate to cached DownloadPage, pre-selecting the current playlist
    function openDownloadPage() {
        var sv = mediaPlayer.stackView
        if (sv.currentItem && sv.currentItem.objectName === "downloadPage") return

        // Determine current local playlist to pre-select (skip special playlists)
        var preselect = ""
        if (!mediaPlayer.isSpotifyPlaylist && currentPlaylistName
            && currentPlaylistName !== "All Music" && currentPlaylistName !== "Unsorted") {
            preselect = currentPlaylistName
        }

        // Reuse cached page if it exists
        if (sv._cachedDownloadPage) {
            // Always refresh playlist list (may have changed while cached)
            if (mediaManager)
                sv._cachedDownloadPage.downloadPlaylists = mediaManager.get_movable_playlist_names()
            // Check if it's already in the stack
            for (var i = 0; i < sv.depth; i++) {
                if (sv.get(i) === sv._cachedDownloadPage) {
                    sv.pop(sv._cachedDownloadPage)
                    syncDownloadPlaylist(preselect)
                    return
                }
            }
            sv.push(sv._cachedDownloadPage)
            syncDownloadPlaylist(preselect)
            return
        }

        // First time: create and cache on the stackView
        var component = Qt.createComponent("DownloadPage.qml")
        if (component.status === Component.Ready) {
            sv._cachedDownloadPage = component.createObject(null, {
                stackView: sv,
                mainWindow: mediaPlayer.mainWindow
            })
            if (sv._cachedDownloadPage) {
                sv.push(sv._cachedDownloadPage)
                syncDownloadPlaylist(preselect)
            }
        }
    }

    function syncDownloadPlaylist(name) {
        if (!name) return
        var dp = stackView._cachedDownloadPage
        if (dp) {
            dp.selectedPlaylist = name
            downloadManager.set_download_playlist(name)
        }
    }

    // Core properties
    property var mediaFiles: []
    property string lastPlayedSong: ""
    property bool isPaused: false
    
    // Playlist properties
    property var playlistNames: []
    property string currentPlaylistName: ""

    // Spotify playlist properties
    property bool spotifyConnected: spotifyManager && spotifyManager.is_connected()
    property var spotifyPlaylistNames: []
    property var spotifyTracks: []
    property var spotifyTrackNames: []
    property string currentSpotifyPlaylistName: ""
    property bool isSpotifyPlaylist: false  // true if currently viewing a Spotify playlist
    property int playlistRefreshCounter: 0  // Incremented to force delegate rebindings

    // Cached playback state to avoid constant re-evaluation in delegates
    property string currentSpotifyTrackName: spotifyManager ? spotifyManager.get_current_track_name() : ""
    property bool spotifyIsPlaying: false

    // Initial scroll position (calculated before render)
    property real initialScrollPosition: -1

    // Force model refresh when switching between local and Spotify playlists
    onIsSpotifyPlaylistChanged: {
        console.log("isSpotifyPlaylist changed to: " + isSpotifyPlaylist)
        playlistRefreshCounter++  // Force delegates to re-evaluate bindings
        updateTimer.restart()
    }

    // Combined playlists for unified dropdown
    property var combinedPlaylists: {
        var list = []
        // Add local playlists first
        for (var i = 0; i < playlistNames.length; i++) {
            list.push({ name: playlistNames[i], type: "local" })
        }
        // Add Spotify playlists if connected
        if (spotifyConnected && spotifyPlaylistNames.length > 0) {
            for (var j = 0; j < spotifyPlaylistNames.length; j++) {
                list.push({ name: spotifyPlaylistNames[j], type: "spotify" })
            }
        }
        return list
    }

    // Get the display name for the dropdown
    property string displayPlaylistName: isSpotifyPlaylist ? currentSpotifyPlaylistName : currentPlaylistName

    // Sorting properties
    property bool sortByTitleAscending: true
    property bool sortByAlbumAscending: true
    property bool sortByArtistAscending: true
    property string currentSortColumn: "title" // Can be "title", "album", "artist", or "none"

    // Sort media files based on current sort column and direction
    function sortMediaFiles() {
        if (mediaManager) {
            // Call backend to perform the sorting
            let ascending = currentSortColumn === "title" ? sortByTitleAscending :
                            currentSortColumn === "album" ? sortByAlbumAscending :
                            sortByArtistAscending

            mediaFiles = mediaManager.sort_media_files(currentSortColumn, ascending)
        }
    }

    // Calculate initial scroll position for a given track
    function calculateScrollPosition(trackName, model) {
        if (!trackName || model.length === 0) return -1

        for (var i = 0; i < model.length; i++) {
            if (model[i] === trackName) {
                var itemHeight = App.Spacing.mediaPlayerRowHeight * 1.4 + 6
                // Estimate list height (will be corrected when ListView is ready)
                var listHeight = 600
                var targetY = (i * itemHeight) - (listHeight / 2) + (itemHeight / 2)
                return Math.max(0, targetY)
            }
        }
        return -1
    }

    // Initialize component
    Component.onCompleted: {
        if (mediaManager) {
            // Load playlist names
            playlistNames = mediaManager.get_playlist_names()
            currentPlaylistName = mediaManager.get_current_playlist_name()

            mediaManager.get_media_files()
            var currentFile = mediaManager.get_current_file()
            if (currentFile) {
                lastPlayedSong = currentFile
                isPaused = !mediaManager.is_playing()
                // Pre-calculate scroll position for local files
                initialScrollPosition = calculateScrollPosition(currentFile, mediaFiles)
            }
            updateTimer.restart()
        }

        // Load Spotify playlists if connected
        if (spotifyConnected && spotifyManager) {
            spotifyPlaylistNames = spotifyManager.get_spotify_playlist_names()

            // Check if we should restore Spotify playlist state
            // If Spotify is the current media source and has a playlist loaded, show it
            if (settingsManager && settingsManager.mediaSource === "spotify" &&
                spotifyManager.has_spotify_playlist_loaded()) {

                console.log("Restoring Spotify playlist state")
                isSpotifyPlaylist = true
                currentSpotifyPlaylistName = spotifyManager.get_current_spotify_playlist_name()
                currentSpotifyTrackName = spotifyManager.get_current_track_name()

                // Load the tracks for display
                var playlistId = spotifyManager.get_current_spotify_playlist_id()
                if (playlistId) {
                    spotifyManager.select_spotify_playlist(playlistId)
                }
            }
        }

        // For local files, apply scroll position after a brief delay to ensure ListView is ready
        if (!isSpotifyPlaylist && initialScrollPosition >= 0) {
            scrollToCurrentTimer.restart()
        }

        // Refresh album art after initial load to ensure cached art is displayed
        albumArtRefreshTimer.restart()
    }

    // Timer to trigger album art refresh after model changes settle
    Timer {
        id: updateTimer
        interval: 50
        repeat: false
        onTriggered: {
            // Trigger album art refresh after delegates have had time to cache
            albumArtRefreshTimer.restart()
        }
    }

    // Timer to refresh album art bindings after initial cache population
    Timer {
        id: albumArtRefreshTimer
        interval: 300  // Give time for album art to be cached by backend
        repeat: false
        onTriggered: {
            playlistRefreshCounter++  // Force delegates to re-evaluate albumArtSource
        }
    }

    // Function to scroll to the currently playing track (instant, no animation)
    function scrollToCurrentTrack() {
        var currentTrackName = ""
        var model = []

        if (isSpotifyPlaylist) {
            currentTrackName = currentSpotifyTrackName
            model = spotifyTrackNames
        } else {
            currentTrackName = lastPlayedSong
            model = mediaFiles
        }

        if (!currentTrackName || model.length === 0) return

        // Find the index of the current track
        for (var i = 0; i < model.length; i++) {
            if (model[i] === currentTrackName) {
                // Calculate the content position to center this item
                var itemHeight = App.Spacing.mediaPlayerRowHeight * 1.4 + 6  // height + spacing
                var targetY = (i * itemHeight) - (mediaListView.height / 2) + (itemHeight / 2)
                // Clamp to valid range
                targetY = Math.max(0, Math.min(targetY, mediaListView.contentHeight - mediaListView.height))
                // Set position directly (instant, no animation)
                mediaListView.contentY = targetY
                console.log("Scrolled to track at index: " + i)
                break
            }
        }
    }

    // Timer to scroll to current track after model is loaded
    Timer {
        id: scrollToCurrentTimer
        interval: 50  // Minimal delay, just enough for model to populate
        repeat: false
        onTriggered: scrollToCurrentTrack()
    }

    // Main content
    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

        // Title bar at the top
        Rectangle {
            id: titleBar
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            height: App.Spacing.bottomBarNavButtonHeight + App.Spacing.mediaRoomMargin * 2
            color: App.Style.headerBackgroundColor
            z: 2

            // Stats (left side, vertically centered)
            Row {
                id: statsRow
                anchors {
                    left: parent.left
                    leftMargin: App.Spacing.overallMargin * 4
                    verticalCenter: parent.verticalCenter
                }
                spacing: App.Spacing.overallMargin * 2

                Text {
                    text: (isSpotifyPlaylist ? spotifyTrackNames.length : mediaFiles.length) + " songs"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.2
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: !isSpotifyPlaylist
                    text: "\u00B7"
                    color: Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.4)
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.4
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: albumCountText
                    visible: !isSpotifyPlaylist
                    text: (mediaManager ? mediaManager.get_album_count() : "-") + " albums"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.2
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: !isSpotifyPlaylist
                    text: "\u00B7"
                    color: Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.4)
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.4
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: artistCountText
                    visible: !isSpotifyPlaylist
                    text: (mediaManager ? mediaManager.get_artist_count() : "-") + " artists"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.2
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    visible: !isSpotifyPlaylist
                    text: "\u00B7"
                    color: Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.4)
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.4
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: totalDurationText
                    visible: !isSpotifyPlaylist
                    text: mediaManager ? mediaManager.get_total_duration() : "--:--:--"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.2
                    font.family: mediaPlayer.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                // Spotify indicator
                Row {
                    visible: isSpotifyPlaylist
                    spacing: App.Spacing.overallMargin
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: dp(8)
                        height: dp(8)
                        radius: dp(4)
                        color: "#1DB954"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "Spotify"
                        color: "#1DB954"
                        font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.1
                        font.family: mediaPlayer.globalFont
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // New playlist button (left of dropdown)
            Rectangle {
                id: newPlaylistButton
                width: App.Spacing.bottomBarNavButtonHeight
                height: App.Spacing.bottomBarNavButtonHeight
                anchors {
                    right: playlistDropdownContainer.left
                    rightMargin: App.Spacing.overallMargin * 2
                    verticalCenter: parent.verticalCenter
                }
                color: newPlaylistMouse.pressed
                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                    : "transparent"
                radius: dpMin(8, 2)
                border.width: 1
                border.color: App.Style.accent

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: App.Style.accent
                    font.pixelSize: dp(22)
                    font.weight: Font.Bold
                    font.family: mediaPlayer.globalFont
                }

                MouseArea {
                    id: newPlaylistMouse
                    width: parent.width * 2
                    height: parent.height * 2
                    anchors.centerIn: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: newPlaylistDialog.open()
                }
            }

            // Playlist dropdown (right side, next to download button)
            Item {
                id: playlistDropdownContainer
                width: Math.min(dp(220), parent.width * 0.25)
                height: App.Spacing.bottomBarNavButtonHeight
                anchors {
                    right: parent.right
                    rightMargin: App.Spacing.mediaRoomMargin + App.Spacing.bottomBarNavButtonWidth + App.Spacing.overallMargin * 2
                    verticalCenter: parent.verticalCenter
                }

                // Cooldown to prevent CloseOnPressOutside from immediately re-opening
                property bool dropdownCooldown: false
                Timer {
                    id: dropdownCooldownTimer
                    interval: 100
                    onTriggered: playlistDropdownContainer.dropdownCooldown = false
                }

                Rectangle {
                    id: playlistDropdown
                    anchors.fill: parent
                    color: dropdownMouseArea.containsMouse ? Qt.lighter(App.Style.hoverColor, 1.2) : "transparent"
                    radius: dpMin(8, 2)
                    border.width: 1
                    border.color: App.Style.accent

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }

                    // Playlist label
                    Text {
                        id: dropdownLabel
                        anchors.top: parent.top
                        anchors.topMargin: dp(4)
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "PLAYLIST"
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 0.85
                        font.bold: true
                        font.family: mediaPlayer.globalFont
                        font.letterSpacing: dp(1)
                    }

                    // Display text
                    Text {
                        id: dropdownText
                        anchors.left: parent.left
                        anchors.right: dropdownArrow.left
                        anchors.top: dropdownLabel.bottom
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: App.Spacing.overallMargin * 2
                        verticalAlignment: Text.AlignVCenter
                        text: displayPlaylistName || "Select Playlist"
                        color: isSpotifyPlaylist ? "#1DB954" : App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.4
                        font.bold: true
                        font.family: mediaPlayer.globalFont
                        elide: Text.ElideRight
                    }

                    // Dropdown arrow
                    Text {
                        id: dropdownArrow
                        anchors.right: parent.right
                        anchors.rightMargin: App.Spacing.overallMargin * 2
                        anchors.verticalCenter: parent.verticalCenter
                        text: playlistPopup.visible ? "\u25B2" : "\u25BC"
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText
                        font.family: mediaPlayer.globalFont
                    }

                    MouseArea {
                        id: dropdownMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (playlistDropdownContainer.dropdownCooldown) return
                            if (playlistPopup.visible) {
                                playlistPopup.close()
                            } else {
                                playlistPopup.open()
                            }
                        }
                    }
                }

                // Popup menu
                Popup {
                    id: playlistPopup
                    parent: playlistDropdownContainer
                    y: playlistDropdown.height + dp(4)
                    width: playlistDropdown.width
                    height: Math.min(playlistColumn.implicitHeight + dp(20), dp(400))
                    padding: dp(10)
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    onClosed: {
                        playlistDropdownContainer.dropdownCooldown = true
                        dropdownCooldownTimer.restart()
                    }

                    background: Rectangle {
                        color: App.Style.backgroundColor
                        border.color: App.Style.accent
                        border.width: 1
                        radius: dpMin(8, 2)
                    }

                    contentItem: Flickable {
                        clip: true
                        contentHeight: playlistColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: playlistColumn
                            width: parent.width
                            spacing: dp(4)

                            Repeater {
                                model: combinedPlaylists

                                Rectangle {
                                    width: playlistColumn.width
                                    height: App.Spacing.bottomBarNavButtonHeight
                                    color: {
                                        var isActive = (modelData.type !== "spotify" && modelData.name === currentPlaylistName) ||
                                                       (modelData.type === "spotify" && modelData.name === currentSpotifyPlaylistName)
                                        if (isActive) return Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                                        if (itemMouseArea.containsMouse) {
                                            return modelData.type === "spotify" ? Qt.rgba(0.114, 0.725, 0.329, 0.3) : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                                        }
                                        return "transparent"
                                    }
                                    radius: dpMin(6, 2)
                                    border.width: {
                                        var isActive = (modelData.type !== "spotify" && modelData.name === currentPlaylistName) ||
                                                       (modelData.type === "spotify" && modelData.name === currentSpotifyPlaylistName)
                                        return isActive ? 1 : 0
                                    }
                                    border.color: App.Style.accent

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: App.Spacing.overallMargin * 2
                                        anchors.rightMargin: App.Spacing.overallMargin * 2
                                        spacing: App.Spacing.overallMargin * 2

                                        // Spotify indicator circle
                                        Rectangle {
                                            visible: modelData.type === "spotify"
                                            width: dp(10)
                                            height: dp(10)
                                            radius: dp(5)
                                            color: "#1DB954"
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: {
                                                if (modelData.type === "spotify") return "#1DB954"
                                                return App.Style.primaryTextColor
                                            }
                                            font.pixelSize: App.Spacing.overallText * 1.1
                                            font.bold: (modelData.type !== "spotify" && modelData.name === currentPlaylistName) ||
                                                       (modelData.type === "spotify" && modelData.name === currentSpotifyPlaylistName)
                                            font.family: mediaPlayer.globalFont
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        id: itemMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            console.log("Playlist selected: " + modelData.name + ", type: " + modelData.type)
                                            if (modelData.type === "spotify") {
                                                mediaPlayer.isSpotifyPlaylist = true
                                                mediaPlayer.currentSpotifyPlaylistName = modelData.name
                                                if (settingsManager) {
                                                    settingsManager.set_media_source("spotify")
                                                }
                                                if (spotifyManager) {
                                                    var playlistId = spotifyManager.get_spotify_playlist_id(modelData.name)
                                                    if (playlistId) {
                                                        spotifyManager.select_spotify_playlist(playlistId)
                                                    }
                                                }
                                            } else {
                                                mediaPlayer.isSpotifyPlaylist = false
                                                if (settingsManager) {
                                                    settingsManager.set_media_source("local")
                                                }
                                                if (mediaManager && modelData.name) {
                                                    mediaManager.select_playlist(modelData.name)
                                                }
                                            }
                                            playlistPopup.close()
                                        }
                                        onPressAndHold: {
                                            // Only allow deleting local, non-special playlists
                                            if (modelData.type !== "spotify" && modelData.name !== "All Music" && modelData.name !== "Unsorted") {
                                                deletePlaylistDialog.playlistToDelete = modelData.name
                                                playlistPopup.close()
                                                deletePlaylistDialog.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            active: true
                            policy: ScrollBar.AsNeeded
                        }
                    }
                }
            }
        }

        // Download Button (top right corner, overlays title bar)
        // Hidden when the local playlist is empty (unless a Spotify playlist is shown), since there is nothing to add to yet
        Control {
            id: downloadButton
            visible: (mediaFiles.length > 0) || isSpotifyPlaylist
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
                scale: downloadMouseArea.pressed ? 0.8 : 1.0
                opacity: downloadMouseArea.pressed ? 0.7 : 1.0
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
                scale: downloadMouseArea.pressed ? 0.8 : 1.0
                opacity: downloadMouseArea.pressed ? 0.7 : 1.0
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
                    id: downloadButtonImage
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
                    anchors.fill: downloadButtonImage
                    source: downloadButtonImage
                    color: App.Style.accent
                }
            }

            MouseArea {
                id: downloadMouseArea
                width: parent.width * 2
                height: parent.height * 2
                anchors.centerIn: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: mediaPlayer.openDownloadPage()
            }
        }

        // Main content area
        Rectangle {
            id: mainContent
            anchors {
                fill: parent
                margins: App.Spacing.overallMargin
                topMargin: titleBar.height + App.Spacing.overallMargin
            }
            color: App.Style.backgroundColor

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Table header
                Rectangle {
                    Layout.fillWidth: true
                    height: App.Spacing.mediaPlayerHeaderHeight * 2.0
                    color: App.Style.headerBackgroundColor
                    
                    Rectangle {
                        width: parent.width
                        height: 2
                        color: App.Style.accent
                        anchors.top: parent.top
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: App.Spacing.overallMargin * 2
                        anchors.rightMargin: App.Spacing.overallMargin * 2
                        spacing: 0

                        // Title header with sort functionality
                        Item {
                            Layout.preferredWidth: parent.width * 0.4
                            Layout.fillHeight: true

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (currentSortColumn === "title") {
                                        sortByTitleAscending = !sortByTitleAscending
                                    } else {
                                        currentSortColumn = "title"
                                        sortByTitleAscending = true
                                    }
                                    sortMediaFiles()
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "TITLE " + (currentSortColumn === "title" ?
                                    (sortByTitleAscending ? "↑" : "↓") : "")
                                color: App.Style.headerTextColor
                                font.pixelSize: App.Spacing.mediaPlayerTextSize * 1.2
                                font.family: mediaPlayer.globalFont
                                font.bold: true
                            }
                        }

                        // Artist header with sort functionality
                        Item {
                            Layout.preferredWidth: parent.width * 0.3
                            Layout.fillHeight: true

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (currentSortColumn === "artist") {
                                        sortByArtistAscending = !sortByArtistAscending
                                    } else {
                                        currentSortColumn = "artist"
                                        sortByArtistAscending = true
                                    }
                                    sortMediaFiles()
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "ARTIST " + (currentSortColumn === "artist" ?
                                    (sortByArtistAscending ? "↑" : "↓") : "")
                                color: App.Style.headerTextColor
                                font.pixelSize: App.Spacing.mediaPlayerTextSize * 1.2
                                font.family: mediaPlayer.globalFont
                                font.bold: true
                            }
                        }

                        // Album header with sort functionality
                        Item {
                            Layout.preferredWidth: parent.width * 0.3
                            Layout.fillHeight: true

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (currentSortColumn === "album") {
                                        sortByAlbumAscending = !sortByAlbumAscending
                                    } else {
                                        currentSortColumn = "album"
                                        sortByAlbumAscending = true
                                    }
                                    sortMediaFiles()
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "ALBUM " + (currentSortColumn === "album" ?
                                    (sortByAlbumAscending ? "↑" : "↓") : "")
                                color: App.Style.headerTextColor
                                font.pixelSize: App.Spacing.mediaPlayerTextSize * 1.2
                                font.family: mediaPlayer.globalFont
                                font.bold: true
                            }
                        }
                    }
                }

                // Media list
                ListView {
                    id: mediaListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: isSpotifyPlaylist ? spotifyTrackNames : mediaFiles
                    cacheBuffer: height * 0.5
                    displayMarginBeginning: dp(40)
                    displayMarginEnd: dp(40)
                    reuseItems: true

                    // Add spacing between items
                    spacing: dp(6)

                    // Set initial position when ListView geometry is ready
                    onHeightChanged: {
                        if (height > 0 && initialScrollPosition >= 0) {
                            // Recalculate with actual height and apply
                            scrollToCurrentTrack()
                            initialScrollPosition = -1  // Only do this once
                        }
                    }

                    // List item delegate
                    delegate: Item {
                        id: delegate
                        width: ListView.view.width
                        height: App.Spacing.mediaPlayerRowHeight * 1.4

                        // Active song properties - check against Spotify current track when in Spotify mode
                        // Bind directly to cached properties for stable updates without model refresh
                        property bool isCurrentSong: mediaPlayer.isSpotifyPlaylist
                            ? (mediaPlayer.currentSpotifyTrackName === modelData)
                            : (lastPlayedSong === modelData)

                        property bool isPlaying: mediaPlayer.isSpotifyPlaylist
                            ? (isCurrentSong && mediaPlayer.spotifyIsPlaying)
                            : (isCurrentSong && !isPaused)

                        // Properties for album art - conditionally fetch from Spotify or local
                        property var albumArtSource: {
                            // Reference playlistRefreshCounter to force rebinding
                            var _ = mediaPlayer.playlistRefreshCounter
                            if (!visible) return ""
                            if (mediaPlayer.isSpotifyPlaylist && spotifyManager) {
                                return spotifyManager.get_spotify_track_image(modelData) || "./assets/missing_art.png"
                            }
                            return mediaManager ? (mediaManager.get_album_art(modelData) || "./assets/missing_art.png") : "./assets/missing_art.png"
                        }
                        
                        // Generate consistent value based on song name
                        property real randomValue: {
                            var hash = 0;
                            for (var i = 0; i < modelData.length; i++) {
                                hash = ((hash << 5) - hash) + modelData.charCodeAt(i);
                                hash = hash & hash;
                            }
                            return Math.abs(hash) / 2147483647;
                        }
                                                
                        // Modern glass-style card with theme awareness
                        Rectangle {
                            id: glassCard
                            anchors.fill: parent
                            anchors.margins: dp(2)
                            radius: dpMin(10, 2)
                                                        
                            // Glass background - adapts to app theme colors
                            color: {
                                if (delegate.isCurrentSong) {
                                    var accent = App.Style.accent;
                                    return Qt.rgba(accent.r, accent.g, accent.b, 0.35);
                                }
                                return "transparent"
                            }

                            // Inner border for glass effect
                            border.width: delegate.isCurrentSong ? 2 : 0
                            border.color: {
                                var baseColor = App.Style.accent;
                                return Qt.rgba(
                                    baseColor.r,
                                    baseColor.g,
                                    baseColor.b,
                                    0.8
                                );
                            }
                            
                            // Create art-based accent layer as a strip on the left side
                            Rectangle {
                                id: accentStrip
                                width: parent.width
                                height: parent.height
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                radius: parent.radius
                                
                                // Clip to only show left part
                                clip: true
                                
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal

                                    GradientStop {
                                        position: 0.0
                                        color: {
                                            if (!delegate.isCurrentSong) return "transparent"
                                            var accentBase = App.Style.accent;
                                            return Qt.rgba(accentBase.r, accentBase.g, accentBase.b, 0.2);
                                        }
                                    }

                                    GradientStop {
                                        position: 1.0
                                        color: "transparent"
                                    }
                                }
                                
                                // Album art as texture overlay with varying opacity
                                Image {
                                    anchors.fill: parent
                                    source: delegate.albumArtSource
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: delegate.isCurrentSong ? 0.10 : 0.03
                                    
                                    // Create a small random offset for visual interest
                                    transform: Translate {
                                        x: -10 + (delegate.randomValue * 20)
                                        y: -10 + ((1 - delegate.randomValue) * 20)
                                    }
                                }
                            }
                            
                            // Active song indicator - glowing accent bar
                            Rectangle {
                                visible: delegate.isCurrentSong
                                width: dp(6)
                                height: parent.height
                                radius: width / 2
                                anchors.left: parent.left
                                anchors.leftMargin: dp(2)
                                anchors.verticalCenter: parent.verticalCenter
                                color: App.Style.accent
                                opacity: pulseAnimation.opacity
                                
                                // Pulse animation
                                SequentialAnimation {
                                    id: pulseAnimation
                                    running: delegate.isPlaying
                                    loops: Animation.Infinite
                                    alwaysRunToEnd: true
                                    property real opacity: 1.0
                                    
                                    NumberAnimation {
                                        target: pulseAnimation
                                        property: "opacity"
                                        from: 0.7
                                        to: 1.0
                                        duration: 800
                                        easing.type: Easing.InOutQuad
                                    }
                                    NumberAnimation {
                                        target: pulseAnimation
                                        property: "opacity"
                                        from: 1.0
                                        to: 0.7
                                        duration: 800
                                        easing.type: Easing.InOutQuad
                                    }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: App.Spacing.overallMargin * 2
                                anchors.rightMargin: App.Spacing.overallMargin * 2
                                spacing: 0

                                // Title section (with album art)
                                RowLayout {
                                    Layout.preferredWidth: parent.width * 0.4
                                    Layout.fillHeight: true
                                    spacing: App.Spacing.overallMargin * 2

                                    // Album art with frame - simplified
                                    Rectangle {
                                        id: albumArtContainer
                                        Layout.preferredWidth: App.Spacing.mediaPlayerRowHeight * 1.25
                                        Layout.preferredHeight: App.Spacing.mediaPlayerRowHeight * 1.25
                                        radius: dpMin(8, 2)
                                        color: Qt.rgba(1, 1, 1, 0.08) // Subtle glass effect
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.2)
                                        clip: true // Simple clipping for rounded corners
                                        
                                        // Album art image
                                        Image {
                                            id: albumArt
                                            anchors.fill: parent
                                            anchors.margins: dp(3)
                                            source: delegate.albumArtSource
                                            sourceSize.width: width * 1.5
                                            sourceSize.height: height * 1.5
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                            smooth: true
                                        }
                                        
                                        // Simple highlight effect for glass appearance
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: dp(3)
                                            height: parent.height * 0.3
                                            radius: dpMin(6, 2)
                                            color: "white"
                                            opacity: 0.1
                                        }
                                    }

                                    // Title and duration container
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        
                                        ColumnLayout {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: App.Spacing.overallMargin

                                            // Song title
                                            Text {
                                                Layout.fillWidth: true
                                                // Reference playlistRefreshCounter to force rebinding when mode changes
                                                text: {
                                                    var _ = mediaPlayer.playlistRefreshCounter
                                                    return mediaPlayer.isSpotifyPlaylist ? modelData : (mediaManager ? mediaManager.get_display_name(modelData) : modelData.replace('.mp3', ''))
                                                }
                                                color: delegate.isCurrentSong ? App.Style.accent : App.Style.primaryTextColor
                                                font.pixelSize: App.Spacing.mediaPlayerTextSize * 1.2
                                                font.family: mediaPlayer.globalFont
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }

                                            // Duration
                                            Text {
                                                text: {
                                                    // Reference playlistRefreshCounter to force rebinding
                                                    var _ = mediaPlayer.playlistRefreshCounter
                                                    if (mediaPlayer.isSpotifyPlaylist && spotifyManager) {
                                                        return spotifyManager.get_spotify_track_duration_formatted(modelData)
                                                    }
                                                    return mediaManager ? mediaManager.get_formatted_duration(modelData) : "0:00"
                                                }
                                                color: App.Style.secondaryTextColor
                                                font.pixelSize: App.Spacing.mediaPlayerSecondaryTextSize * 1.1
                                                font.family: mediaPlayer.globalFont
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                // Artist column
                                Item {
                                    Layout.preferredWidth: parent.width * 0.3
                                    Layout.fillHeight: true
                                    clip: true

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: {
                                            // Reference playlistRefreshCounter to force rebinding
                                            var _ = mediaPlayer.playlistRefreshCounter
                                            if (mediaPlayer.isSpotifyPlaylist && spotifyManager) {
                                                return spotifyManager.get_spotify_track_artist(modelData)
                                            }
                                            return mediaManager ? mediaManager.get_band(modelData) : "Unknown Artist"
                                        }
                                        color: App.Style.secondaryTextColor
                                        font.pixelSize: App.Spacing.mediaPlayerSecondaryTextSize * 1.2
                                        font.family: mediaPlayer.globalFont
                                        elide: Text.ElideRight
                                    }
                                }

                                // Album column
                                Item {
                                    Layout.preferredWidth: parent.width * 0.3
                                    Layout.fillHeight: true
                                    clip: true

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: {
                                            // Reference playlistRefreshCounter to force rebinding
                                            var _ = mediaPlayer.playlistRefreshCounter
                                            if (mediaPlayer.isSpotifyPlaylist && spotifyManager) {
                                                return spotifyManager.get_spotify_track_album(modelData)
                                            }
                                            return mediaManager ? mediaManager.get_album(modelData) : "Unknown Album"
                                        }
                                        color: App.Style.secondaryTextColor
                                        font.pixelSize: App.Spacing.mediaPlayerSecondaryTextSize * 1.2
                                        font.family: mediaPlayer.globalFont
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            
                            // Click + long-press behavior for list items
                            MouseArea {
                                anchors.fill: parent
                                pressAndHoldInterval: 600
                                onClicked: {
                                    var returnToLibrary = settingsManager ? settingsManager.returnToLibraryAfterSelection : false

                                    if (mediaPlayer.isSpotifyPlaylist) {
                                        if (spotifyManager) {
                                            if (mediaManager && mediaManager.is_playing()) {
                                                mediaManager.pause()
                                            }
                                            var uri = spotifyManager.get_spotify_track_uri(modelData)
                                            if (uri) {
                                                spotifyManager.play_uri(uri)
                                            }
                                            if (!returnToLibrary) {
                                                stackView.push("MediaRoom.qml", {
                                                    stackView: mediaPlayer.stackView
                                                })
                                            }
                                        }
                                    } else {
                                        if (mediaManager) {
                                            if (spotifyManager && spotifyManager.is_connected() && spotifyManager.is_playing()) {
                                                spotifyManager.pause()
                                            }
                                            if (settingsManager && settingsManager.mediaSource !== "local") {
                                                settingsManager.set_media_source("local")
                                            }
                                            mediaManager.play_file(modelData)
                                            lastPlayedSong = modelData
                                            if (!returnToLibrary) {
                                                stackView.push("MediaRoom.qml", {
                                                    stackView: mediaPlayer.stackView
                                                })
                                            }
                                        }
                                    }
                                }
                                onPressAndHold: {
                                    // Only allow actions for local files
                                    if (!mediaPlayer.isSpotifyPlaylist) {
                                        songActionMenu.songFilename = modelData
                                        songActionMenu.songDisplayName = mediaManager
                                            ? (mediaManager.get_display_name(modelData) || modelData)
                                            : modelData
                                        songActionMenu.open()
                                    }
                                }
                            }
                            
                            // Add subtle scaling effect on active song (no animation to prevent bounce on page load)
                            scale: 1.0
                            
                            // Shadow for depth (using Rectangle instead of effects)
                            Rectangle {
                                z: -1
                                anchors.fill: parent
                                anchors.margins: -dp(2)
                                radius: parent.radius + dp(2)
                                color: "black"
                                opacity: 0.08
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        id: verticalScrollBar
                        active: true
                        policy: ScrollBar.AsNeeded
                        
                        Component.onCompleted: {
                            if (background) background.implicitWidth = dp(12)
                        }
                    
                        palette.mid: App.Style.accent
                    }
                }
            }

            // Empty state message
            Text {
                anchors.centerIn: parent
                text: "No songs found in media folder"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.mediaPlayerTextSize * 1.3
                font.family: mediaPlayer.globalFont
                visible: mediaListView.count === 0
            }
        }
    }

    // Connect to mediaManager signals
    Connections {
        target: mediaManager

        // Media list updated
        function onMediaListChanged(files) {
            mediaFiles = files;

            if (currentSortColumn !== "none") {
                sortMediaFiles();
            }
            // Album art refresh after new files load
            updateTimer.restart();
        }

        // Current media changed
        function onCurrentMediaChanged(filename) {
            lastPlayedSong = filename
        }

        // Play state changed
        function onPlayStateChanged(isPlaying) {
            isPaused = !isPlaying
            if (mediaManager) {
                var currentFile = mediaManager.get_current_file()
                if (currentFile) {
                    lastPlayedSong = currentFile
                }
            }
        }

        // Statistics updates
        function onTotalDurationChanged(duration) {
            totalDurationText.text = duration
        }

        function onAlbumCountChanged(count) {
            albumCountText.text = count + " albums"
        }

        function onArtistCountChanged(count) {
            artistCountText.text = count + " artists"
        }

        // Playlist updates
        function onPlaylistsChanged() {
            console.log("Playlists changed")
            playlistNames = mediaManager.get_playlist_names()
        }

        function onCurrentPlaylistChanged(name) {
            console.log("Current playlist changed to: " + name)
            currentPlaylistName = name
        }
    }

    // Connect to spotifyManager signals
    Connections {
        target: spotifyManager

        function onPlaylistsChanged(playlists) {
            console.log("Spotify playlists changed: " + playlists.length + " playlists")
            spotifyPlaylistNames = spotifyManager.get_spotify_playlist_names()
        }

        function onSpotifyTracksChanged(tracks) {
            console.log("Spotify tracks changed: " + tracks.length + " tracks")
            console.log("isSpotifyPlaylist at signal time: " + mediaPlayer.isSpotifyPlaylist)
            spotifyTracks = tracks
            // Extract track names for the model
            var names = []
            for (var i = 0; i < tracks.length; i++) {
                names.push(tracks[i].name)
                if (i < 3) {
                    console.log("Track " + i + ": " + tracks[i].name + " by " + tracks[i].artist)
                }
            }
            spotifyTrackNames = names
            console.log("spotifyTrackNames length: " + spotifyTrackNames.length)
            updateTimer.restart()
            // Scroll to current track after model updates
            scrollToCurrentTimer.restart()
        }

        function onCurrentSpotifyPlaylistChanged(name) {
            console.log("Current Spotify playlist changed to: " + name)
            currentSpotifyPlaylistName = name
        }

        function onConnectionStateChanged(connected) {
            console.log("Spotify connection state changed: " + connected)
            if (connected) {
                spotifyPlaylistNames = spotifyManager.get_spotify_playlist_names()
            } else {
                spotifyPlaylistNames = []
                // If viewing Spotify playlist, reset to local
                if (isSpotifyPlaylist) {
                    isSpotifyPlaylist = false
                    spotifyTrackNames = []
                }
            }
        }

        function onCurrentTrackChanged(title, artist, album, artUrl) {
            // Update cached track name - delegates will automatically update via binding
            currentSpotifyTrackName = title
        }

        function onPlayStateChanged(playing) {
            // Update cached play state - delegates will automatically update via binding
            spotifyIsPlaying = playing
        }
    }

    // ─── Song Action Menu (long-press) ─────────────────────────────

    Popup {
        id: songActionMenu
        anchors.centerIn: parent
        width: dp(320)
        height: actionMenuContent.implicitHeight + dp(32)
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string songFilename: ""
        property string songDisplayName: ""

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: actionMenuContent
            anchors.fill: parent
            anchors.margins: dp(16)
            spacing: dp(12)

            // Song name header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: actionSongLabel.implicitHeight + dp(16)
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                radius: dp(6)

                Text {
                    id: actionSongLabel
                    anchors.centerIn: parent
                    width: parent.width - dp(16)
                    text: songActionMenu.songDisplayName
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(13)
                    font.weight: Font.DemiBold
                    color: App.Style.primaryTextColor
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Move to Playlist button
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight
                color: moveToMouse.pressed
                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                    : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                border.width: 1
                radius: dp(8)

                Text {
                    anchors.centerIn: parent
                    text: "Move to Playlist"
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(14)
                    font.weight: Font.DemiBold
                    color: App.Style.accent
                }

                MouseArea {
                    id: moveToMouse
                    anchors.fill: parent
                    onClicked: {
                        songActionMenu.close()
                        // Fetch available playlists, excluding current and "All Music"
                        var all = mediaManager.get_movable_playlist_names()
                        var filtered = []
                        for (var i = 0; i < all.length; i++) {
                            if (all[i] !== mediaPlayer.currentPlaylistName) {
                                filtered.push(all[i])
                            }
                        }
                        moveToPlaylistMenu.targetPlaylists = filtered
                        moveToPlaylistMenu.songFilename = songActionMenu.songFilename
                        moveToPlaylistMenu.songDisplayName = songActionMenu.songDisplayName
                        moveToPlaylistMenu.open()
                    }
                }
            }

            // Delete button
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight
                color: deleteActionMouse.pressed ? Qt.darker("#e53935", 1.3) : Qt.rgba(1, 0.227, 0.208, 0.12)
                border.color: Qt.rgba(1, 0.227, 0.208, 0.3)
                border.width: 1
                radius: dp(8)

                Text {
                    anchors.centerIn: parent
                    text: "Delete"
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(14)
                    font.weight: Font.DemiBold
                    color: "#e53935"
                }

                MouseArea {
                    id: deleteActionMouse
                    anchors.fill: parent
                    onClicked: {
                        songActionMenu.close()
                        deleteConfirmDialog.songFilename = songActionMenu.songFilename
                        deleteConfirmDialog.songDisplayName = songActionMenu.songDisplayName
                        deleteConfirmDialog.open()
                    }
                }
            }
        }
    }

    // ─── Move to Playlist Menu ─────────────────────────────────────

    Popup {
        id: moveToPlaylistMenu
        anchors.centerIn: parent
        width: dp(320)
        height: Math.min(moveMenuContent.implicitHeight + dp(32), dp(400))
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string songFilename: ""
        property string songDisplayName: ""
        property var targetPlaylists: []

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        contentItem: Flickable {
            clip: true
            contentHeight: moveMenuContent.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: moveMenuContent
                width: parent.width
                spacing: dp(6)

                Text {
                    text: "Move to..."
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(16)
                    font.weight: Font.Bold
                    color: App.Style.primaryTextColor
                    Layout.fillWidth: true
                    Layout.leftMargin: dp(4)
                }

                // Song name
                Text {
                    text: moveToPlaylistMenu.songDisplayName
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(11)
                    color: App.Style.secondaryTextColor
                    Layout.fillWidth: true
                    Layout.leftMargin: dp(4)
                    elide: Text.ElideRight
                }

                Repeater {
                    model: moveToPlaylistMenu.targetPlaylists

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight
                        color: moveItemMouse.pressed
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                            : moveItemMouse.containsMouse
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.1)
                                : "transparent"
                        radius: dp(6)
                        border.width: moveItemMouse.containsMouse ? 1 : 0
                        border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: App.Spacing.overallMargin * 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            font.family: mediaPlayer.globalFont
                            font.pixelSize: dp(13)
                            font.weight: Font.DemiBold
                            color: App.Style.primaryTextColor
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: moveItemMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                mediaManager.move_song_to_playlist(moveToPlaylistMenu.songFilename, modelData)
                                moveToPlaylistMenu.close()
                            }
                        }
                    }
                }

                // Empty state
                Text {
                    visible: moveToPlaylistMenu.targetPlaylists.length === 0
                    text: "No other playlists available.\nCreate one with the + button."
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(12)
                    color: App.Style.secondaryTextColor
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.topMargin: dp(8)
                }
            }

            ScrollBar.vertical: ScrollBar {
                active: true
                policy: ScrollBar.AsNeeded
            }
        }
    }

    // ─── Delete Confirmation Dialog ─────────────────────────────

    Popup {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        width: dp(320)
        height: deleteDialogContent.implicitHeight + dp(32)
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string songFilename: ""
        property string songDisplayName: ""

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: deleteDialogContent
            anchors.fill: parent
            anchors.margins: dp(16)
            spacing: dp(12)

            Text {
                text: "Delete Song"
                font.family: mediaPlayer.globalFont
                font.pixelSize: dp(18)
                font.weight: Font.Bold
                color: App.Style.primaryTextColor
                Layout.fillWidth: true
            }

            Text {
                text: "Are you sure you want to delete this file?"
                font.family: mediaPlayer.globalFont
                font.pixelSize: dp(13)
                color: App.Style.secondaryTextColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // Song name
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: songNameLabel.implicitHeight + dp(16)
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                radius: dp(6)

                Text {
                    id: songNameLabel
                    anchors.centerIn: parent
                    width: parent.width - dp(16)
                    text: deleteConfirmDialog.songDisplayName
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(13)
                    font.weight: Font.DemiBold
                    color: App.Style.primaryTextColor
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                text: "This cannot be undone."
                font.family: mediaPlayer.globalFont
                font.pixelSize: dp(11)
                color: Qt.rgba(1, 0.4, 0.4, 0.8)
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: dp(10)

                // Cancel
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(48)
                    color: cancelMouse.pressed
                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                    border.width: 1
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font.family: mediaPlayer.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: App.Style.primaryTextColor
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        onClicked: deleteConfirmDialog.close()
                    }
                }

                // Delete
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(48)
                    color: deleteMouse.pressed ? Qt.darker("#e53935", 1.3) : "#e53935"
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "Delete"
                        font.family: mediaPlayer.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: "#ffffff"
                    }

                    MouseArea {
                        id: deleteMouse
                        anchors.fill: parent
                        onClicked: {
                            if (mediaManager && deleteConfirmDialog.songFilename) {
                                mediaManager.delete_song(deleteConfirmDialog.songFilename)
                            }
                            deleteConfirmDialog.close()
                        }
                    }
                }
            }
        }
    }

    // ─── Delete Playlist Confirmation Dialog ─────────────────────

    Popup {
        id: deletePlaylistDialog
        anchors.centerIn: parent
        width: dp(320)
        height: deletePlaylistContent.implicitHeight + dp(32)
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string playlistToDelete: ""

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: deletePlaylistContent
            anchors.fill: parent
            anchors.margins: dp(16)
            spacing: dp(12)

            Text {
                text: "Delete Playlist"
                font.family: mediaPlayer.globalFont
                font.pixelSize: dp(18)
                font.weight: Font.Bold
                color: "#e53935"
                Layout.fillWidth: true
            }

            Text {
                text: "Delete \"" + deletePlaylistDialog.playlistToDelete + "\" and all songs in it?"
                font.family: mediaPlayer.globalFont
                font.pixelSize: dp(13)
                color: App.Style.secondaryTextColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: dp(10)

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(48)
                    color: delPlCancelMouse.pressed
                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                    border.width: 1
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font.family: mediaPlayer.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: App.Style.primaryTextColor
                    }

                    MouseArea {
                        id: delPlCancelMouse
                        anchors.fill: parent
                        onClicked: deletePlaylistDialog.close()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(48)
                    color: delPlConfirmMouse.pressed ? Qt.darker("#e53935", 1.3) : "#e53935"
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "Delete"
                        font.family: mediaPlayer.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: "#ffffff"
                    }

                    MouseArea {
                        id: delPlConfirmMouse
                        anchors.fill: parent
                        onClicked: {
                            if (deletePlaylistDialog.playlistToDelete) {
                                mediaManager.delete_playlist(deletePlaylistDialog.playlistToDelete)
                            }
                            deletePlaylistDialog.close()
                        }
                    }
                }
            }
        }
    }

    // ─── New Playlist Dialog ─────────────────────────────────────

    Popup {
        id: newPlaylistDialog
        property bool npShowKeyboard: false

        // Compact: centered dialog.  Keyboard open: wide + tall, still centered.
        property real npCompactW: dp(420)
        property real npCompactH: newPlaylistContent.implicitHeight + dp(48)
        property real npExpandedW: Math.min(mediaPlayer.width * 0.92, dp(700))
        property real npExpandedH: mediaPlayer.height * 0.82

        width:  npShowKeyboard ? npExpandedW  : npCompactW
        height: npShowKeyboard ? npExpandedH  : npCompactH
        x: (parent.width  - width)  / 2
        y: (parent.height - height) / 2

        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: newPlaylistInput.forceActiveFocus()
        onClosed: { npShowKeyboard = false; newPlaylistInput.text = "" }

        Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: App.Style.backgroundColor
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: newPlaylistContent
            anchors.fill: parent
            anchors.margins: dp(24)
            spacing: dp(14)

            // Top portion (title + input + buttons)
            ColumnLayout {
                id: npTopContent
                Layout.fillWidth: true
                spacing: dp(14)

                Text {
                    text: "New Playlist"
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(22)
                    font.weight: Font.Bold
                    color: App.Style.primaryTextColor
                    Layout.fillWidth: true
                }

                Text {
                    text: "Enter a name for the new playlist folder."
                    font.family: mediaPlayer.globalFont
                    font.pixelSize: dp(14)
                    color: App.Style.secondaryTextColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: !newPlaylistDialog.npShowKeyboard
                }

                // Input row with keyboard toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: dp(8)

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(48)
                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                        border.color: newPlaylistInput.activeFocus ? App.Style.accent : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                        border.width: 1
                        radius: dp(8)

                        TextField {
                            id: newPlaylistInput
                            anchors.fill: parent
                            anchors.leftMargin: dp(12)
                            anchors.rightMargin: dp(12)
                            placeholderText: "Playlist name..."
                            placeholderTextColor: App.Style.secondaryTextColor
                            font.family: mediaPlayer.globalFont
                            font.pixelSize: dp(16)
                            color: App.Style.primaryTextColor
                            background: Item {}
                            selectByMouse: true
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    var pName = text.trim()
                                    mediaManager.create_playlist(pName)
                                    mediaManager.select_playlist(pName)
                                    newPlaylistDialog.close()
                                }
                            }
                        }
                    }

                    // Keyboard toggle
                    Rectangle {
                        Layout.preferredWidth: dp(48)
                        Layout.preferredHeight: dp(48)
                        color: newPlaylistDialog.npShowKeyboard
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                            : npKbToggleMouse.containsMouse
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.12)
                                : "transparent"
                        radius: dp(6)
                        border.width: 1
                        border.color: newPlaylistDialog.npShowKeyboard ? App.Style.accent : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)

                        Text {
                            anchors.centerIn: parent
                            text: "\u2328"
                            font.pixelSize: dp(20)
                            color: newPlaylistDialog.npShowKeyboard ? App.Style.accent : App.Style.secondaryTextColor
                        }

                        MouseArea {
                            id: npKbToggleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                newPlaylistDialog.npShowKeyboard = !newPlaylistDialog.npShowKeyboard
                                newPlaylistInput.forceActiveFocus()
                            }
                        }
                    }
                }

                // Cancel / Create buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: dp(4)
                    spacing: dp(12)

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(52)
                        color: cancelNewMouse.pressed
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                        border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                        border.width: 1
                        radius: dp(8)

                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            font.family: mediaPlayer.globalFont
                            font.pixelSize: dp(15)
                            font.weight: Font.DemiBold
                            color: App.Style.primaryTextColor
                        }

                        MouseArea {
                            id: cancelNewMouse
                            anchors.fill: parent
                            onClicked: newPlaylistDialog.close()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(52)
                        color: createMouse.pressed ? Qt.darker(App.Style.accent, 1.3) : App.Style.accent
                        radius: dp(8)
                        opacity: newPlaylistInput.text.trim().length > 0 ? 1.0 : 0.4

                        Text {
                            anchors.centerIn: parent
                            text: "Create"
                            font.family: mediaPlayer.globalFont
                            font.pixelSize: dp(15)
                            font.weight: Font.DemiBold
                            color: "#000000"
                        }

                        MouseArea {
                            id: createMouse
                            anchors.fill: parent
                            enabled: newPlaylistInput.text.trim().length > 0
                            onClicked: {
                                var pName = newPlaylistInput.text.trim()
                                mediaManager.create_playlist(pName)
                                mediaManager.select_playlist(pName)
                                newPlaylistDialog.close()
                            }
                        }
                    }
                }
            }

            // ─── Inline On-Screen Keyboard ─────────────────────
            Rectangle {
                id: npKeyboard
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: newPlaylistDialog.npShowKeyboard
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.04)
                border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                border.width: 1
                radius: dp(8)

                property var rows: [
                    ["Q","W","E","R","T","Y","U","I","O","P"],
                    ["A","S","D","F","G","H","J","K","L"],
                    ["Z","X","C","V","B","N","M"]
                ]

                // Dynamic sizing: 4 rows (3 letter + 1 bottom), 10 keys wide
                property real kbMargin: dp(6)
                property real kbSpacing: dp(3)
                property real kbRowCount: 4
                property real kbColCount: 10
                property real keyW: Math.floor((width - kbMargin * 2 - kbSpacing * (kbColCount - 1)) / kbColCount)
                property real keyH: Math.floor((height - kbMargin * 2 - kbSpacing * (kbRowCount - 1)) / kbRowCount)
                property real keyFont: Math.max(dp(11), Math.min(keyH * 0.45, dp(28)))

                Column {
                    anchors.fill: parent
                    anchors.margins: npKeyboard.kbMargin
                    spacing: npKeyboard.kbSpacing

                    Repeater {
                        model: npKeyboard.rows

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: npKeyboard.kbSpacing

                            Repeater {
                                model: modelData

                                Rectangle {
                                    width: npKeyboard.keyW
                                    height: npKeyboard.keyH
                                    radius: dp(5)
                                    color: npKeyMouse.pressed
                                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                                    border.width: 1
                                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.family: mediaPlayer.globalFont
                                        font.pixelSize: npKeyboard.keyFont
                                        font.weight: Font.DemiBold
                                        color: App.Style.primaryTextColor
                                    }

                                    MouseArea {
                                        id: npKeyMouse
                                        anchors.fill: parent
                                        onClicked: {
                                            newPlaylistInput.text += modelData.toLowerCase()
                                            newPlaylistInput.forceActiveFocus()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Bottom row: backspace, space, clear
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: npKeyboard.kbSpacing

                        Rectangle {
                            width: npKeyboard.keyW * 2 + npKeyboard.kbSpacing
                            height: npKeyboard.keyH
                            radius: dp(5)
                            color: npBkspMouse.pressed
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "\u232B"
                                font.pixelSize: npKeyboard.keyFont * 1.1
                                color: App.Style.primaryTextColor
                            }

                            MouseArea {
                                id: npBkspMouse
                                anchors.fill: parent
                                onClicked: {
                                    if (newPlaylistInput.text.length > 0)
                                        newPlaylistInput.text = newPlaylistInput.text.slice(0, -1)
                                    newPlaylistInput.forceActiveFocus()
                                }
                            }
                        }

                        Rectangle {
                            width: npKeyboard.keyW * 5 + npKeyboard.kbSpacing * 4
                            height: npKeyboard.keyH
                            radius: dp(5)
                            color: npSpaceMouse.pressed
                                ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                                : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "SPACE"
                                font.family: mediaPlayer.globalFont
                                font.pixelSize: npKeyboard.keyFont * 0.85
                                font.weight: Font.DemiBold
                                color: App.Style.secondaryTextColor
                            }

                            MouseArea {
                                id: npSpaceMouse
                                anchors.fill: parent
                                onClicked: {
                                    newPlaylistInput.text += " "
                                    newPlaylistInput.forceActiveFocus()
                                }
                            }
                        }

                        Rectangle {
                            width: npKeyboard.keyW * 2 + npKeyboard.kbSpacing
                            height: npKeyboard.keyH
                            radius: dp(5)
                            color: npClearMouse.pressed
                                ? Qt.rgba(1, 0.227, 0.208, 0.2)
                                : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "CLR"
                                font.family: mediaPlayer.globalFont
                                font.pixelSize: npKeyboard.keyFont * 0.85
                                font.weight: Font.DemiBold
                                color: "#e53935"
                            }

                            MouseArea {
                                id: npClearMouse
                                anchors.fill: parent
                                onClicked: {
                                    newPlaylistInput.text = ""
                                    newPlaylistInput.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}