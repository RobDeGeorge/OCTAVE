// MusicEmptyPrompt.qml
//
// What a music screen shows while there is no music: "No Music Loaded", what
// to do about it, and one Connect Music button whose sheet offers the sources
// (the library folder, Spotify, a phone). One component, so Home's music card,
// the Now Playing screen and the library list say and offer the same thing.
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
    // The phone is shown on Home only for now (PhoneMirrorWidget): go there
    // and start mirroring; an iPhone still opens the AirPlay page
    function openPhone() {
        if (settingsManager && settingsManager.phoneMirrorEnabled) {
            if (mainWindow) mainWindow.phoneMirrorStopped = false
            if (typeof phoneMirrorManager !== "undefined" && phoneMirrorManager) phoneMirrorManager.startScrcpy()
            if (mainWindow) mainWindow.navigateHome()
            return
        }
        if (stackView) stackView.openPage("AirPlayView.qml")
    }

    // The height the host can give, 0 for as much as it takes. In a short
    // space (Home's music cell in a portrait window is wide and low) the
    // explanation goes first, then the heading: the button always stays.
    property real maxHeight: 0
    readonly property bool _headingFits: maxHeight <= 0
                                         || heading.implicitHeight + spacing + connectButton.height <= maxHeight
    readonly property bool _hintFits: maxHeight <= 0
                                      || heading.implicitHeight + hint.implicitHeight + 2 * spacing + connectButton.height <= maxHeight

    function pickFolder() {
        // the browser demo opens the browser's own folder picker instead
        if (typeof mediaManager === "undefined" || !mediaManager || !mediaManager.pickBrowserLibrary())
            folderDialog.open()
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
        id: heading
        width: parent.width
        visible: prompt._headingFits
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: "No Music Loaded"
        color: App.Style.metadataColor
        font.pixelSize: App.Spacing.overallText * 1.2
        font.bold: true
        font.family: prompt.fontFamily
    }
    Text {
        id: hint
        width: parent.width
        visible: prompt._hintFits
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: prompt.phoneOffered && prompt.spotifyOffered ? "Play from a music folder, Spotify or your phone"
              : prompt.phoneOffered ? "Play from a music folder or your phone"
              : prompt.spotifyOffered ? "Play from a music folder or Spotify"
              : "Pick the folder your music is in"
        color: App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText * 0.9
        font.family: prompt.fontFamily
    }
    PromptButton {
        id: connectButton
        objectName: "musicPromptConnectButton"
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Connect Music"
        onClicked: sourceSheet.open()
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

    // Where the music comes from: one row per source (the ConnectSheet look
    // Connect Vehicle shares)
    App.ConnectSheet {
        id: sourceSheet
        objectName: "musicSourceSheet"
        title: "Connect Music"

        App.SheetRow {
            objectName: "musicSourceLocal"
            title: "Local Music"
            detail: "A folder of songs on this device"
            onPicked: { sourceSheet.close(); prompt.pickFolder() }
        }
        App.SheetRow {
            objectName: "musicSourceSpotify"
            visible: prompt.spotifyOffered
            title: "Spotify"
            detail: "Sign in and play your library"
            onPicked: { sourceSheet.close(); if (prompt.mainWindow) prompt.mainWindow.openSpotify() }
        }
        App.SheetRow {
            objectName: "musicSourcePhone"
            visible: prompt.phoneOffered
            title: "Phone"
            detail: "Mirror your phone and play from it"
            onPicked: { sourceSheet.close(); prompt.openPhone() }
        }
    }
}
