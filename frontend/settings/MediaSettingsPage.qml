import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import ".." as App

Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // Tile model — consumed by SettingsSidebarLayout (grid + slide-in popup).
    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    // Live state behind the tiles. Album count arrives by signal only so
    // opening settings never forces a library stats scan.
    property int _albumCount: -1

    property var tileModel: [
        { cardId: "media_library", title: "Library & Playback", component: libraryContent,
          summary: function() {
              if (!settingsManager) return {}
              var path = String(settingsManager.mediaFolder || "").replace(/[\\/]+$/, "")
              var name = path.substring(Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\")) + 1)
              var lib = !name ? "No folder" : pageRoot._albumCount >= 0 ? pageRoot._albumCount + " albums" : name
              return { state: lib + " · " + (settingsManager.autoPlayOnStartup ? "Autoplay" : "Manual") }
          } },
        { cardId: "media_spotify", title: "Spotify", component: spotifyContent,
          summary: function() {
              if (!pageRoot.hasSpotify) return {}
              return { state: spotifyManager.signedIn ? "Signed in as " + spotifyManager.userName
                            : spotifyManager.state === "signingIn" ? "Signing in…"
                            : spotifyManager.clientId === "" ? "Needs setup" : "Not connected" }
          } }
    ]

    readonly property bool hasSpotify: typeof spotifyManager !== "undefined" && spotifyManager !== null

    Connections {
        target: typeof mediaManager !== "undefined" ? mediaManager : null
        function onAlbumCountChanged(count) { pageRoot._albumCount = count }
    }

    // Folder dialog for selecting music library folder. The manager turns
    // what it returns into a path (src/util/pathurl.h): a file:// URL on
    // desktop, an Android content:// tree for internal storage or an SD card.
    property string folderError: ""
    FolderDialog {
        id: folderDialog
        title: "Select Music Library Folder"
        onAccepted: {
            if (!settingsManager) return
            pageRoot.folderError = settingsManager.save_media_folder_url(selectedFolder)
                ? "" : "That folder can't be used as the music library: pick one on the device's storage or SD card."
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

    // ─────────────────────────────────────────── Library & Playback
    Component {
        id: libraryContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                title: "Library Folder"
                description: "Each subfolder is a playlist."

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
                                    pageRoot.folderError = settingsManager.save_media_folder(text)
                                        ? "" : "Not a folder OCTAVE can use"
                                }
                            }
                        }

                        SettingsButton {
                            text: "Browse"
                            Layout.preferredHeight: mediaFolderField.height
                            // the browser demo opens the browser's own folder picker instead
                            onClicked: if (!mediaManager || !mediaManager.pickBrowserLibrary()) folderDialog.open()
                        }

                        SettingsButton {
                            text: "Scan"
                            Layout.preferredHeight: mediaFolderField.height
                            onClicked: {
                                if (mediaManager) mediaManager.scanLibrary()
                            }
                        }

                        SettingsButton {
                            text: "Clean Titles"
                            Layout.preferredHeight: mediaFolderField.height
                            onClicked: {
                                if (mediaManager) mediaManager.stripFilenames()
                            }
                        }
                    }

                    // Why a picked folder wasn't taken (instead of an empty library)
                    SettingDescription {
                        visible: pageRoot.folderError !== ""
                        text: pageRoot.folderError
                        color: App.Style.statusDanger
                    }
                }
            }

            SettingCategory {
                title: "Startup Volume"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: volumeSlider
                        // Percent, like every other volume slider; the setting
                        // is stored linear (VolumeController owns the curve)
                        from: 0
                        to: 100
                        stepSize: 1
                        value: settingsManager && volumeController ? volumeController.toPercent(settingsManager.startUpVolume) : 50
                        valueDisplay: Math.round(value) + "%"
                        activeColor: App.Style.volumeSliderColor
                        Layout.fillWidth: true

                        Timer {
                            id: volumeUpdateTimer
                            interval: 100
                            running: false
                            repeat: false
                            onTriggered: {
                                if (settingsManager) {
                                    settingsManager.save_start_volume(volumeController.toLinear(volumeSlider.value))
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
                id: audioOutputCategory
                objectName: "audioOutputCategory"
                title: "Audio Output"
                // Music and phone audio play here. A picked output that is
                // unplugged falls back to the system default until it returns.
                readonly property var outputs: typeof mediaManager !== "undefined" && mediaManager ? mediaManager.audioOutputs : []
                readonly property string picked: settingsManager ? settingsManager.audioOutputDevice : ""
                readonly property bool pickedPresent: {
                    for (var i = 0; i < outputs.length; i++)
                        if (outputs[i].id === picked) return true
                    return false
                }
                readonly property string pickedName: {
                    for (var i = 0; i < outputs.length; i++)
                        if (outputs[i].id === picked) return outputs[i].name
                    return ""
                }
                description: {
                    var now = typeof mediaManager !== "undefined" && mediaManager ? mediaManager.currentAudioOutput : ""
                    if (picked !== "" && !pickedPresent)
                        return "The picked output is not connected; playing on " + (now || "the system default") + " until it is."
                    return now ? "Playing on " + now + "." : "Where music and phone audio play."
                }

                SettingsChips {
                    options: {
                        var names = ["System default"]
                        for (var i = 0; i < audioOutputCategory.outputs.length; i++)
                            names.push(audioOutputCategory.outputs[i].name)
                        return names
                    }
                    currentValue: audioOutputCategory.picked === "" || !audioOutputCategory.pickedPresent
                                  ? "System default" : audioOutputCategory.pickedName
                    onSelected: function(name) {
                        if (!settingsManager) return
                        if (name === "System default") {
                            settingsManager.save_audio_output_device("")
                            return
                        }
                        var list = audioOutputCategory.outputs
                        for (var i = 0; i < list.length; i++) {
                            if (list[i].name === name) {
                                settingsManager.save_audio_output_device(list[i].id)
                                return
                            }
                        }
                    }
                }
            }

            SettingCategory {
                title: "Autoplay on Startup"

                inlineContent: SettingsToggle {
                    id: autoPlayToggle
                    compact: true
                    checked: settingsManager ? settingsManager.autoPlayOnStartup : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_auto_play_on_startup(checked)
                    }
                }
            }

            SettingCategory {
                title: "Remember Shuffle"

                inlineContent: SettingsToggle {
                    id: persistShuffleToggle
                    compact: true
                    checked: settingsManager ? settingsManager.persistShuffleState : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_persist_shuffle_state(checked)
                    }
                }
            }

            SettingCategory {
                title: "Open Music to the Library"

                inlineContent: SettingsToggle {
                    id: musicButtonDefaultPageToggle
                    compact: true
                    checked: settingsManager ? settingsManager.musicButtonDefaultPage === "mediaPlayer" : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) {
                            settingsManager.save_music_button_default_page(checked ? "mediaPlayer" : "mediaRoom")
                        }
                    }
                }
            }

            SettingCategory {
                title: "Return to Library After Picking a Song"

                inlineContent: SettingsToggle {
                    id: returnToLibraryToggle
                    compact: true
                    checked: settingsManager ? settingsManager.returnToLibraryAfterSelection : false
                    activeColor: App.Style.accent
                    inactiveColor: App.Style.hoverColor

                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_return_to_library_after_selection(checked)
                    }
                }
            }

            // Scan output last, like the OBD connection log: it is tall.
            App.TerminalFeedback {
                id: scanTerminal
                Layout.fillWidth: true
                Layout.preferredHeight: pageRoot.dp(300)
                title: "Scan Log"
                // Nothing to show until Scan or Clean Titles has run
                property int lineCount: 0
                visible: lineCount > 0

                Connections {
                    target: mediaManager
                    function onScanProgress(message) {
                        scanTerminal.lineCount++
                        scanTerminal.appendLine(message)
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── Spotify
    // One card, one step at a time: the Client ID of a Spotify developer app
    // (the user's own, unless the build has one), then Connect, which shows
    // only once there is an ID that can work. Signed in, just who and Sign
    // Out. OCTAVE then browses the library and controls the Spotify app on
    // the phone (SpotifyManager).
    Component {
        id: spotifyContent
        ColumnLayout {
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            SettingCategory {
                id: spotifyCard
                title: "Spotify"
                description: "Your playlists, Liked Songs and albums in the music menu. The Spotify app on your "
                             + "phone (or any Spotify device) plays the music; OCTAVE is the remote. Needs Spotify "
                             + "Premium."

                readonly property bool signedIn: pageRoot.hasSpotify && spotifyManager.signedIn
                readonly property bool signingIn: pageRoot.hasSpotify && spotifyManager.state === "signingIn"
                readonly property bool builtInId: pageRoot.hasSpotify && spotifyManager.builtInClientId
                readonly property bool idReady: pageRoot.hasSpotify && spotifyCard.isClientId(spotifyManager.clientId)
                // A build with its own ID needs none of the setup, unless asked for
                property bool useOwnId: false
                readonly property bool showSetup: pageRoot.hasSpotify && !signedIn && !signingIn
                                                  && (!builtInId || useOwnId)

                function isClientId(text) { return /^[0-9a-zA-Z]{32}$/.test(text.trim()) }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingDescription {
                        visible: spotifyCard.showSetup
                        text: "To connect, create an app at developer.spotify.com (Web API). Add the redirect URI "
                              + "below and your account under User Management, then paste the app's Client ID here."
                    }

                    SettingDescription {
                        visible: spotifyCard.showSetup
                        text: "Redirect URI: " + (pageRoot.hasSpotify ? spotifyManager.redirectUri : "")
                    }

                    SettingsTextField {
                        id: spotifyClientIdField
                        visible: spotifyCard.showSetup
                        Layout.fillWidth: true
                        placeholderText: "Client ID"
                        text: pageRoot.hasSpotify ? spotifyManager.clientId : ""
                        onEditingFinished: if (pageRoot.hasSpotify) spotifyManager.setClientId(text)
                        // A whole ID pasted is taken at once: a phone's keyboard
                        // may never finish editing, which would keep Connect away
                        onTextChanged: if (pageRoot.hasSpotify && spotifyCard.isClientId(text)
                                               && text.trim() !== spotifyManager.clientId)
                                           spotifyManager.setClientId(text)
                        Connections {
                            target: pageRoot.hasSpotify ? spotifyManager : null
                            function onClientIdChanged() { spotifyClientIdField.text = spotifyManager.clientId }
                        }
                    }

                    SettingDescription {
                        visible: spotifyCard.showSetup && spotifyClientIdField.text.trim() !== ""
                                 && !spotifyCard.isClientId(spotifyClientIdField.text)
                        text: "A Client ID is 32 letters and digits."
                        color: App.Style.statusDanger
                    }

                    SettingLabel {
                        visible: !pageRoot.hasSpotify || spotifyCard.signedIn || spotifyCard.signingIn
                        Layout.fillWidth: true
                        text: !pageRoot.hasSpotify ? "Not available"
                              : spotifyCard.signedIn ? "Signed in as " + spotifyManager.userName
                              : "Waiting for Spotify… finish signing in in the browser."
                    }

                    RowLayout {
                        visible: spotifyCard.idReady || spotifyCard.signedIn || spotifyCard.signingIn
                        Layout.fillWidth: true
                        spacing: App.Spacing.rowSpacing
                        SettingsButton {
                            visible: !spotifyCard.signedIn
                            text: spotifyCard.signingIn ? "Try Again" : "Connect Spotify"
                            onClicked: spotifyManager.signIn()
                            // SettingsButton has no height of its own
                            Layout.preferredHeight: App.Spacing.formElementHeight
                        }
                        // Signing out drops the saved session: two taps.
                        // Cancelling a sign-in that has not finished loses
                        // nothing and stays one.
                        ConfirmButton {
                            visible: spotifyCard.signedIn
                            label: "Sign Out"
                            confirmLabel: "Confirm sign out"
                            onConfirmed: spotifyManager.signOut()
                            Layout.preferredHeight: App.Spacing.formElementHeight
                        }
                        SettingsButton {
                            visible: !spotifyCard.signedIn && spotifyCard.signingIn
                            text: "Cancel"
                            onClicked: spotifyManager.signOut()
                            Layout.preferredHeight: App.Spacing.formElementHeight
                        }
                        SettingsButton {
                            visible: spotifyCard.builtInId && !spotifyCard.useOwnId && !spotifyCard.signedIn
                                     && !spotifyCard.signingIn
                            text: "Use My Own Client ID"
                            buttonColor: App.Style.hoverColor
                            onClicked: spotifyCard.useOwnId = true
                            Layout.preferredHeight: App.Spacing.formElementHeight
                        }
                        Item { Layout.fillWidth: true }
                    }

                    SettingDescription {
                        visible: pageRoot.hasSpotify && spotifyManager.status !== ""
                        text: pageRoot.hasSpotify ? spotifyManager.status : ""
                        color: App.Style.statusDanger
                    }
                }
            }
        }
    }

    // ── Stacked SettingsCards, for a host that leaves renderCards on (none does now).
    //    Sidebar layout sets pageRoot.visible = false and renders the tile grid + popup instead.
    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: App.Spacing.sectionSpacing

        Repeater {
            model: pageRoot.renderCards ? pageRoot.tileModel : []

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
