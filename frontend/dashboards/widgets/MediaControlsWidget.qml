// MediaControlsWidget.qml
//
// Dashboard transport controls: previous / play-pause / next, driving
// whichever media source is active through nowPlaying (the phone while it
// has the media controls, else local media).
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []).
// Interactive in the rendered dashboard; inside the editor canvas the
// drag/select MouseArea sits on top, so taps there select the cell
// instead of skipping tracks — which is what you want while editing.

import QtQuick 2.15
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Style ───────────────────────────────────────────────────────
    property color buttonColor: Qt.darker(App.Style.obdBoxBackground, 1.15)
    property color glyphColor: App.Style.obdValueColor
    property color accentColor: App.Style.obdBarColor

    // ── Playback state ──────────────────────────────────────────────
    // nowPlaying (C++) routes the controls to whichever source has them
    readonly property var _np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    readonly property bool playing: !!_np && _np.playing

    // ── Layout ──────────────────────────────────────────────────────
    readonly property real _btn:
        Math.min(height * 0.8, (width - App.Spacing.dp(24)) / 3.4)

    Row {
        anchors.centerIn: parent
        spacing: Math.max(App.Spacing.dp(6), root._btn * 0.2)

        Repeater {
            model: [
                { "action": "prev",   "glyph": "◀◀", "big": false },
                { "action": "toggle", "glyph": "",   "big": true },
                { "action": "next",   "glyph": "▶▶", "big": false }
            ]
            delegate: Rectangle {
                readonly property real _d: root._btn * (modelData.big ? 1.0 : 0.78)
                anchors.verticalCenter: parent.verticalCenter
                width: _d; height: _d; radius: _d / 2
                color: btnMouse.pressed ? Qt.lighter(root.buttonColor, 1.3) : root.buttonColor
                border.color: modelData.big ? root.accentColor
                                            : Qt.darker(App.Style.obdBarColor, 1.5)
                border.width: 1
                scale: btnMouse.pressed ? 0.92 : 1.0
                Behavior on scale { NumberAnimation { duration: 100 } }

                Text {
                    anchors.centerIn: parent
                    text: modelData.big ? (root.playing ? "❚❚" : "▶") : modelData.glyph
                    color: modelData.big ? root.accentColor : root.glyphColor
                    font.bold: true
                    font.family: App.Style.fontFamily
                    font.pixelSize: Math.max(1, parent.width * (modelData.big ? 0.34 : 0.3))
                }

                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    onClicked: {
                        if (!root._np) return
                        if (modelData.action === "prev") root._np.previous()
                        else if (modelData.action === "next") root._np.next()
                        else root._np.togglePlay()
                    }
                }
            }
        }
    }
}
