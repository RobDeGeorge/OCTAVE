// MusicEmptyPrompt.qml
//
// What a music screen shows while there is no music: "No music yet", what
// to do about it, and the buttons that do it (pick the library folder, open
// Spotify, connect a phone). One component, so Home's music card, the Now
// Playing screen and the library list say and offer the same thing.
//
//   App.MusicEmptyPrompt {
//       anchors.centerIn: parent
//       width: parent.width - dp(24)
//       visible: page.musicEmpty
//       stackView: page.stackView; mainWindow: page.mainWindow
//   }
import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import "." as App

Column {
    id: prompt
    objectName: "musicEmptyPrompt"

    // Local dp wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property StackView stackView
    property ApplicationWindow mainWindow
    property string fontFamily: App.Style.fontFamily

    readonly property bool spotifyOffered: typeof spotifyManager !== "undefined" && !!spotifyManager
    readonly property bool phoneOffered: (!!settingsManager && settingsManager.phoneMirrorEnabled)
                                         || (typeof airPlayManager !== "undefined" && !!airPlayManager
                                             && airPlayManager.available && airPlayManager.enabled)
    function openPhone() {
        if (!stackView) return
        stackView.openPage(settingsManager && settingsManager.phoneMirrorEnabled
                           ? "PhoneMirrorView.qml" : "AirPlayView.qml")
    }

    // Why a picked folder wasn't taken (instead of a library that stays empty)
    property string folderError: ""
    FolderDialog {
        id: folderDialog
        title: "Select Music Library Folder"
        onAccepted: {
            if (!settingsManager) return
            prompt.folderError = settingsManager.save_media_folder_url(selectedFolder)
                ? "" : "That folder can't be used as the music library: pick one on the device's storage or SD card."
        }
    }

    // Outlined accent button, the look of OBDConnectButton
    component PromptButton: Rectangle {
        id: promptButton
        property string text: ""
        signal clicked()
        height: App.Spacing.bottomBarNavButtonHeight
        width: Math.min(promptLabel.implicitWidth + prompt.dp(32), prompt.width)
        radius: prompt.dpMin(8, 2)
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
            width: Math.min(implicitWidth, parent.width - prompt.dp(12))
            elide: Text.ElideRight
            text: promptButton.text
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText
            font.bold: true
            font.family: prompt.fontFamily
        }
        MouseArea {
            id: promptMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: promptButton.clicked()
        }
    }

    spacing: dp(16)

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: "No music yet"
        color: App.Style.metadataColor
        font.pixelSize: App.Spacing.overallText * 1.2
        font.bold: true
        font.family: prompt.fontFamily
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: prompt.phoneOffered && prompt.spotifyOffered ? "Pick a music folder, or connect your phone or Spotify"
              : prompt.phoneOffered ? "Pick a music folder or connect your phone"
              : prompt.spotifyOffered ? "Pick a music folder or connect Spotify"
              : "Pick the folder your music is in"
        color: App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText * 0.9
        font.family: prompt.fontFamily
    }
    // Side by side when both fit: Home's music panel is a third of the
    // screen, and narrower still at a large UI scale
    Grid {
        anchors.horizontalCenter: parent.horizontalCenter
        columns: browseButton.width + spotifyButton.width + spacing <= parent.width ? 2 : 1
        spacing: prompt.dp(12)
        horizontalItemAlignment: Grid.AlignHCenter

        PromptButton {
            id: browseButton
            objectName: "musicPromptBrowseButton"
            text: "Browse Music"
            // the browser demo opens the browser's own folder picker instead
            onClicked: if (typeof mediaManager === "undefined" || !mediaManager
                           || !mediaManager.pickBrowserLibrary()) folderDialog.open()
        }
        PromptButton {
            id: spotifyButton
            objectName: "musicPromptSpotifyButton"
            visible: prompt.spotifyOffered
            text: "Spotify"
            onClicked: if (prompt.mainWindow) prompt.mainWindow.openSpotify()
        }
    }
    PromptButton {
        objectName: "musicPromptPhoneButton"
        visible: prompt.phoneOffered
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Connect Phone"
        onClicked: prompt.openPhone()
    }
    Text {
        width: parent.width
        visible: prompt.folderError !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: prompt.folderError
        color: App.Style.statusDanger
        font.pixelSize: App.Spacing.overallText * 0.8
        font.family: prompt.fontFamily
    }
}
