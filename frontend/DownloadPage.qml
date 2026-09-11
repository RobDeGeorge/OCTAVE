import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: downloadPage
    objectName: "downloadPage"
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    property StackView stackView
    property var mainWindow

    property string globalFont: App.Style.fontFamily
    property color bgColor: App.Style.backgroundColor
    property color accentColor: App.Style.accent
    property color textColor: App.Style.primaryTextColor
    property color dimTextColor: App.Style.secondaryTextColor
    property color cardColor: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
    property color cardBorderColor: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
    property color successColor: App.Style.statusSuccess

    // On-screen keyboard state
    property bool showKeyboard: false

    // Search state
    property var searchResults: []
    property bool isSearching: false
    property string statusText: "Search for songs to download"
    property bool statusVisible: true

    // Start a download, first making sure the app may write to the target
    // folder. Only the C++ backend exposes hasStorageAccess()/
    // requestStorageAccess() (Android "All files access"); on Python and on
    // desktop C++ hasStorageAccess() is always true.
    function startDownload(songJson) {
        if (isAndroid && typeof downloadManager.hasStorageAccess === "function"
                && !downloadManager.hasStorageAccess()) {
            downloadManager.requestStorageAccess()
            downloadPage.statusText = "Grant \"All files access\" in the settings screen, then tap download again"
            return
        }
        downloadManager.download_song(songJson)
    }

    onStatusTextChanged: {
        downloadPage.statusVisible = true
        statusFadeTimer.restart()
    }

    Timer {
        id: statusFadeTimer
        interval: 10000
        repeat: false
        running: true
        onTriggered: downloadPage.statusVisible = false
    }

    // Download state — keyed by song_id for uniqueness
    property var activeDownloads: ({})    // song_id -> progress (0.0-1.0)
    property var downloadNames: ({})      // song_id -> display name
    property var downloadedPaths: ({})    // song_id -> absolute file path (captured from backend)

    // Queue panel is always visible when items exist (no toggle needed)

    // Playlist selector state
    property var downloadPlaylists: []
    property string selectedPlaylist: ""

    // Download queue data model
    ListModel { id: queueModel }

    Component.onCompleted: {
        // Load available playlists for the download target selector
        if (mediaManager) {
            downloadPlaylists = mediaManager.get_movable_playlist_names()
        }
        if (downloadManager) {
            selectedPlaylist = downloadManager.get_download_playlist()
        }
    }

    Connections {
        target: mediaManager
        function onPlaylistsChanged() {
            var newList = mediaManager.get_movable_playlist_names()
            downloadPlaylists = newList
            // Clear stale selection if the playlist was deleted or renamed
            if (selectedPlaylist && newList.indexOf(selectedPlaylist) === -1) {
                selectedPlaylist = downloadManager ? downloadManager.get_download_playlist() : ""
            }
        }
    }

    // ─── Connections to DownloadManager ──────────────────────────────

    Connections {
        target: downloadManager

        function onSearchResults(jsonStr) {
            try {
                var results = JSON.parse(jsonStr)
                downloadPage.searchResults = results
            } catch (e) {
                downloadPage.searchResults = []
            }
        }

        function onSearchInProgress(inProgress) {
            downloadPage.isSearching = inProgress
        }

        function onSearchError(msg) {
            downloadPage.statusText = "Search error: " + msg
        }

        function onDownloadStarted(songId, songName) {
            var downloads = Object.assign({}, downloadPage.activeDownloads)
            downloads[songId] = 0.0
            downloadPage.activeDownloads = downloads
            var names = Object.assign({}, downloadPage.downloadNames)
            names[songId] = songName
            downloadPage.downloadNames = names

            // Add to queue model
            queueModel.append({
                songId: songId,
                songName: songName,
                status: "downloading",
                progress: 0.0,
                errorMsg: ""
            })
        }

        function onDownloadProgress(songId, progress) {
            var downloads = Object.assign({}, downloadPage.activeDownloads)
            downloads[songId] = progress
            downloadPage.activeDownloads = downloads

            // Update queue model
            for (var i = 0; i < queueModel.count; i++) {
                if (queueModel.get(i).songId === songId) {
                    queueModel.setProperty(i, "progress", progress)
                    break
                }
            }
        }

        function onDownloadComplete(songId, filePath) {
            var displayName = downloadPage.downloadNames[songId] || ""
            var downloads = Object.assign({}, downloadPage.activeDownloads)
            delete downloads[songId]
            downloadPage.activeDownloads = downloads
            var names = Object.assign({}, downloadPage.downloadNames)
            delete names[songId]
            downloadPage.downloadNames = names
            // Remember the exact file path so click-to-play can use it directly
            var paths = Object.assign({}, downloadPage.downloadedPaths)
            paths[songId] = filePath
            downloadPage.downloadedPaths = paths
            downloadPage.statusText = displayName ? ("Downloaded: " + displayName) : "Download complete"

            // Update queue model
            for (var i = 0; i < queueModel.count; i++) {
                if (queueModel.get(i).songId === songId) {
                    queueModel.setProperty(i, "status", "complete")
                    queueModel.setProperty(i, "progress", 1.0)
                    break
                }
            }
        }

        function onDownloadError(songId, songName, errorMsg) {
            var displayName = downloadPage.downloadNames[songId] || songName || "download"
            var downloads = Object.assign({}, downloadPage.activeDownloads)
            delete downloads[songId]
            downloadPage.activeDownloads = downloads
            var names = Object.assign({}, downloadPage.downloadNames)
            delete names[songId]
            downloadPage.downloadNames = names
            downloadPage.statusText = "Error: " + displayName + " - " + errorMsg

            // Update queue model
            for (var i = 0; i < queueModel.count; i++) {
                if (queueModel.get(i).songId === songId) {
                    queueModel.setProperty(i, "status", "failed")
                    queueModel.setProperty(i, "errorMsg", errorMsg)
                    break
                }
            }
        }

        function onDownloadStatusChanged(songId, field, valueJson) {
            // Patch searchResults and re-assign, but save/restore scroll position
            // so the ListView doesn't jump to the top
            var val = JSON.parse(valueJson)
            var results = downloadPage.searchResults
            for (var i = 0; i < results.length; i++) {
                if (results[i].song_id === songId) {
                    results[i][field] = val
                    break
                }
            }
            var savedY = resultsListView.contentY
            downloadPage.searchResults = results.slice()  // trigger model update
            resultsListView.contentY = savedY
        }

        function onStatusMessage(msg) {
            downloadPage.statusText = msg
        }
    }

    // ─── Main Layout ─────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: bgColor

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: App.Spacing.mediaRoomMargin
            anchors.rightMargin: App.Spacing.mediaRoomMargin
            anchors.bottomMargin: dp(16)
            anchors.leftMargin: dp(16)
            spacing: dp(12)

            // ─── Header ──────────────────────────────────────────

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight
                spacing: App.Spacing.overallMargin * 2

                Text {
                    text: "Download Music"
                    font.family: downloadPage.globalFont
                    font.pixelSize: dp(24)
                    font.weight: Font.Bold
                    color: textColor
                }

                Item { Layout.fillWidth: true }

                // Status text
                Text {
                    text: downloadPage.statusText
                    font.family: downloadPage.globalFont
                    font.pixelSize: dp(11)
                    color: dimTextColor
                    elide: Text.ElideRight
                    Layout.maximumWidth: dp(300)
                    opacity: downloadPage.statusVisible ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation { duration: 600; easing.type: Easing.InOutQuad }
                    }
                }

                // New playlist button (left of dropdown, matches MediaPlayer)
                Rectangle {
                    Layout.preferredWidth: App.Spacing.bottomBarNavButtonHeight
                    Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight
                    color: dlNewPlaylistMouse.pressed
                        ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                        : "transparent"
                    radius: dpMin(8, 2)
                    border.width: 1
                    border.color: accentColor

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: accentColor
                        font.pixelSize: dp(22)
                        font.weight: Font.Bold
                        font.family: downloadPage.globalFont
                    }

                    MouseArea {
                        id: dlNewPlaylistMouse
                        width: parent.width * 2
                        height: parent.height * 2
                        anchors.centerIn: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dlNewPlaylistDialog.open()
                    }
                }

                // Playlist dropdown (matches MediaPlayer style)
                Item {
                    id: dlPlaylistDropdownContainer
                    Layout.preferredWidth: Math.min(dp(220), downloadPage.width * 0.25)
                    Layout.preferredHeight: App.Spacing.bottomBarNavButtonHeight

                    property bool dlDropdownCooldown: false
                    Timer {
                        id: dlDropdownCooldownTimer
                        interval: 100
                        onTriggered: dlPlaylistDropdownContainer.dlDropdownCooldown = false
                    }

                    Rectangle {
                        id: dlPlaylistDropdown
                        anchors.fill: parent
                        color: dlDropdownMouse.containsMouse
                            ? Qt.lighter(App.Style.hoverColor, 1.2)
                            : "transparent"
                        radius: dpMin(8, 2)
                        border.width: 1
                        border.color: accentColor

                        Behavior on color {
                            ColorAnimation { duration: 150 }
                        }

                        // Label
                        Text {
                            id: dlDropdownLabel
                            anchors.top: parent.top
                            anchors.topMargin: dp(4)
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "SAVE TO"
                            color: accentColor
                            font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 0.85
                            font.bold: true
                            font.family: downloadPage.globalFont
                            font.letterSpacing: dp(1)
                        }

                        // Selected playlist name
                        Text {
                            anchors.left: parent.left
                            anchors.right: dlDropdownArrow.left
                            anchors.top: dlDropdownLabel.bottom
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: App.Spacing.overallMargin * 2
                            verticalAlignment: Text.AlignVCenter
                            text: downloadPage.selectedPlaylist || "Downloads"
                            color: textColor
                            font.pixelSize: App.Spacing.mediaPlayerStatsTextSize * 1.4
                            font.bold: true
                            font.family: downloadPage.globalFont
                            elide: Text.ElideRight
                        }

                        // Arrow
                        Text {
                            id: dlDropdownArrow
                            anchors.right: parent.right
                            anchors.rightMargin: App.Spacing.overallMargin * 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: dlPlaylistPopup.visible ? "\u25B2" : "\u25BC"
                            color: accentColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: downloadPage.globalFont
                        }

                        MouseArea {
                            id: dlDropdownMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (dlPlaylistDropdownContainer.dlDropdownCooldown) return
                                if (dlPlaylistPopup.visible) {
                                    dlPlaylistPopup.close()
                                } else {
                                    dlPlaylistPopup.open()
                                }
                            }
                        }
                    }

                    // Popup menu (matches MediaPlayer style)
                    Popup {
                        id: dlPlaylistPopup
                        parent: dlPlaylistDropdownContainer
                        y: dlPlaylistDropdown.height + dp(4)
                        width: dlPlaylistDropdown.width
                        height: Math.min(dlPlaylistColumn.implicitHeight + dp(20), dp(400))
                        padding: dp(10)
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        onClosed: {
                            dlPlaylistDropdownContainer.dlDropdownCooldown = true
                            dlDropdownCooldownTimer.restart()
                        }

                        background: Rectangle {
                            color: bgColor
                            border.color: accentColor
                            border.width: 1
                            radius: dpMin(8, 2)
                        }

                        contentItem: Flickable {
                            clip: true
                            contentHeight: dlPlaylistColumn.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: dlPlaylistColumn
                                width: parent.width
                                spacing: dp(4)

                                Repeater {
                                    model: downloadPage.downloadPlaylists

                                    Rectangle {
                                        width: dlPlaylistColumn.width
                                        height: App.Spacing.bottomBarNavButtonHeight
                                        color: {
                                            var isActive = modelData === downloadPage.selectedPlaylist
                                            if (isActive) return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.25)
                                            if (dlItemMouse.containsMouse) return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                            return "transparent"
                                        }
                                        radius: dpMin(6, 2)
                                        border.width: modelData === downloadPage.selectedPlaylist ? 1 : 0
                                        border.color: accentColor

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: App.Spacing.overallMargin * 2
                                            anchors.rightMargin: App.Spacing.overallMargin * 2
                                            spacing: App.Spacing.overallMargin * 2

                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData
                                                color: textColor
                                                font.pixelSize: App.Spacing.overallText * 1.1
                                                font.bold: modelData === downloadPage.selectedPlaylist
                                                font.family: downloadPage.globalFont
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            id: dlItemMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                downloadPage.selectedPlaylist = modelData
                                                downloadManager.set_download_playlist(modelData)
                                                dlPlaylistPopup.close()
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

            // ─── Search Bar ──────────────────────────────────────

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: dp(56)
                Layout.maximumHeight: dp(56)
                Layout.minimumHeight: dp(56)
                spacing: dp(10)

                // Search icon — lives outside the bar, to its left
                Image {
                    id: searchIconImg
                    Layout.preferredWidth: dp(22)
                    Layout.preferredHeight: dp(22)
                    Layout.alignment: Qt.AlignVCenter
                    source: "./assets/search_icon.svg"
                    sourceSize: Qt.size(dp(44), dp(44))
                    fillMode: Image.PreserveAspectFit
                    visible: false
                }
                ColorOverlay {
                    Layout.preferredWidth: dp(22)
                    Layout.preferredHeight: dp(22)
                    Layout.alignment: Qt.AlignVCenter
                    source: searchIconImg
                    color: dimTextColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: cardColor
                    border.color: searchInput.activeFocus ? accentColor : cardBorderColor
                    border.width: 1
                    radius: dp(8)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: dp(8)
                        anchors.rightMargin: dp(8)
                        spacing: dp(8)

                    // Keyboard toggle button (left side of search input)
                    Rectangle {
                        Layout.preferredWidth: dp(36)
                        Layout.preferredHeight: dp(36)
                        color: downloadPage.showKeyboard
                            ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.25)
                            : kbToggleMouse.containsMouse
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.12)
                                : "transparent"
                        radius: dp(6)
                        border.width: 1
                        border.color: downloadPage.showKeyboard ? accentColor : cardBorderColor

                        Text {
                            anchors.centerIn: parent
                            text: "\u2328"
                            font.pixelSize: dp(20)
                            color: downloadPage.showKeyboard ? accentColor : dimTextColor
                        }

                        MouseArea {
                            id: kbToggleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                downloadPage.showKeyboard = !downloadPage.showKeyboard
                                if (downloadPage.showKeyboard) searchInput.forceActiveFocus()
                            }
                        }
                    }

                    TextField {
                        id: searchInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Song name, artist"
                        placeholderTextColor: dimTextColor
                        font.family: downloadPage.globalFont
                        font.pixelSize: dp(14)
                        color: textColor
                        background: Item {}
                        selectByMouse: true

                        onAccepted: {
                            if (text.trim().length > 0) {
                                downloadManager.search(text.trim())
                                downloadPage.showKeyboard = false
                            }
                        }
                    }

                    // Search button
                    Rectangle {
                        Layout.preferredWidth: dp(70)
                        Layout.preferredHeight: dp(32)
                        color: searchMouseArea.pressed ? Qt.darker(accentColor, 1.3) : accentColor
                        radius: dp(6)
                        opacity: downloadPage.isSearching ? 0.5 : 1.0

                        Text {
                            anchors.centerIn: parent
                            text: downloadPage.isSearching ? "..." : "Search"
                            font.family: downloadPage.globalFont
                            font.pixelSize: dp(12)
                            font.weight: Font.DemiBold
                            color: "#000000"
                        }

                        MouseArea {
                            id: searchMouseArea
                            anchors.fill: parent
                            enabled: !downloadPage.isSearching
                            onClicked: {
                                if (searchInput.text.trim().length > 0) {
                                    downloadManager.search(searchInput.text.trim())
                                    downloadPage.showKeyboard = false
                                }
                            }
                        }
                    }
                }
                }
            }

            // ─── On-Screen Keyboard ───────────────────────────────
            Rectangle {
                id: onScreenKeyboard
                Layout.fillWidth: true
                Layout.fillHeight: downloadPage.showKeyboard
                visible: downloadPage.showKeyboard
                color: cardColor
                border.color: cardBorderColor
                border.width: 1
                radius: dp(8)

                property var rows: [
                    ["1","2","3","4","5","6","7","8","9","0"],
                    ["Q","W","E","R","T","Y","U","I","O","P"],
                    ["A","S","D","F","G","H","J","K","L"],
                    ["Z","X","C","V","B","N","M"]
                ]

                // Dynamic key sizing: 5 rows (4 letter + 1 bottom), 10 keys wide
                property real kbMargin: dp(8)
                property real kbSpacing: dp(4)
                property real kbRowCount: 5
                property real kbColCount: 10
                property real keyWidth: Math.floor((width - kbMargin * 2 - kbSpacing * (kbColCount - 1)) / kbColCount)
                property real keyHeight: Math.floor((height - kbMargin * 2 - kbSpacing * (kbRowCount - 1)) / kbRowCount)
                property real keyFontSize: Math.max(dp(12), Math.min(keyHeight * 0.45, dp(32)))

                Column {
                    id: kbGrid
                    anchors.fill: parent
                    anchors.margins: onScreenKeyboard.kbMargin
                    spacing: onScreenKeyboard.kbSpacing

                    Repeater {
                        model: onScreenKeyboard.rows

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: onScreenKeyboard.kbSpacing

                            Repeater {
                                model: modelData

                                Rectangle {
                                    width: onScreenKeyboard.keyWidth
                                    height: onScreenKeyboard.keyHeight
                                    radius: dp(6)
                                    color: kbKeyMouse.pressed
                                        ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                        : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                                    border.width: 1
                                    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: onScreenKeyboard.keyFontSize
                                        font.weight: Font.DemiBold
                                        color: textColor
                                    }

                                    MouseArea {
                                        id: kbKeyMouse
                                        anchors.fill: parent
                                        onClicked: {
                                            searchInput.text += modelData.toLowerCase()
                                            searchInput.forceActiveFocus()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Bottom row: backspace, space, clear, search
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: onScreenKeyboard.kbSpacing

                        // Backspace
                        Rectangle {
                            width: onScreenKeyboard.keyWidth * 1.5 + onScreenKeyboard.kbSpacing * 0.5
                            height: onScreenKeyboard.keyHeight
                            radius: dp(6)
                            color: bkspMouse.pressed
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "\u232B"
                                font.pixelSize: onScreenKeyboard.keyFontSize * 1.2
                                color: textColor
                            }

                            MouseArea {
                                id: bkspMouse
                                anchors.fill: parent
                                onClicked: {
                                    if (searchInput.text.length > 0) {
                                        searchInput.text = searchInput.text.slice(0, -1)
                                    }
                                    searchInput.forceActiveFocus()
                                }
                            }
                        }

                        // Space bar
                        Rectangle {
                            width: onScreenKeyboard.keyWidth * 4.5 + onScreenKeyboard.kbSpacing * 3.5
                            height: onScreenKeyboard.keyHeight
                            radius: dp(6)
                            color: spaceMouse.pressed
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "SPACE"
                                font.family: downloadPage.globalFont
                                font.pixelSize: onScreenKeyboard.keyFontSize * 0.85
                                font.weight: Font.DemiBold
                                color: dimTextColor
                            }

                            MouseArea {
                                id: spaceMouse
                                anchors.fill: parent
                                onClicked: {
                                    searchInput.text += " "
                                    searchInput.forceActiveFocus()
                                }
                            }
                        }

                        // Clear
                        Rectangle {
                            width: onScreenKeyboard.keyWidth * 1.5 + onScreenKeyboard.kbSpacing * 0.5
                            height: onScreenKeyboard.keyHeight
                            radius: dp(6)
                            color: clearMouse.pressed
                                ? Qt.rgba(1, 0.227, 0.208, 0.2)
                                : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)

                            Text {
                                anchors.centerIn: parent
                                text: "CLR"
                                font.family: downloadPage.globalFont
                                font.pixelSize: onScreenKeyboard.keyFontSize * 0.85
                                font.weight: Font.DemiBold
                                color: "#e53935"
                            }

                            MouseArea {
                                id: clearMouse
                                anchors.fill: parent
                                onClicked: {
                                    searchInput.text = ""
                                    searchInput.forceActiveFocus()
                                }
                            }
                        }

                        // Search from keyboard
                        Rectangle {
                            width: onScreenKeyboard.keyWidth * 2 + onScreenKeyboard.kbSpacing
                            height: onScreenKeyboard.keyHeight
                            radius: dp(6)
                            color: kbSearchMouse.pressed ? Qt.darker(accentColor, 1.3) : accentColor
                            opacity: searchInput.text.trim().length > 0 ? 1.0 : 0.4

                            Text {
                                anchors.centerIn: parent
                                text: "SEARCH"
                                font.family: downloadPage.globalFont
                                font.pixelSize: onScreenKeyboard.keyFontSize * 0.85
                                font.weight: Font.Bold
                                color: "#000000"
                            }

                            MouseArea {
                                id: kbSearchMouse
                                anchors.fill: parent
                                enabled: searchInput.text.trim().length > 0 && !downloadPage.isSearching
                                onClicked: {
                                    downloadManager.search(searchInput.text.trim())
                                    downloadPage.showKeyboard = false
                                }
                            }
                        }
                    }
                }
            }

            // ─── Results + Queue Side-by-Side ─────────────────────

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: !downloadPage.showKeyboard
                visible: !downloadPage.showKeyboard
                spacing: dp(10)

                // ─── Results List (3/4 width when queue visible) ──────
                ListView {
                    id: resultsListView
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    clip: true
                    spacing: dp(6)
                    model: downloadPage.searchResults

                    ScrollBar.vertical: ScrollBar {
                        active: true
                        policy: ScrollBar.AsNeeded
                    }

                delegate: Rectangle {
                    id: songDelegate
                    property color failedColor: App.Style.statusDanger
                    property string songId: modelData.song_id || ""
                    property bool isDownloading: songId !== "" && downloadPage.activeDownloads.hasOwnProperty(songId)
                    property real dlProgress: isDownloading ? (downloadPage.activeDownloads[songId] || 0) : 0
                    onDlProgressChanged: if (progressRing.visible) progressRing.requestPaint()

                    width: resultsListView.width - dp(14)
                    height: dp(72)
                    color: cardHover.hovered ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.12) : cardColor

                    function handleCardAction() {
                        if (modelData.is_downloaded) {
                            var artist = modelData.artist || ""
                            var name = modelData.name || ""
                            var playlist = modelData.downloaded_playlist || ""
                            var knownPath = downloadPage.downloadedPaths[modelData.song_id || ""] || ""
                            downloadPage.statusText = "Playing: " + artist + " - " + name
                            if (!mediaManager) return
                            if (spotifyManager && spotifyManager.is_connected() && spotifyManager.is_playing()) {
                                spotifyManager.pause()
                            }
                            if (settingsManager && settingsManager.mediaSource !== "local") {
                                settingsManager.set_media_source("local")
                            }
                            var ok = false
                            if (knownPath) {
                                ok = mediaManager.play_file_at_path(knownPath)
                            }
                            if (!ok && artist && name && playlist) {
                                ok = mediaManager.play_downloaded_song(artist, name, playlist)
                            }
                            if (!ok) {
                                downloadPage.statusText = "Could not find file for: " + artist + " - " + name
                                return
                            }
                            if (downloadPage.stackView) {
                                downloadPage.stackView.push("MediaRoom.qml", {
                                    stackView: downloadPage.stackView
                                })
                            }
                        } else if (modelData.is_failed) {
                            errorPopup.errorMsg = modelData.error_message || "Download failed — no audio source found"
                            errorPopup.songName = (modelData.artist || "") + " - " + (modelData.name || "")
                            errorPopup.songJson = JSON.stringify(modelData)
                            errorPopup.open()
                        } else {
                            downloadPage.startDownload(JSON.stringify(modelData))
                        }
                    }

                    HoverHandler {
                        id: cardHover
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        cursorShape: songDelegate.isDownloading ? Qt.ArrowCursor : Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: !songDelegate.isDownloading
                        onTapped: songDelegate.handleCardAction()
                    }
                    border.color: {
                        if (modelData.is_downloaded) return Qt.rgba(successColor.r, successColor.g, successColor.b, 0.3)
                        if (modelData.is_failed) return Qt.rgba(failedColor.r, failedColor.g, failedColor.b, 0.3)
                        return cardBorderColor
                    }
                    border.width: 1
                    radius: dp(8)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: dp(10)
                        spacing: dp(12)

                        // Album art thumbnail
                        Rectangle {
                            Layout.preferredWidth: dp(52)
                            Layout.preferredHeight: dp(52)
                            radius: dp(6)
                            color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                            clip: true

                            Image {
                                id: albumArtImg
                                anchors.fill: parent
                                source: modelData.cover_url || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: status === Image.Ready
                            }

                            // Fallback icon
                            Text {
                                anchors.centerIn: parent
                                text: "\u266A"
                                font.pixelSize: dp(20)
                                color: dimTextColor
                                visible: albumArtImg.status !== Image.Ready
                            }
                        }

                        // Song info
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: dp(2)

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name || "Unknown"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dp(14)
                                font.weight: Font.DemiBold
                                color: textColor
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.artist || "Unknown Artist"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dp(12)
                                color: dimTextColor
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                spacing: dp(8)

                                Text {
                                    text: modelData.album_name || ""
                                    font.family: downloadPage.globalFont
                                    font.pixelSize: dp(10)
                                    color: dimTextColor
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: dp(200)
                                }

                                Text {
                                    text: {
                                        var dur = modelData.duration || 0
                                        var m = Math.floor(dur / 60)
                                        var s = dur % 60
                                        return m + ":" + (s < 10 ? "0" : "") + s
                                    }
                                    font.family: downloadPage.globalFont
                                    font.pixelSize: dp(10)
                                    color: dimTextColor
                                }

                                Rectangle {
                                    visible: modelData.explicit === true
                                    width: explicitLabel.implicitWidth + dp(6)
                                    height: explicitLabel.implicitHeight + dp(4)
                                    color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.6)
                                    radius: 2

                                    Text {
                                        id: explicitLabel
                                        anchors.centerIn: parent
                                        text: "E"
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: dp(9)
                                        font.weight: Font.Bold
                                        color: "#000"
                                    }
                                }

                                // Playlist badge for downloaded songs
                                Rectangle {
                                    visible: modelData.is_downloaded === true && (modelData.downloaded_playlist || "") !== ""
                                    width: playlistBadgeText.implicitWidth + dp(10)
                                    height: playlistBadgeText.implicitHeight + dp(4)
                                    color: Qt.rgba(successColor.r, successColor.g, successColor.b, 0.15)
                                    border.width: 1
                                    border.color: Qt.rgba(successColor.r, successColor.g, successColor.b, 0.3)
                                    radius: dp(3)

                                    Text {
                                        id: playlistBadgeText
                                        anchors.centerIn: parent
                                        text: modelData.downloaded_playlist || ""
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: dp(9)
                                        font.weight: Font.DemiBold
                                        color: successColor
                                    }
                                }
                            }
                        }

                        // Download button / Downloaded / Failed / Downloading indicator
                        Rectangle {
                            Layout.preferredWidth: dp(40)
                            Layout.preferredHeight: dp(40)
                            radius: dp(20)
                            color: {
                                if (modelData.is_downloaded)
                                    return Qt.rgba(successColor.r, successColor.g, successColor.b, 0.15)
                                if (modelData.is_failed)
                                    return Qt.rgba(failedColor.r, failedColor.g, failedColor.b, 0.12)
                                if (isDownloading)
                                    return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                return accentColor
                            }

                            // Download icon (shown when available to download)
                            Image {
                                id: dlBtnIcon
                                anchors.centerIn: parent
                                width: dp(20)
                                height: dp(20)
                                source: "./assets/download_button.svg"
                                sourceSize: Qt.size(dp(40), dp(40))
                                fillMode: Image.PreserveAspectFit
                                visible: false
                            }
                            ColorOverlay {
                                anchors.fill: dlBtnIcon
                                source: dlBtnIcon
                                color: "#000000"
                                visible: !modelData.is_downloaded && !modelData.is_failed && !isDownloading
                            }

                            // Progress ring (shown while downloading)
                            Canvas {
                                id: progressRing
                                anchors.centerIn: parent
                                width: dp(32)
                                height: dp(32)
                                visible: isDownloading
                                onPaint: {
                                    var ctx = getContext("2d")
                                    var cx = width / 2
                                    var cy = height / 2
                                    var r = (width / 2) - 2.5
                                    var startAngle = -Math.PI / 2

                                    ctx.clearRect(0, 0, width, height)

                                    // Background track
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                                    ctx.lineWidth = 3
                                    ctx.strokeStyle = Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.25)
                                    ctx.stroke()

                                    // Progress arc
                                    var sweep = dlProgress * 2 * Math.PI
                                    if (sweep > 0) {
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, startAngle, startAngle + sweep)
                                        ctx.lineWidth = 3
                                        ctx.strokeStyle = accentColor
                                        ctx.lineCap = "round"
                                        ctx.stroke()
                                    }
                                }
                            }

                            // Percentage text inside ring
                            Text {
                                anchors.centerIn: parent
                                text: Math.round(dlProgress * 100)
                                font.family: downloadPage.globalFont
                                font.pixelSize: dp(9)
                                font.weight: Font.Bold
                                color: accentColor
                                visible: isDownloading
                            }

                            // Checkmark (shown when downloaded)
                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                font.pixelSize: dp(20)
                                font.weight: Font.Bold
                                color: successColor
                                visible: modelData.is_downloaded === true
                            }

                            // X mark (shown when failed)
                            Text {
                                anchors.centerIn: parent
                                text: "\u2717"
                                font.pixelSize: dp(18)
                                font.weight: Font.Bold
                                color: failedColor
                                visible: modelData.is_failed === true && !modelData.is_downloaded
                            }

                        }
                    }
                }

                    // Loading indicator
                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: downloadPage.isSearching && downloadPage.searchResults.length === 0
                        running: downloadPage.isSearching && downloadPage.searchResults.length === 0
                        palette.dark: accentColor
                    }
                }

                // ─── Queue Panel (1/4 width) ───────────────────────
                Rectangle {
                    id: queuePanel
                    Layout.fillHeight: true
                    Layout.preferredWidth: downloadPage.width * 0.25
                    Layout.minimumWidth: dp(200)
                    Layout.maximumWidth: dp(350)
                    visible: queueModel.count > 0
                    color: cardColor
                    border.color: cardBorderColor
                    border.width: 1
                    radius: dp(8)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: dp(10)
                        spacing: dp(8)

                        // Queue header
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: dp(8)

                            Text {
                                text: "Downloads"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dp(14)
                                font.weight: Font.Bold
                                color: textColor
                            }

                            // Count badge
                            Rectangle {
                                width: queueCountText.implicitWidth + dp(12)
                                height: dp(20)
                                radius: dp(10)
                                color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)

                                Text {
                                    id: queueCountText
                                    anchors.centerIn: parent
                                    text: queueModel.count
                                    font.family: downloadPage.globalFont
                                    font.pixelSize: dp(11)
                                    font.weight: Font.Bold
                                    color: accentColor
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // Clear button
                            Rectangle {
                                width: clearQueueText.implicitWidth + dp(16)
                                height: dp(24)
                                radius: dp(12)
                                color: clearQueueMouse.containsMouse
                                    ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                    : "transparent"
                                border.width: 1
                                border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                visible: {
                                    for (var i = 0; i < queueModel.count; i++) {
                                        var s = queueModel.get(i).status
                                        if (s === "complete" || s === "failed") return true
                                    }
                                    return false
                                }

                                Text {
                                    id: clearQueueText
                                    anchors.centerIn: parent
                                    text: "Clear"
                                    font.family: downloadPage.globalFont
                                    font.pixelSize: dp(11)
                                    color: dimTextColor
                                }

                                MouseArea {
                                    id: clearQueueMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // Remove completed and failed items
                                        for (var i = queueModel.count - 1; i >= 0; i--) {
                                            var s = queueModel.get(i).status
                                            if (s === "complete" || s === "failed") {
                                                queueModel.remove(i)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Separator
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                        }

                        // Queue items list
                        ListView {
                            id: queueListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: dp(4)
                            model: queueModel

                            ScrollBar.vertical: ScrollBar {
                                active: true
                                policy: ScrollBar.AsNeeded
                            }

                            delegate: Rectangle {
                                id: queueItemDelegate
                                width: queueListView.width - dp(14)
                                height: dp(52)
                                radius: dp(6)
                                color: {
                                    if (model.status === "complete")
                                        return queueItemHover.hovered
                                            ? Qt.rgba(successColor.r, successColor.g, successColor.b, 0.18)
                                            : Qt.rgba(successColor.r, successColor.g, successColor.b, 0.08)
                                    if (model.status === "failed")
                                        return Qt.rgba(downloadPage.accentColor.r, downloadPage.accentColor.g, downloadPage.accentColor.b, 0.0)
                                    return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.06)
                                }

                                HoverHandler {
                                    id: queueItemHover
                                    enabled: model.status === "complete"
                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                    cursorShape: Qt.PointingHandCursor
                                }

                                TapHandler {
                                    enabled: model.status === "complete"
                                    onTapped: {
                                        if (!mediaManager) return
                                        var knownPath = downloadPage.downloadedPaths[model.songId] || ""
                                        // Fall back to looking up song data from searchResults
                                        var song = null
                                        var results = downloadPage.searchResults
                                        for (var i = 0; i < results.length; i++) {
                                            if (results[i].song_id === model.songId) {
                                                song = results[i]
                                                break
                                            }
                                        }
                                        if (spotifyManager && spotifyManager.is_connected() && spotifyManager.is_playing()) {
                                            spotifyManager.pause()
                                        }
                                        if (settingsManager && settingsManager.mediaSource !== "local") {
                                            settingsManager.set_media_source("local")
                                        }
                                        var ok = false
                                        if (knownPath) {
                                            ok = mediaManager.play_file_at_path(knownPath)
                                        }
                                        if (!ok && song) {
                                            ok = mediaManager.play_downloaded_song(
                                                song.artist || "",
                                                song.name || "",
                                                song.downloaded_playlist || ""
                                            )
                                        }
                                        if (!ok) {
                                            downloadPage.statusText = "Could not play: " + (model.songName || "")
                                            return
                                        }
                                        downloadPage.statusText = "Playing: " + (model.songName || "")
                                        if (downloadPage.stackView) {
                                            downloadPage.stackView.push("MediaRoom.qml", {
                                                stackView: downloadPage.stackView
                                            })
                                        }
                                    }
                                }
                                border.width: 1
                                border.color: {
                                    if (model.status === "complete")
                                        return Qt.rgba(successColor.r, successColor.g, successColor.b, 0.2)
                                    if (model.status === "failed")
                                        return Qt.rgba(1, 0, 0, 0.2)
                                    return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.1)
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: dp(8)
                                    spacing: dp(4)

                                    // Song name + status icon row
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: dp(6)

                                        // Status icon
                                        Text {
                                            text: {
                                                if (model.status === "complete") return "\u2713"
                                                if (model.status === "failed") return "\u2717"
                                                return "\u25CF"
                                            }
                                            font.pixelSize: dp(12)
                                            font.weight: Font.Bold
                                            color: {
                                                if (model.status === "complete") return successColor
                                                if (model.status === "failed") return "#e53935"
                                                return accentColor
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: model.songName
                                            font.family: downloadPage.globalFont
                                            font.pixelSize: dp(11)
                                            font.weight: Font.DemiBold
                                            color: textColor
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                        }
                                    }

                                    // Progress bar (for downloading items)
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: dp(6)
                                        visible: model.status === "downloading"

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: dp(3)
                                            color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                        }

                                        Rectangle {
                                            width: parent.width * model.progress
                                            height: parent.height
                                            radius: dp(3)
                                            color: accentColor

                                            Behavior on width {
                                                NumberAnimation { duration: 200 }
                                            }
                                        }
                                    }

                                    // Status text for completed/failed
                                    Text {
                                        visible: model.status !== "downloading"
                                        text: {
                                            if (model.status === "complete") return "Downloaded"
                                            if (model.status === "failed") return model.errorMsg || "Failed"
                                            return ""
                                        }
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: dp(10)
                                        color: {
                                            if (model.status === "complete") return successColor
                                            if (model.status === "failed") return "#e53935"
                                            return dimTextColor
                                        }
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    // Percentage for downloading
                                    Text {
                                        visible: model.status === "downloading"
                                        text: Math.round(model.progress * 100) + "%"
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: dp(10)
                                        font.weight: Font.DemiBold
                                        color: accentColor
                                    }
                                }
                            }
                        }
                    }
                }
            }

        }
    }

    // ─── Error Detail Popup ───────────────────────────────────────

    Popup {
        id: errorPopup
        anchors.centerIn: parent
        width: dp(320)
        height: errorPopupContent.implicitHeight + dp(32)
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string songName: ""
        property string errorMsg: ""
        property string songJson: ""

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: bgColor
            border.color: Qt.rgba(errorPopup.parent ? downloadPage.accentColor.r : 0, errorPopup.parent ? downloadPage.accentColor.g : 0, errorPopup.parent ? downloadPage.accentColor.b : 0, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: errorPopupContent
            anchors.fill: parent
            anchors.margins: dp(16)
            spacing: dp(12)

            Text {
                text: "Download Failed"
                font.family: downloadPage.globalFont
                font.pixelSize: dp(18)
                font.weight: Font.Bold
                color: "#e53935"
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: errorSongLabel.implicitHeight + dp(12)
                color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                radius: dp(6)

                Text {
                    id: errorSongLabel
                    anchors.centerIn: parent
                    width: parent.width - dp(16)
                    text: errorPopup.songName
                    font.family: downloadPage.globalFont
                    font.pixelSize: dp(13)
                    font.weight: Font.DemiBold
                    color: textColor
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                text: errorPopup.errorMsg
                font.family: downloadPage.globalFont
                font.pixelSize: dp(12)
                color: dimTextColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: dp(8)

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(36)
                    color: errorRetryMouse.pressed
                        ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.35)
                        : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.22)
                    border.color: accentColor
                    border.width: 1
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "Retry"
                        font.family: downloadPage.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: textColor
                    }

                    MouseArea {
                        id: errorRetryMouse
                        anchors.fill: parent
                        onClicked: {
                            if (errorPopup.songJson.length > 0) {
                                downloadPage.startDownload(errorPopup.songJson)
                            }
                            errorPopup.close()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dp(36)
                    color: errorDismissMouse.pressed
                        ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                        : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                    border.width: 1
                    radius: dp(6)

                    Text {
                        anchors.centerIn: parent
                        text: "OK"
                        font.family: downloadPage.globalFont
                        font.pixelSize: dp(13)
                        font.weight: Font.DemiBold
                        color: textColor
                    }

                    MouseArea {
                        id: errorDismissMouse
                        anchors.fill: parent
                        onClicked: errorPopup.close()
                    }
                }
            }
        }
    }

    // ─── New Playlist Dialog (matches MediaPlayer) ──────────────────

    Popup {
        id: dlNewPlaylistDialog
        property bool npShowKeyboard: false

        property real npCompactW: dp(420)
        property real npCompactH: dlNewPlaylistContent.implicitHeight + dp(48)
        property real npExpandedW: Math.min(downloadPage.width * 0.92, dp(700))
        property real npExpandedH: downloadPage.height * 0.82

        width:  npShowKeyboard ? npExpandedW  : npCompactW
        height: npShowKeyboard ? npExpandedH  : npCompactH
        x: (parent.width  - width)  / 2
        y: (parent.height - height) / 2

        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: dlNewPlaylistInput.forceActiveFocus()
        onClosed: { npShowKeyboard = false; dlNewPlaylistInput.text = "" }

        Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: bgColor
            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
            border.width: 1
            radius: dp(12)
        }

        ColumnLayout {
            id: dlNewPlaylistContent
            anchors.fill: parent
            anchors.margins: dp(24)
            spacing: dp(14)

            // Top portion (title + input + buttons)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: dp(14)

                Text {
                    text: "New Playlist"
                    font.family: downloadPage.globalFont
                    font.pixelSize: dp(22)
                    font.weight: Font.Bold
                    color: textColor
                    Layout.fillWidth: true
                }

                Text {
                    text: "Enter a name for the new playlist folder."
                    font.family: downloadPage.globalFont
                    font.pixelSize: dp(14)
                    color: dimTextColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: !dlNewPlaylistDialog.npShowKeyboard
                }

                // Input row with keyboard toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: dp(8)

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(48)
                        color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                        border.color: dlNewPlaylistInput.activeFocus ? accentColor : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                        border.width: 1
                        radius: dp(8)

                        TextField {
                            id: dlNewPlaylistInput
                            anchors.fill: parent
                            anchors.leftMargin: dp(12)
                            anchors.rightMargin: dp(12)
                            placeholderText: "Playlist name..."
                            placeholderTextColor: dimTextColor
                            font.family: downloadPage.globalFont
                            font.pixelSize: dp(16)
                            color: textColor
                            background: Item {}
                            selectByMouse: true
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    var pName = text.trim()
                                    if (mediaManager.create_playlist(pName)) {
                                        downloadPage.selectedPlaylist = pName
                                        downloadManager.set_download_playlist(pName)
                                        downloadPage.downloadPlaylists = mediaManager.get_movable_playlist_names()
                                    }
                                    dlNewPlaylistDialog.close()
                                }
                            }
                        }
                    }

                    // Keyboard toggle
                    Rectangle {
                        Layout.preferredWidth: dp(48)
                        Layout.preferredHeight: dp(48)
                        color: dlNewPlaylistDialog.npShowKeyboard
                            ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.25)
                            : dlNpKbToggleMouse.containsMouse
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.12)
                                : "transparent"
                        radius: dp(6)
                        border.width: 1
                        border.color: dlNewPlaylistDialog.npShowKeyboard ? accentColor : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)

                        Text {
                            anchors.centerIn: parent
                            text: "\u2328"
                            font.pixelSize: dp(20)
                            color: dlNewPlaylistDialog.npShowKeyboard ? accentColor : dimTextColor
                        }

                        MouseArea {
                            id: dlNpKbToggleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                dlNewPlaylistDialog.npShowKeyboard = !dlNewPlaylistDialog.npShowKeyboard
                                dlNewPlaylistInput.forceActiveFocus()
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
                        color: dlNpCancelMouse.pressed
                            ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                            : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                        border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                        border.width: 1
                        radius: dp(8)

                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            font.family: downloadPage.globalFont
                            font.pixelSize: dp(15)
                            font.weight: Font.DemiBold
                            color: textColor
                        }

                        MouseArea {
                            id: dlNpCancelMouse
                            anchors.fill: parent
                            onClicked: dlNewPlaylistDialog.close()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(52)
                        color: dlNpCreateMouse.pressed ? Qt.darker(accentColor, 1.3) : accentColor
                        radius: dp(8)
                        opacity: dlNewPlaylistInput.text.trim().length > 0 ? 1.0 : 0.4

                        Text {
                            anchors.centerIn: parent
                            text: "Create"
                            font.family: downloadPage.globalFont
                            font.pixelSize: dp(15)
                            font.weight: Font.DemiBold
                            color: "#000000"
                        }

                        MouseArea {
                            id: dlNpCreateMouse
                            anchors.fill: parent
                            enabled: dlNewPlaylistInput.text.trim().length > 0
                            onClicked: {
                                var pName = dlNewPlaylistInput.text.trim()
                                if (mediaManager.create_playlist(pName)) {
                                    downloadPage.selectedPlaylist = pName
                                    downloadManager.set_download_playlist(pName)
                                    downloadPage.downloadPlaylists = mediaManager.get_movable_playlist_names()
                                }
                                dlNewPlaylistDialog.close()
                            }
                        }
                    }
                }
            }

            // ─── Inline On-Screen Keyboard ─────────────────────
            Rectangle {
                id: dlNpKeyboard
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: dlNewPlaylistDialog.npShowKeyboard
                color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.04)
                border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                border.width: 1
                radius: dp(8)

                property var rows: [
                    ["Q","W","E","R","T","Y","U","I","O","P"],
                    ["A","S","D","F","G","H","J","K","L"],
                    ["Z","X","C","V","B","N","M"]
                ]

                property real kbMargin: dp(6)
                property real kbSpacing: dp(3)
                property real kbRowCount: 4
                property real kbColCount: 10
                property real keyW: Math.floor((width - kbMargin * 2 - kbSpacing * (kbColCount - 1)) / kbColCount)
                property real keyH: Math.floor((height - kbMargin * 2 - kbSpacing * (kbRowCount - 1)) / kbRowCount)
                property real keyFont: Math.max(dp(11), Math.min(keyH * 0.45, dp(28)))

                Column {
                    anchors.centerIn: parent
                    spacing: dlNpKeyboard.kbSpacing

                    Repeater {
                        model: dlNpKeyboard.rows
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: dlNpKeyboard.kbSpacing
                            Repeater {
                                model: modelData
                                Rectangle {
                                    width: dlNpKeyboard.keyW
                                    height: dlNpKeyboard.keyH
                                    radius: dp(4)
                                    color: dlNpKeyMA.pressed
                                        ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                        : dlNpKeyMA.containsMouse
                                            ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                            : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                                    border.width: 1
                                    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.family: downloadPage.globalFont
                                        font.pixelSize: dlNpKeyboard.keyFont
                                        font.weight: Font.DemiBold
                                        color: textColor
                                    }
                                    MouseArea {
                                        id: dlNpKeyMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            dlNewPlaylistInput.text += modelData.toLowerCase()
                                            dlNewPlaylistInput.cursorPosition = dlNewPlaylistInput.text.length
                                            dlNewPlaylistInput.forceActiveFocus()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Bottom row: space + backspace
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: dlNpKeyboard.kbSpacing

                        // Space bar
                        Rectangle {
                            width: dlNpKeyboard.keyW * 6 + dlNpKeyboard.kbSpacing * 5
                            height: dlNpKeyboard.keyH
                            radius: dp(4)
                            color: dlNpSpaceMouse.pressed
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                : dlNpSpaceMouse.containsMouse
                                    ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                    : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                            Text {
                                anchors.centerIn: parent
                                text: "SPACE"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dlNpKeyboard.keyFont * 0.85
                                font.weight: Font.Bold
                                color: dimTextColor
                            }
                            MouseArea {
                                id: dlNpSpaceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    dlNewPlaylistInput.text += " "
                                    dlNewPlaylistInput.cursorPosition = dlNewPlaylistInput.text.length
                                    dlNewPlaylistInput.forceActiveFocus()
                                }
                            }
                        }

                        // Backspace
                        Rectangle {
                            width: dlNpKeyboard.keyW * 2 + dlNpKeyboard.kbSpacing
                            height: dlNpKeyboard.keyH
                            radius: dp(4)
                            color: dlNpBackMouse.pressed
                                ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3)
                                : dlNpBackMouse.containsMouse
                                    ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                                    : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
                            Text {
                                anchors.centerIn: parent
                                text: "\u232B"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dlNpKeyboard.keyFont
                                color: textColor
                            }
                            MouseArea {
                                id: dlNpBackMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (dlNewPlaylistInput.text.length > 0) {
                                        dlNewPlaylistInput.text = dlNewPlaylistInput.text.slice(0, -1)
                                        dlNewPlaylistInput.cursorPosition = dlNewPlaylistInput.text.length
                                    }
                                    dlNewPlaylistInput.forceActiveFocus()
                                }
                            }
                        }

                        // Enter / Create
                        Rectangle {
                            width: dlNpKeyboard.keyW * 2 + dlNpKeyboard.kbSpacing
                            height: dlNpKeyboard.keyH
                            radius: dp(6)
                            color: dlNpEnterMouse.pressed ? Qt.darker(accentColor, 1.3) : accentColor
                            opacity: dlNewPlaylistInput.text.trim().length > 0 ? 1.0 : 0.4
                            Text {
                                anchors.centerIn: parent
                                text: "CREATE"
                                font.family: downloadPage.globalFont
                                font.pixelSize: dlNpKeyboard.keyFont * 0.85
                                font.weight: Font.Bold
                                color: "#000000"
                            }
                            MouseArea {
                                id: dlNpEnterMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: dlNewPlaylistInput.text.trim().length > 0
                                onClicked: dlNewPlaylistInput.accepted()
                            }
                        }
                    }
                }
            }
        }
    }
}
