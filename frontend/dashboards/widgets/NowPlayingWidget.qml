// NowPlayingWidget.qml
//
// Dashboard "Now Playing" card: album art + title + artist + progress bar.
// Source-aware — mirrors the app-wide rule (the phone while
// phoneMirrorManager.mediaActive, else settingsManager.mediaSource ==
// "spotify" AND spotifyManager connected → Spotify, else local media), and
// re-resolves whenever the source, connection, or track changes.
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []), it
// reads mediaManager / spotifyManager directly. Display-only — transport
// buttons live in MediaControlsWidget.

import QtQuick 2.15
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Features ────────────────────────────────────────────────────
    property bool showArt: true
    property bool showArtist: true
    property bool showProgress: true

    // ── Style ───────────────────────────────────────────────────────
    property color titleColor: App.Style.obdValueColor
    property color artistColor: App.Style.obdLabelColor
    property color progressColor: App.Style.obdBarColor

    // ── Now-playing state ───────────────────────────────────────────
    property bool _useSpotify: false
    property bool _usePhone: false
    property string trackTitle: ""
    property string trackArtist: ""
    property string artSource: ""
    property real position: 0
    property real duration: 0

    function _computeUseSpotify() {
        return typeof settingsManager !== "undefined" && settingsManager
            && settingsManager.mediaSource === "spotify"
            && typeof spotifyManager !== "undefined" && spotifyManager
            && spotifyManager.is_connected()
    }

    function _computeUsePhone() {
        return typeof phoneMirrorManager !== "undefined" && phoneMirrorManager !== null
            && phoneMirrorManager.mediaActive
    }

    function refresh() {
        _usePhone = _computeUsePhone()
        _useSpotify = !_usePhone && _computeUseSpotify()
        if (_usePhone) {
            trackTitle = phoneMirrorManager.mediaAvailable ? phoneMirrorManager.mediaTitle : "Phone"
            trackArtist = phoneMirrorManager.mediaAvailable ? phoneMirrorManager.mediaArtist : "Nothing playing"
            artSource = phoneMirrorManager.mediaArtUrl
            position = phoneMirrorManager.mediaPosition()
            duration = phoneMirrorManager.mediaDuration
        } else if (_useSpotify) {
            trackTitle = spotifyManager.get_current_track_name() || ""
            trackArtist = spotifyManager.get_current_artist() || ""
            artSource = spotifyManager.get_current_album_art() || ""
            position = spotifyManager.get_position()
            duration = spotifyManager.get_duration()
        } else if (typeof mediaManager !== "undefined" && mediaManager) {
            var f = mediaManager.get_current_file()
            trackTitle = f ? mediaManager.get_display_name(f) : ""
            trackArtist = f ? mediaManager.get_band(f) : ""
            artSource = f ? (mediaManager.get_album_art(f) || "") : ""
            position = mediaManager.get_position()
            duration = mediaManager.get_duration()
        } else {
            trackTitle = ""; trackArtist = ""; artSource = ""
            position = 0; duration = 0
        }
    }

    Component.onCompleted: refresh()

    Connections {
        target: (typeof mediaManager !== "undefined" && mediaManager) ? mediaManager : null
        ignoreUnknownSignals: true
        function onCurrentMediaChanged(filename) { if (!root._useSpotify && !root._usePhone) root.refresh() }
        function onPositionChanged(position) { if (!root._useSpotify && !root._usePhone) root.position = position }
        function onDurationChanged(duration) { if (!root._useSpotify && !root._usePhone) root.duration = duration }
    }

    Connections {
        target: (typeof spotifyManager !== "undefined" && spotifyManager) ? spotifyManager : null
        ignoreUnknownSignals: true
        function onCurrentTrackChanged(title, artist, album, artUrl) {
            if (!root._useSpotify) return
            root.trackTitle = title || ""
            root.trackArtist = artist || ""
            root.artSource = artUrl || ""
        }
        function onPositionChanged(position) { if (root._useSpotify) root.position = position }
        function onDurationChanged(duration) { if (root._useSpotify) root.duration = duration }
        function onConnectionStateChanged(connected) { root.refresh() }
    }

    Connections {
        target: (typeof phoneMirrorManager !== "undefined" && phoneMirrorManager) ? phoneMirrorManager : null
        ignoreUnknownSignals: true
        function onMediaActiveChanged(active) { root.refresh() }
        function onMediaChanged() { if (root._usePhone) root.refresh() }
        function onMediaArtChanged() { if (root._usePhone) root.artSource = phoneMirrorManager.mediaArtUrl }
    }

    // The phone does not stream its position: read the extrapolated one
    Timer {
        interval: 500
        repeat: true
        running: root._usePhone && root.visible && root.showProgress && phoneMirrorManager.mediaPlaying
        onTriggered: root.position = phoneMirrorManager.mediaPosition()
    }

    Connections {
        target: (typeof settingsManager !== "undefined" && settingsManager) ? settingsManager : null
        ignoreUnknownSignals: true
        function onMediaSourceChanged(source) { root.refresh() }
    }

    // ── Layout ──────────────────────────────────────────────────────
    // Shrink typography with the cell, same convention as the gauges.
    readonly property real _fontFit: Math.max(0.1, Math.min(1,
        height / App.Spacing.dp(90), width / App.Spacing.dp(200)))
    readonly property real _pad: App.Spacing.dp(6)
    readonly property real _artSize: Math.min(height - 2 * _pad, width * 0.4)

    // Album art
    Rectangle {
        id: artFrame
        visible: root.showArt
        x: root._pad
        anchors.verticalCenter: parent.verticalCenter
        width: root._artSize
        height: root._artSize
        radius: Math.max(2, root._artSize * 0.08)
        color: Qt.darker(App.Style.obdBoxBackground, 1.2)
        clip: true

        Image {
            anchors.fill: parent
            source: root.artSource !== ""
                    ? root.artSource
                    : Qt.resolvedUrl("../../assets/missing_art.png")
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
    }

    // Title / artist / progress
    Item {
        anchors.left: root.showArt ? artFrame.right : parent.left
        anchors.leftMargin: root.showArt ? App.Spacing.dp(10) : root._pad
        anchors.right: parent.right
        anchors.rightMargin: root._pad
        anchors.verticalCenter: parent.verticalCenter
        height: textColumn.implicitHeight

        Column {
            id: textColumn
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: App.Spacing.dp(3)

            Text {
                width: parent.width
                text: root.trackTitle !== "" ? root.trackTitle : "Nothing playing"
                color: root.trackTitle !== "" ? root.titleColor : root.artistColor
                font.bold: true
                font.family: App.Style.fontFamily
                font.pixelSize: Math.max(1, App.Spacing.overallText * 1.05 * root._fontFit)
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.showArtist && root.trackArtist !== ""
                text: root.trackArtist
                color: root.artistColor
                font.family: App.Style.fontFamily
                font.pixelSize: Math.max(1, App.Spacing.overallText * 0.85 * root._fontFit)
                elide: Text.ElideRight
            }

            // Progress bar
            Rectangle {
                visible: root.showProgress
                width: parent.width
                height: Math.max(2, App.Spacing.dp(4) * root._fontFit)
                radius: height / 2
                color: Qt.darker(App.Style.obdBoxBackground, 1.35)

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * (root.duration > 0
                           ? Math.max(0, Math.min(1, root.position / root.duration)) : 0)
                    radius: parent.radius
                    color: root.progressColor

                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
        }
    }
}
