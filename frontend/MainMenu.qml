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

    // Full IMU rate while the home screen is up: its sensor panel shows the IMU
    App.ImuDemand {
        consumer: "mainMenu"
        wanted: mainMenu.visible && !mainMenu.dashboardHome
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
    // Follows the source only while on screen: home stays alive behind
    // every other page, and each 10 Hz tick re-laid out the time text and
    // slider for nothing. Back on screen it catches up at once.
    property int positionMs: 0
    Binding {
        target: mainMenu
        property: "positionMs"
        value: mainMenu._np ? mainMenu._np.positionMs : 0
        when: mainMenu.visible
        restoreMode: Binding.RestoreNone
    }
    readonly property int durationMs: _np ? _np.durationMs : 0

    function formatTime(ms) { return units.formatDuration(ms) }

    // First-run prompts: each card asks for its connection while it has
    // nothing to show, so a fresh install is one tap from a car and music
    readonly property bool obdConnected: typeof obdManager !== "undefined" && !!obdManager
                                         && obdManager.connectionState === OBDManager.Connected
    // The vehicle's picture beside the readings (the homeVehicleCard setting)
    // There with or without an adapter and with or without a vehicle
    // chosen, as the media and OBD cards are there with nothing to show
    readonly property bool vehicleCardShown: !!settingsManager && settingsManager.homeVehicleCard
    // The active playlist's length (not a raw directory scan, which misses
    // "All Music"); the phone or Android Auto counts as music
    property int localSongCount: 0
    readonly property bool musicEmpty: !useRemote && localSongCount === 0
    Connections {
        target: typeof mediaManager !== "undefined" ? mediaManager : null
        function onMediaListChanged(files) { mainMenu.localSongCount = files.length }
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

    // Home is a dashboard (HomeDashboard.qml: dashboardManager.homeDashboardId
    // in the dashboard renderer, editable like any other); the hand-built
    // panels below are the "classic" Home, kept selectable (the homeStyle
    // setting, Settings > Device) until the dashboard one has been checked
    // in the car, then they go (TODO/home-dashboard.md)
    property string homeStyle: settingsManager ? settingsManager.get_setting_with_default("homeStyle", "dashboard") : "dashboard"
    readonly property bool dashboardHome: homeStyle !== "classic"
                                          && typeof dashboardManager !== "undefined" && !!dashboardManager
    Connections {
        target: settingsManager
        function onGenericSettingChanged(key) {
            if (key === "homeStyle")
                mainMenu.homeStyle = settingsManager.get_setting_with_default("homeStyle", "dashboard")
        }
    }

    Loader {
        id: homeLoader
        objectName: "homeLoader"
        anchors.fill: parent
        sourceComponent: mainMenu.dashboardHome ? dashboardHomeComponent : classicHomeComponent
    }

    Component {
        id: dashboardHomeComponent
        App.HomeDashboard {
            stackView: mainMenu.stackView
            mainWindow: mainMenu.mainWindow
            musicEmpty: mainMenu.musicEmpty
        }
    }

    Component {
        id: classicHomeComponent
        Item {
    // Main content area, three panels: media on the left (a quarter), the OBD
    // readings in the middle, the sensors on the right; in portrait media is
    // a strip on top, the readings under it and the sensors a strip below
    GridLayout {
        anchors.fill: parent
        anchors.margins: dp(10)
        columns: App.Spacing.isPortrait ? 1 : 3
        columnSpacing: dp(10)
        rowSpacing: dp(10)

        // ========== LEFT SECTION: Media (a quarter of the width) ==========
        Rectangle {
            id: mediaSection
            objectName: "homeMediaSection"
            Layout.preferredWidth: App.Spacing.isPortrait ? -1 : Math.round(parent.width * 0.24)
            Layout.fillWidth: App.Spacing.isPortrait
            Layout.preferredHeight: App.Spacing.isPortrait ? Math.round(parent.height * 0.2) : -1
            Layout.fillHeight: !App.Spacing.isPortrait
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

            // Media content: one column in the narrow panel (art, title,
            // artist, progress); in portrait the panel is a short strip,
            // so the art stands on the left with the rest beside it
            GridLayout {
                id: mediaGrid
                visible: !mainMenu.musicEmpty
                anchors.fill: parent
                anchors.margins: dp(15)
                readonly property bool strip: App.Spacing.isPortrait
                columns: strip ? 2 : 1
                rowSpacing: dp(10)
                columnSpacing: dp(15)

                // Album Art (top, centered; the strip: left, full height)
                Item {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.rowSpan: mediaGrid.strip ? 5 : 1
                    Layout.fillWidth: !mediaGrid.strip
                    Layout.fillHeight: mediaGrid.strip
                    Layout.preferredWidth: mediaGrid.strip ? mediaGrid.height : -1
                    Layout.preferredHeight: mediaGrid.strip ? -1 : mediaGrid.width + dp(40)
                    Layout.maximumHeight: mediaGrid.strip ? Number.POSITIVE_INFINITY : mediaGrid.width + dp(40)
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

                // The strip: title and artist midway between the top and the progress bar
                Item {
                    visible: mediaGrid.strip
                    Layout.row: mediaGrid.strip ? 0 : 5
                    Layout.column: mediaGrid.strip ? 1 : 0
                    Layout.fillHeight: true
                }

                // Song title with scrolling (like MediaRoom)
                Item {
                    id: songTitleContainer
                    Layout.row: 1
                    Layout.column: mediaGrid.strip ? 1 : 0
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
                            duration: settingsManager ? settingsManager.textScrollSpeed : 5000
                            easing.type: Easing.InOutQuad
                            onFinished: if (mainMenu.visible) songScrollTimer.restart()
                        }
                    }
                }

                // Artist & Album with scrolling (like MediaRoom)
                Item {
                    id: metadataContainer
                    Layout.row: 2
                    Layout.column: mediaGrid.strip ? 1 : 0
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
                            duration: settingsManager ? settingsManager.textScrollSpeed : 5000
                            easing.type: Easing.InOutQuad
                            onFinished: if (mainMenu.visible) metadataScrollTimer.restart()
                        }
                    }
                }

                // Spacer
                Item {
                    Layout.row: 3
                    Layout.column: mediaGrid.strip ? 1 : 0
                    Layout.fillHeight: true
                }

                // Progress Bar
                RowLayout {
                    Layout.row: 4
                    Layout.column: mediaGrid.strip ? 1 : 0
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

                        // The slider is only as tall as its handle: a touch
                        // just above or below missed it and fell through to
                        // the card's tap, which opened the music page. This
                        // takes those touches (to a dp(44) target) and seeks.
                        MouseArea {
                            readonly property real reach: Math.max(0, (mainMenu.dp(44) - parent.height) / 2)
                            anchors.fill: parent
                            anchors.topMargin: -reach
                            anchors.bottomMargin: -reach
                            enabled: progressSlider.enabled
                            property bool own: false
                            function valueAt(x) {
                                var pos = Math.max(0, Math.min(1, (x - progressSlider.leftPadding) / progressSlider.availableWidth))
                                return progressSlider.from + pos * (progressSlider.to - progressSlider.from)
                            }
                            onPressed: function(mouse) {
                                own = mouse.y < reach || mouse.y > height - reach
                                if (!own) {
                                    mouse.accepted = false   // the slider's own
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
            // (the same prompt as the music screens)
            App.MusicEmptyPrompt {
                objectName: "homeMusicPrompt"
                visible: mainMenu.musicEmpty
                anchors.centerIn: parent
                width: parent.width - dp(24)
                stackView: mainMenu.stackView
                mainWindow: mainMenu.mainWindow
                fontFamily: mainMenu.globalFont
            }
        }

        // ========== MIDDLE SECTION: OBD (what the other two leave) ==========
        Rectangle {
            id: obdSection
            objectName: "homeObdSection"
            // The vehicle card: under the readings in a landscape window
            // (the panel is about as tall as wide there, and a car is
            // wider than tall), beside them in portrait
            readonly property bool vehicleBelow: !App.Spacing.isPortrait
            readonly property real vehicleTakenW: mainMenu.vehicleCardShown && !vehicleBelow ? vehicleCard.width + dp(15) : 0
            readonly property real vehicleTakenH: mainMenu.vehicleCardShown && vehicleBelow ? vehicleCard.height + dp(15) : 0
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
                id: homeReadings
                visible: mainMenu.obdConnected
                anchors.fill: parent
                anchors.margins: dp(5)
                // Room for the vehicle card, the grid's own spacing between them
                anchors.rightMargin: obdSection.vehicleTakenW > 0 ? obdSection.vehicleTakenW : dp(5)
                anchors.bottomMargin: obdSection.vehicleTakenH > 0 ? obdSection.vehicleTakenH : dp(5)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    stackView.openPage("OBDMenu.qml")
                }
            }

            // The vehicle with the readings; above the MouseArea, as its
            // tap opens Settings > Device > Vehicle (pick or change the
            // vehicle), not the OBD page
            App.HomeVehicleCard {
                id: vehicleCard
                visible: mainMenu.vehicleCardShown
                anchors { right: parent.right; bottom: parent.bottom; margins: dp(15) }
                width: obdSection.vehicleBelow ? parent.width - 2 * dp(15) : Math.round(parent.width * 0.36)
                // Under the readings it gives way as their rows grow (2 columns)
                readonly property real belowShare: homeReadings.cardCount <= 4 ? 0.36 : homeReadings.cardCount <= 6 ? 0.3 : 0.24
                height: obdSection.vehicleBelow ? Math.round(parent.height * belowShare) : parent.height - 2 * dp(15)
                onClicked: if (mainMenu.mainWindow) mainMenu.mainWindow.navigateToSettings("deviceSettings", "device_vehicle")
            }

            // No adapter: connect straight from Home (scan, pair, connect);
            // above the MouseArea so the button gets its taps
            Column {
                objectName: "homeOBDPrompt"
                visible: !mainMenu.obdConnected
                // Centred in what the vehicle card leaves of the panel
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -obdSection.vehicleTakenW / 2
                anchors.verticalCenterOffset: -obdSection.vehicleTakenH / 2
                width: Math.min(parent.width - obdSection.vehicleTakenW - dp(40), dp(560))
                spacing: dp(16)

                // The vehicle, while the panel has the height for it
                App.VehiclePicture {
                    objectName: "homeVehiclePicture"
                    width: parent.width
                    height: Math.round(obdSection.height * 0.42)
                    // (the vehicle card beside this shows it instead)
                    visible: hasPicture && obdSection.height >= dp(360) && !mainMenu.vehicleCardShown
                }

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

        // ========== RIGHT SECTION: Sensors ==========
        App.HomeSensorPanel {
            Layout.preferredWidth: App.Spacing.isPortrait ? -1 : Math.round(parent.width * 0.22)
            Layout.fillWidth: App.Spacing.isPortrait
            Layout.preferredHeight: App.Spacing.isPortrait ? Math.round(parent.height * 0.2) : -1
            Layout.fillHeight: !App.Spacing.isPortrait
            inRow: App.Spacing.isPortrait
            onClicked: stackView.openPage("SensorHome.qml")
        }
    }
        }
    }

    Component.onCompleted: {
        if (typeof mediaManager !== "undefined" && mediaManager)
            localSongCount = mediaManager.getCurrentSongList().length
        if (obdManager && obdManager.refreshValues) {
            obdManager.refreshValues()
        }
    }

    Connections {
        target: settingsManager
        function onHomeOBDParametersChanged() {
            if (obdManager && obdManager.refreshValues) {
                obdManager.refreshValues()
            }
        }
    }
}
