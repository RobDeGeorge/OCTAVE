// ErrorToast.qml — the one app-wide notice, for errors the driver should
// hear about while on another page (OBD link lost, a failed download).
// Errors otherwise stay on their own pages and in the logs: nothing chatty
// in the driver's peripheral vision. Top centre (the bottom belongs to the
// BottomBar), fade only, auto-dismiss, tap to dismiss. Messages queue; a
// message equal to the one showing (or the last queued) is dropped.
//
// Main.qml instantiates it once as the window's last child.

import QtQuick 2.15
import "." as App

Item {
    id: toast
    objectName: "errorToast"

    // How long a message stays up
    property int durationMs: 8000

    // Read by tests and the dev command server
    readonly property string currentMessage: _current
    readonly property int queuedCount: _queue.length

    property string _current: ""
    property var _queue: []

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    function show(message) {
        if (!message)
            return
        var last = _queue.length > 0 ? _queue[_queue.length - 1] : _current
        if (message === last)
            return
        if (_current === "") {
            _current = message
            dismissTimer.restart()
        } else {
            _queue = _queue.concat([message])
        }
    }

    function dismiss() {
        dismissTimer.stop()
        if (_queue.length > 0) {
            _current = _queue[0]
            _queue = _queue.slice(1)
            dismissTimer.restart()
        } else {
            _current = ""
        }
    }

    anchors.top: parent ? parent.top : undefined
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.topMargin: dp(16)
    width: Math.min(parent ? parent.width - dp(40) : dp(600), label.implicitWidth + dp(48))
    height: label.implicitHeight + dp(24)
    z: 1000
    visible: opacity > 0
    opacity: _current !== "" ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Timer {
        id: dismissTimer
        interval: toast.durationMs
        onTriggered: toast.dismiss()
    }

    Rectangle {
        anchors.fill: parent
        radius: toast.dp(10)
        color: App.Style.statusDanger
        opacity: 0.95
    }

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, (toast.parent ? toast.parent.width : toast.dp(600)) - toast.dp(88))
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: toast._current
        color: "white"
        font.pixelSize: App.Spacing.overallText * 1.1
        font.bold: true
        font.family: App.Style.fontFamily
    }

    MouseArea {
        anchors.fill: parent
        onClicked: toast.dismiss()
    }
}
