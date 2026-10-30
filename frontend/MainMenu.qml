import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import QtQuick.Dialogs
import "." as App
import OCTAVE.OBD 1.0

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: mainMenu
    objectName: "mainMenu"

    // Full IMU rate while the home screen shows an IMU reading on a card
    App.ImuDemand {
        consumer: "mainMenu"
        wanted: mainMenu.visible && !!settingsManager && settingsManager.homeOBDParameters.some(function(id) {
            return App.OBDParameterModel.sensorParameters.indexOf(id) !== -1
        })
    }
    property StackView stackView
    property ApplicationWindow mainWindow
    property real windowWidth
    property real windowHeight
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // What is playing, whichever source has the media controls (the phone
    // while mirroring, else local media): nowPlaying (C++) follows it and
    // owns the phone's position clock; this page only words and draws it
    readonly property var _np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    readonly property bool useRemote: !!_np && _np.source !== "local"   // the phone or Android Auto
    readonly property string remoteName: !!_np && _np.source === "androidauto" ? "Android Auto" : "Phone"
    // Something to show: a track, or the phone (which says "Nothing playing")
    readonly property bool hasMedia: !!_np && (_np.hasTrack || useRemote)
    readonly property string currentArt: (_np && _np.artUrl) ? _np.artUrl : "./assets/missing_art.png"
    readonly property string currentTitle: !_np ? "" : (_np.hasTrack ? _np.title : (useRemote ? remoteName : ""))
    readonly property string currentArtist: !_np ? "" : (_np.hasTrack ? _np.artist : (useRemote ? "Nothing playing" : ""))
    readonly property string currentAlbum: (_np && _np.hasTrack) ? _np.album : ""
    readonly property int positionMs: _np ? _np.positionMs : 0
    readonly property int durationMs: _np ? _np.durationMs : 0

    function formatTime(ms) { return units.formatDuration(ms) }

    // First-run prompts: each card asks for its connection while it has
    // nothing to show, so a fresh install is one tap from a car and music
    readonly property bool obdConnected: typeof obdManager !== "undefined" && !!obdManager
                                         && obdManager.connectionState === OBDManager.Connected
    // The active playlist's length (not a raw directory scan, which misses
    // "All Music"); the phone or Android Auto counts as music
    property int localSongCount: 0
    readonly property bool musicEmpty: !useRemote && localSongCount === 0
    readonly property bool phoneOffered: (!!settingsManager && settingsManager.phoneMirrorEnabled)
                                         || (typeof airPlayManager !== "undefined" && !!airPlayManager
                                             && airPlayManager.available && airPlayManager.enabled)
    function openPhone() {
        if (settingsManager && settingsManager.phoneMirrorEnabled)
            stackView.openPage("PhoneMirrorView.qml")
        else
            stackView.openPage("AirPlayView.qml")
    }

    // Library folder picker (same rules and wording as Media Room's Browse)
    property string _folderError: ""
    FolderDialog {
        id: homeFolderDialog
        title: "Select Music Library Folder"
        onAccepted: {
            if (!settingsManager) return
            mainMenu._folderError = settingsManager.save_media_folder_url(selectedFolder)
                ? "" : "That folder can't be used as the music library: pick one on the device's storage or SD card."
        }
    }
    Connections {
        target: typeof mediaManager !== "undefined" ? mediaManager : null
        function onMediaListChanged(files) { mainMenu.localSongCount = files.length }
    }

    // Outlined accent button, the look of OBDConnectButton
    component PromptButton: Rectangle {
        id: promptButton
        property string text: ""
        signal clicked()
        height: App.Spacing.bottomBarNavButtonHeight
        width: Math.min(promptLabel.implicitWidth + mainMenu.dp(32), mediaSection.width - mainMenu.dp(24))
        radius: mainMenu.dpMin(8, 2)
        color: "transparent"
        border.color: App.Style.accent
        border.width: 1
        scale: promptMouse.pressed ? 0.9 : 1.0
        Behavior on scale {
            NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
        }
        Text {
            id: promptLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - mainMenu.dp(12))
            elide: Text.ElideRight
            text: promptButton.text
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText
            font.bold: true
            font.family: mainMenu.globalFont
        }
        MouseArea {
            id: promptMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: promptButton.clicked()
        }
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
                visible: !mainMenu.musicEmpty
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
                    // Layout.preferredHeight, not height: a ColumnLayout sizes its children
                    // from Layout.* / implicit size, and a plain height here was sometimes
                    // ignored (depending on startup timing), collapsing the row to 0 px so
                    // the Home card showed no title/artist.
                    Layout.preferredHeight: Math.ceil(App.Spacing.mainMenuSongTextSize * 1.4)

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

                        // Title marquee. Runs only while mainMenu is visible: this page stays alive

                        // hidden (StackView cache / pre-building), and a hidden marquee kept a 5 s

                        // animation cycling forever, costing ~15% of a Pi core at idle. restart()

                        // below bypasses the running binding, hence the visible checks there too.

                        Timer {
                            id: songScrollTimer
                            interval: 3000
                            running: songTitleText.width > songTitleContainer.width && mainMenu.visible
                            repeat: true
                            onTriggered: {
                                if (!mainMenu.visible) return
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
                            onFinished: if (mainMenu.visible) songScrollTimer.restart()
                        }
                    }
                }

                // Artist & Album with scrolling (like MediaRoom)
                Item {
                    id: metadataContainer
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: Math.ceil(App.Spacing.mainMenuArtistTextSize * 1.4)  // see songTitleContainer

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
                                text: mainMenu.hasMedia ? mainMenu.currentArtist : "Select a song"
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.7
                            }
                            Text {
                                text: mainMenu.hasMedia && mainMenu.currentAlbum ? "•" : ""
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.8
                            }
                            Text {
                                text: mainMenu.hasMedia ? mainMenu.currentAlbum : ""
                                color: App.Style.metadataColor
                                font.pixelSize: App.Spacing.mainMenuArtistTextSize
                                font.family: mainMenu.globalFont
                                opacity: 0.8
                            }
                        }

                        Timer {
                            id: metadataScrollTimer
                            interval: 3000
                            running: metadataRow.width > metadataContainer.width && mainMenu.visible
                            repeat: true
                            onTriggered: {
                                if (!mainMenu.visible) return
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
                            onFinished: if (mainMenu.visible) metadataScrollTimer.restart()
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
                        text: mainMenu.formatTime(progressSlider.userSeeking ? progressSlider.value : mainMenu.positionMs)
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.mainMenuTimeTextSize
                        font.family: mainMenu.globalFont
                        Layout.minimumWidth: App.Spacing.mainMenuTimeTextSize * 2.5
                    }

                    Slider {
                        id: progressSlider
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, mainMenu.durationMs)
                        enabled: !!mainMenu._np && mainMenu._np.canSeek && mainMenu.durationMs > 0

                        property bool userSeeking: false
                        // Follows the source, except while the user drags
                        Binding on value {
                            when: !progressSlider.userSeeking
                            value: mainMenu.positionMs
                            restoreMode: Binding.RestoreNone   // pausing must not snap back to 0
                        }

                        background: Rectangle {
                            x: progressSlider.leftPadding
                            y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                            width: progressSlider.availableWidth
                            height: App.Spacing.mainMenuSliderHeight
                            radius: height / 2
                            color: App.Style.secondaryTextColor

                            Rectangle {
                                // Ends at the handle's center so the rounded end stays under it
                                width: progressSlider.visualPosition * (parent.width - progressSlider.handle.width) + progressSlider.handle.width / 2
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
                                if (mainMenu._np)
                                    mainMenu._np.seek(value)
                                userSeeking = false
                            }
                        }
                    }

                    Text {
                        id: durationText
                        text: mainMenu.formatTime(mainMenu.durationMs)
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
                enabled: !mainMenu.musicEmpty   // the prompt's buttons own the taps
                onTapped: {
                    var defaultPage = settingsManager ? settingsManager.musicButtonDefaultPage : "mediaRoom"
                    var targetPage = defaultPage === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml"
                    stackView.openPage(targetPage)
                }
            }

            // No music yet: pick the library folder or bring the phone
            Column {
                objectName: "homeMusicPrompt"
                visible: mainMenu.musicEmpty
                anchors.centerIn: parent
                width: parent.width - dp(24)
                spacing: dp(16)

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "No music yet"
                    color: App.Style.metadataColor
                    font.pixelSize: App.Spacing.overallText * 1.2
                    font.bold: true
                    font.family: mainMenu.globalFont
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: mainMenu.phoneOffered ? "Pick a music folder or connect your phone"
                                                : "Pick the folder your music is in"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.9
                    font.family: mainMenu.globalFont
                }
                PromptButton {
                    objectName: "homeMusicBrowseButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Browse Music"
                    onClicked: homeFolderDialog.open()
                }
                PromptButton {
                    objectName: "homeConnectPhoneButton"
                    visible: mainMenu.phoneOffered
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Connect Phone"
                    onClicked: mainMenu.openPhone()
                }
                Text {
                    width: parent.width
                    visible: mainMenu._folderError !== ""
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: mainMenu._folderError
                    color: App.Style.statusDanger
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.family: mainMenu.globalFont
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
            // Hidden while no adapter is connected (its cards would all read
            // "--"); hiding it also drops its poll demand
            HomeOBDView {
                visible: mainMenu.obdConnected
                anchors.fill: parent
                anchors.margins: dp(5)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    stackView.openPage("OBDMenu.qml")
                }
            }

            // No adapter: connect straight from Home (scan, pair, connect);
            // above the MouseArea so the button gets its taps
            Column {
                objectName: "homeOBDPrompt"
                visible: !mainMenu.obdConnected
                anchors.centerIn: parent
                width: Math.min(parent.width - dp(40), dp(560))
                spacing: dp(16)

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "No vehicle connected"
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText * 1.2
                    font.bold: true
                    font.family: mainMenu.globalFont
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Plug in your OBD2 adapter and connect it to see live readings here"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText * 0.9
                    font.family: mainMenu.globalFont
                }
                App.OBDConnectButton {
                    objectName: "homeOBDConnectButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    mainWindow: mainMenu.mainWindow
                    fontFamily: mainMenu.globalFont
                }
            }
        }
    }

    Component.onCompleted: {
        if (typeof mediaManager !== "undefined" && mediaManager)
            localSongCount = mediaManager.get_current_song_list().length
        if (obdManager && obdManager.refresh_values) {
            obdManager.refresh_values()
        }
    }

    Connections {
        target: settingsManager
        function onHomeOBDParametersChanged() {
            if (obdManager && obdManager.refresh_values) {
                obdManager.refresh_values()
            }
        }
    }
}
