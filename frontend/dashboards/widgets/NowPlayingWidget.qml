// NowPlayingWidget.qml
//
// Dashboard "Now Playing" card: album art + title + artist + progress bar.
// Source-aware through nowPlaying (the phone while it has the media
// controls, else local media).
//
// Wider than tall it is a strip (art on the left, the text beside it);
// taller than wide a column (art on top, the text centred under it, the
// progress bar at the bottom with the times), which is Home's music panel.
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []), it
// reads nowPlaying. Transport buttons live in MediaControlsWidget; with
// `seekable` the progress bar takes a touch and seeks.

import QtQuick 2.15
import QtQuick.Effects
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Features ────────────────────────────────────────────────────
    property bool showArt: true
    property bool showArtist: true
    property bool showProgress: true
    property bool seekable: false         // touch the progress bar to seek
    property bool showBackground: false   // the art, blurred and darkened, behind the card

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
    // Follows the source only while on screen (Home stays alive behind every other page)
    property real position: 0
    Binding {
        target: root
        property: "position"
        value: root._np ? root._np.positionMs : 0
        when: root.visible
        restoreMode: Binding.RestoreNone
    }
    readonly property real duration: _np ? _np.durationMs : 0
    readonly property url _art: artSource !== "" ? artSource : Qt.resolvedUrl("../../assets/missing_art.png")

    function _time(ms) {
        if (typeof units !== "undefined" && units) return units.formatDuration(ms)
        var s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    // ── Layout ──────────────────────────────────────────────────────
    readonly property bool _vertical: height > width * 1.15
    // Shrink typography with the cell, same convention as the gauges.
    readonly property real _fontFit: _vertical
        ? Math.max(0.1, Math.min(1, width / App.Spacing.dp(200), height / App.Spacing.dp(260)))
        : Math.max(0.1, Math.min(1, height / App.Spacing.dp(90), width / App.Spacing.dp(200)))
    readonly property real _pad: App.Spacing.dp(_vertical || showBackground ? 12 : 6)
    readonly property real _titleSize: Math.max(1, App.Spacing.overallText * 1.05 * _fontFit)
    readonly property real _artistSize: Math.max(1, App.Spacing.overallText * 0.85 * _fontFit)
    // What the column keeps under the art: title, artist, the progress row
    readonly property real _textHeight: _titleSize * 1.3 + (showArtist ? _artistSize * 1.3 : 0)
                                        + (showProgress ? progressRow.height + App.Spacing.dp(10) : 0) + App.Spacing.dp(14)
    readonly property real _artSize: !showArt ? 0
        : _vertical ? Math.max(0, Math.min(width - 2 * _pad, height - 2 * _pad - _textHeight))
                    : Math.min(height - 2 * _pad, width * 0.4)

    // The widget keeps a touch on the progress bar to itself (DashboardRenderer)
    function octaveConsumesTap(x, y) {
        if (!seekable || !showProgress) return false
        var p = seekArea.mapToItem(root, 0, 0)
        return x >= p.x && x <= p.x + seekArea.width && y >= p.y && y <= p.y + seekArea.height
    }

    // Blurred art behind the card
    Loader {
        anchors.fill: parent
        active: root.showBackground
        sourceComponent: Item {
            clip: true
            Image {
                anchors.fill: parent
                source: root._art
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                layer.enabled: status === Image.Ready
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 64
                    blur: (typeof settingsManager !== "undefined" && settingsManager)
                          ? Math.min(1.0, settingsManager.backgroundBlurRadius / 64) : 0.625
                }
            }
            Rectangle { anchors.fill: parent; color: "#C0000000" }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: App.Spacing.dp(8)
                border.color: Qt.darker(root.progressColor, 1.4)
                border.width: 1
            }
        }
    }

    // Album art
    Rectangle {
        id: artFrame
        visible: root.showArt && root._artSize > 0
        x: root._vertical ? (root.width - width) / 2 : root._pad
        y: root._vertical ? root._pad : (root.height - height) / 2
        width: root._artSize
        height: root._artSize
        radius: Math.max(2, root._artSize * 0.08)
        color: Qt.darker(App.Style.obdBoxBackground, 1.2)
        clip: true

        Image {
            anchors.fill: parent
            source: root._art
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
    }

    // Title / artist
    Column {
        id: textColumn
        readonly property real leftEdge: (!root._vertical && root.showArt) ? artFrame.x + artFrame.width + App.Spacing.dp(10) : root._pad
        x: leftEdge
        width: Math.max(0, root.width - leftEdge - root._pad)
        y: root._vertical
           ? (root.showArt ? artFrame.y + artFrame.height + App.Spacing.dp(10) : root._pad)
           : (root.height - (height + (progressRow.visible ? progressRow.height + spacing : 0))) / 2
        spacing: App.Spacing.dp(3)

        Text {
            width: parent.width
            horizontalAlignment: root._vertical ? Text.AlignHCenter : Text.AlignLeft
            text: root.trackTitle !== "" ? root.trackTitle : "Nothing playing"
            color: root.trackTitle !== "" ? root.titleColor : root.artistColor
            font.bold: true
            font.family: App.Style.fontFamily
            font.pixelSize: root._titleSize
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            horizontalAlignment: root._vertical ? Text.AlignHCenter : Text.AlignLeft
            visible: root.showArtist && root.trackArtist !== ""
            text: root.trackArtist
            color: root.artistColor
            font.family: App.Style.fontFamily
            font.pixelSize: root._artistSize
            elide: Text.ElideRight
        }
    }

    // Progress: the bar, with the times beside it in the column and when seekable
    Item {
        id: progressRow
        visible: root.showProgress
        readonly property bool times: root._vertical || root.seekable
        x: textColumn.x
        width: textColumn.width
        height: Math.max(barTrack.height, times ? positionLabel.implicitHeight : 0)
        y: root._vertical ? root.height - root._pad - height
                          : textColumn.y + textColumn.height + textColumn.spacing

        Text {
            id: positionLabel
            visible: progressRow.times
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root._time(seekArea.pressed ? seekArea.fraction * root.duration : root.position)
            color: root.artistColor
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.7 * root._fontFit)
        }
        Text {
            id: durationLabel
            visible: progressRow.times
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root._time(root.duration)
            color: root.artistColor
            font.family: positionLabel.font.family
            font.pixelSize: positionLabel.font.pixelSize
        }

        Rectangle {
            id: barTrack
            objectName: "nowPlayingProgress"
            readonly property real inset: progressRow.times
                ? Math.max(positionLabel.implicitWidth, durationLabel.implicitWidth) + App.Spacing.dp(8) : 0
            x: inset
            width: Math.max(0, parent.width - 2 * inset)
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(2, App.Spacing.dp(4) * root._fontFit)
            radius: height / 2
            color: Qt.darker(App.Style.obdBoxBackground, 1.35)

            readonly property real shown: seekArea.pressed ? seekArea.fraction
                : (root.duration > 0 ? Math.max(0, Math.min(1, root.position / root.duration)) : 0)

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * barTrack.shown
                radius: parent.radius
                color: root.progressColor

                Behavior on width { enabled: !seekArea.pressed; NumberAnimation { duration: 200 } }
            }

            // Seek handle
            Rectangle {
                visible: root.seekable && root.duration > 0
                width: Math.max(barTrack.height * 2.5, App.Spacing.dp(10) * root._fontFit)
                height: width
                radius: width / 2
                x: barTrack.width * barTrack.shown - width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: seekArea.pressed ? root.progressColor : root.titleColor
            }

            // A finger-sized target around the thin bar
            MouseArea {
                id: seekArea
                objectName: "nowPlayingSeek"
                enabled: root.seekable && root.duration > 0
                visible: enabled
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: Math.max(parent.height, App.Spacing.dp(36))
                preventStealing: true
                property real fraction: 0
                function _at(x) { return Math.max(0, Math.min(1, x / width)) }
                onPressed: function(mouse) { fraction = _at(mouse.x) }
                onPositionChanged: function(mouse) { fraction = _at(mouse.x) }
                onReleased: if (root._np) root._np.seek(Math.round(fraction * root.duration))
            }
        }
    }
}
