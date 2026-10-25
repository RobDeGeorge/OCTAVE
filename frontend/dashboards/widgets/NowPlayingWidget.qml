// NowPlayingWidget.qml
//
// Dashboard "Now Playing" card: album art + title + artist + progress bar.
// Source-aware through nowPlaying (the phone while it has the media
// controls, else local media).
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []), it
// reads nowPlaying. Display-only — transport
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
    // nowPlaying (C++) follows whichever source has the media controls and
    // owns the phone's position clock; this only picks the wording
    readonly property var _np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    readonly property bool _remote: !!_np && _np.source !== "local"   // the phone or Android Auto
    readonly property string trackTitle: !_np ? "" : (_np.hasTrack ? _np.title : (_remote ? (_np.source === "androidauto" ? "Android Auto" : "Phone") : ""))
    readonly property string trackArtist: !_np ? "" : (_np.hasTrack ? _np.artist : (_remote ? "Nothing playing" : ""))
    readonly property string artSource: _np ? _np.artUrl : ""
    readonly property real position: _np ? _np.positionMs : 0
    readonly property real duration: _np ? _np.durationMs : 0

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
