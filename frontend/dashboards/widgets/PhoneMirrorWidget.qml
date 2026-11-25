// PhoneMirrorWidget.qml
//
// The phone's screen in a dashboard cell, for Home: live while phone
// mirroring runs (touches go through to the phone). Since 2026-11-25 this is
// the only place the phone is shown (no bottom-bar button, nothing opens the
// full-screen PhoneMirrorView) while the flow is worked out; Connect Phone
// starts the session here. With Phone Mirror on, it also starts the
// session itself when it comes on screen (unless the user closed mirroring
// with the full-screen page's X in this run: mainWindow.phoneMirrorStopped),
// keeps the manager's phone retries running while shown, and reports its
// size so an "auto" virtual display is drawn for this window (the full-screen
// page reports its own when shown: the manager restarts the session at the
// new size).
//
// The manager has one video sink: this widget takes it while it is on screen
// and mirroring runs, and gives it up when hidden (PhoneMirrorView takes it
// back whenever it is shown).
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []).
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window
import QtMultimedia
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID
    // Every tap is the widget's own (the phone, or its buttons)
    function octaveConsumesTap(x, y) { return true }

    // ── Features ────────────────────────────────────────────────────
    property bool showFrame: true    // the reading cards' box around it

    // ── Style ───────────────────────────────────────────────────────
    property color frameColor: App.Style.obdBarColor
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    function dp(size) { return App.Spacing.dp(size) }

    readonly property bool _hasManager: typeof phoneMirrorManager !== "undefined" && !!phoneMirrorManager
    readonly property bool _available: _hasManager && phoneMirrorManager.nativeAvailable
    readonly property bool _enabled: typeof settingsManager !== "undefined" && !!settingsManager
                                     && settingsManager.phoneMirrorEnabled
    readonly property string _session: _hasManager ? phoneMirrorManager.sessionState : ""
    // The session state as well: it always says when the stream is up
    readonly property bool live: _available && (phoneMirrorManager.isRunning || _session === "streaming"
                                                || _session === "reconnecting")
    readonly property string _device: _hasManager ? phoneMirrorManager.deviceState : ""

    // ── Starting the session from Home ──
    readonly property bool _shown: visible && _available && _enabled
    function _userStopped() {
        var w = _window()
        return !!w && w.phoneMirrorStopped === true
    }
    function _reportSize() {
        if (!_shown || width < 64 || height < 64) return
        var ratio = Screen.devicePixelRatio || 1
        phoneMirrorManager.setViewSize(Math.round(width * ratio), Math.round(height * ratio))
    }
    function _bringUp() {
        if (!_shown) return
        _reportSize()
        // After the full-screen page's own deactivation, which turns retries off
        Qt.callLater(function() { if (root._shown) phoneMirrorManager.setViewActive(true) })
        if (!_userStopped() && (_session === "idle" || _session === ""))
            phoneMirrorManager.startScrcpy()
    }
    on_ShownChanged: _bringUp()
    onWidthChanged: _reportSize()
    onHeightChanged: _reportSize()
    readonly property real _fontFit: Math.max(0.1, Math.min(1, height / dp(140), width / dp(180)))

    // ── The video sink: ours while shown and live ──
    readonly property bool _wantSink: live && visible
    on_WantSinkChanged: _syncSink()
    Component.onCompleted: { _syncSink(); _bringUp() }
    Component.onDestruction: _releaseSink()
    function _syncSink() {
        if (!_hasManager) return
        if (_wantSink) phoneMirrorManager.videoSink = video.videoSink
        else _releaseSink()
    }
    // Only when it is still ours: the full-screen page may have taken it
    function _releaseSink() {
        if (_hasManager && phoneMirrorManager.videoSink === video.videoSink)
            phoneMirrorManager.videoSink = null
    }

    readonly property bool _connecting: _session === "starting" || _session === "reconnecting"
    readonly property bool _needsSetup: _session === "failed" || _device === "no-adb"
    function _window() { return ApplicationWindow.window }
    // Connect Phone: start here, as a user's own request (clears a stop)
    function connect() {
        if (!_hasManager) return
        var w = _window()
        if (w && w.phoneMirrorStopped !== undefined) w.phoneMirrorStopped = false
        _reportSize()
        phoneMirrorManager.setViewActive(true)
        phoneMirrorManager.startScrcpy()
    }
    function openSettings() {
        var w = _window()
        if (w && typeof w.navigateToSettings === "function") w.navigateToSettings("phoneSettings", "phone_mirror")
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: root.dp(8)
        color: root.live ? "black" : (root.showFrame ? App.Style.obdBoxBackground : "transparent")
        border.color: root.showFrame ? Qt.darker(root.frameColor, 1.4) : "transparent"
        border.width: 1
    }

    // ── Live ──
    Item {
        anchors.fill: parent
        anchors.margins: 1
        visible: root.live
        clip: true

        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
        }

        // Touches map through the letterboxed picture to 0..1 of the
        // phone's display, as on the full-screen page; one that starts on
        // the bars is ignored for its whole life
        MultiPointTouchArea {
            objectName: "phoneMirrorWidgetTouch"
            anchors.fill: parent
            enabled: root.live
            mouseEnabled: true
            minimumTouchPoints: 1
            maximumTouchPoints: 10
            property var _ignored: ({})

            function rawPos(p) {
                var r = video.contentRect
                if (r.width <= 0 || r.height <= 0) return null
                return { x: (p.x - r.x) / r.width, y: (p.y - r.y) / r.height }
            }
            function send(points, action) {
                for (var i = 0; i < points.length; ++i) {
                    var p = points[i], rp = rawPos(p)
                    if (action === 0)
                        _ignored[p.pointId] = !rp || rp.x < 0 || rp.x > 1 || rp.y < 0 || rp.y > 1
                    if (_ignored[p.pointId]) {
                        if (action === 1) delete _ignored[p.pointId]
                        continue
                    }
                    if (rp && root._hasManager)
                        phoneMirrorManager.injectTouch(p.pointId, action,
                                                       Math.max(0, Math.min(1, rp.x)),
                                                       Math.max(0, Math.min(1, rp.y)))
                }
            }
            onPressed: function(points) { send(points, 0) }
            onUpdated: function(points) { send(points, 2) }
            onReleased: function(points) { send(points, 1) }
            onCanceled: function(points) { send(points, 1) }
        }
    }

    // ── Not mirroring ──
    Column {
        visible: !root.live
        anchors.centerIn: parent
        width: parent.width - root.dp(16)
        spacing: root.dp(10) * root._fontFit

        // A phone outline
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.dp(34) * root._fontFit
            height: width * 1.8
            radius: width * 0.18
            color: "transparent"
            border.color: root.labelColor
            border.width: Math.max(1, root.dp(2) * root._fontFit)
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: parent.height * 0.07
                width: parent.width * 0.3
                height: Math.max(1, parent.border.width)
                radius: height / 2
                color: root.labelColor
            }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: !root._available ? "Phone mirroring isn't available here"
                  : !root._enabled ? "Phone Mirror is off"
                  : root._connecting ? "Connecting to your phone…"
                  : root._device === "unauthorized" ? "Allow USB debugging on your phone"
                  : root._session === "failed" ? "Phone mirroring needs setting up"
                  : "No phone connected"
            color: root.valueColor
            font.family: App.Style.fontFamily
            font.bold: true
            font.pixelSize: Math.max(1, App.Spacing.overallText * 1.0 * root._fontFit)
        }
        // Outlined accent button, the look of Connect Music / Connect Vehicle
        Rectangle {
            id: connectButton
            objectName: "phoneMirrorWidgetConnect"
            visible: root._available && !root._connecting
            anchors.horizontalCenter: parent.horizontalCenter
            height: App.Spacing.bottomBarNavButtonHeight * Math.max(0.6, root._fontFit)
            width: Math.min(connectLabel.implicitWidth + root.dp(32), parent.width)
            radius: Math.max(2, root.dp(8))
            color: "transparent"
            border.color: App.Style.accent
            border.width: 1
            scale: connectMouse.pressed ? 0.9 : 1.0
            Behavior on scale {
                NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
            }
            Text {
                id: connectLabel
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - root.dp(12))
                elide: Text.ElideRight
                // Setting up and pairing a new phone (QR code) are in Settings > Phone
                text: !root._enabled ? "Turn On" : root._needsSetup ? "Set Up" : "Connect Phone"
                color: App.Style.accent
                font.pixelSize: Math.max(1, App.Spacing.overallText * Math.max(0.7, root._fontFit))
                font.bold: true
                font.family: App.Style.fontFamily
            }
            MouseArea {
                id: connectMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: (!root._enabled || root._needsSetup) ? root.openSettings() : root.connect()
            }
        }
    }
}
