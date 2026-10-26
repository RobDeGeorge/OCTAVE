import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import QtMultimedia
import "." as App

// An iPhone over AirPlay (airPlayManager): its mirrored screen, with taps sent
// back through OCTAVE's Bluetooth mouse; the setup steps while nothing mirrors.
// Opens by itself when an iPhone starts mirroring (Main.qml).
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: airPlayView
    objectName: "airPlayView"
    property StackView stackView
    property ApplicationWindow mainWindow
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    property string globalFont: App.Style.fontFamily
    readonly property var ap: typeof airPlayManager !== "undefined" ? airPlayManager : null
    readonly property string apState: ap ? ap.state : "unavailable"
    readonly property bool mirroring: ap ? ap.mirroring : false
    readonly property string touchState: ap ? ap.touchState : "unavailable"
    readonly property bool calibrating: ap ? ap.touchCalibrating : false

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // ── The mirrored screen ──
    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: airPlayView.mirroring
        Component.onCompleted: if (airPlayView.ap) airPlayView.ap.videoSink = videoSink
    }

    // Taps map through the letterboxed picture to 0..1 of the iPhone's
    // screen; one that starts on the bars is ignored for its whole life
    MultiPointTouchArea {
        objectName: "airPlayTouchArea"
        anchors.fill: parent
        enabled: airPlayView.mirroring
        mouseEnabled: true
        minimumTouchPoints: 1
        maximumTouchPoints: 5

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
                if (rp && airPlayView.ap)
                    airPlayView.ap.injectTouch(p.pointId, action,
                                               Math.max(0, Math.min(1, rp.x)),
                                               Math.max(0, Math.min(1, rp.y)))
            }
        }
        onPressed: function(points) { send(points, 0) }
        onUpdated: function(points) { send(points, 2) }
        onReleased: function(points) { send(points, 1) }
        onCanceled: function(points) { send(points, 1) }
    }

    // A mouse wheel on OCTAVE scrolls the iPhone
    WheelHandler {
        enabled: airPlayView.mirroring
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            if (!airPlayView.ap) return
            var dy = event.angleDelta.y > 0 ? -1 : (event.angleDelta.y < 0 ? 1 : 0)
            var dx = event.angleDelta.x > 0 ? -1 : (event.angleDelta.x < 0 ? 1 : 0)
            airPlayView.ap.scroll(dx, dy)
        }
    }

    // ── Calibration: the pointer sits at a known spot, the driver taps it ──
    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: dp(12)
        visible: airPlayView.mirroring && airPlayView.calibrating
        width: calibrateText.implicitWidth + dp(32)
        height: calibrateText.implicitHeight + dp(16)
        radius: dpMin(10, 2)
        color: "#DD000000"
        z: 5
        Text {
            id: calibrateText
            anchors.centerIn: parent
            text: "Tap the iPhone's pointer (the grey circle)"
            font.pixelSize: dp(16)
            font.family: airPlayView.globalFont
            color: "white"
        }
    }

    // ── While mirroring: touch status, calibrate, close ──
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: dp(10)
        spacing: dp(8)
        visible: airPlayView.mirroring
        z: 10

        Rectangle {
            visible: airPlayView.touchState !== "connected" && airPlayView.touchState !== "unavailable"
            height: dp(40)
            width: touchHint.implicitWidth + dp(24)
            radius: dpMin(20, 2)
            color: "#AA000000"
            Text {
                id: touchHint
                anchors.centerIn: parent
                text: airPlayView.ap && airPlayView.ap.touchPairing
                      ? "Pairing: tap \"" + airPlayView.ap.receiverName + " Touch\" in the iPhone's Bluetooth settings"
                      : "Touch isn't connected: tap to pair"
                font.pixelSize: dp(13)
                font.family: airPlayView.globalFont
                color: "white"
            }
            MouseArea {
                objectName: "airPlayTouchBadge"
                anchors.fill: parent
                onClicked: if (airPlayView.ap) airPlayView.ap.pairTouch(!airPlayView.ap.touchPairing)
            }
        }

        Rectangle {
            objectName: "airPlayCalibrate"
            visible: airPlayView.touchState === "connected"
            height: dp(40)
            width: calibrateLabel.implicitWidth + dp(24)
            radius: dpMin(20, 2)
            color: "#AA000000"
            Text {
                id: calibrateLabel
                anchors.centerIn: parent
                text: airPlayView.calibrating ? "Cancel" : "Calibrate touch"
                font.pixelSize: dp(13)
                font.family: airPlayView.globalFont
                color: "white"
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (!airPlayView.ap) return
                    if (airPlayView.calibrating) airPlayView.ap.cancelCalibration()
                    else airPlayView.ap.calibrateTouch()
                }
            }
        }

        // Leaves the page; the iPhone keeps mirroring until it stops
        Rectangle {
            width: dp(40)
            height: dp(40)
            radius: dpMin(20, 2)
            color: "#AA000000"
            Text {
                anchors.centerIn: parent
                text: "X"
                font.pixelSize: dp(20)
                font.bold: true
                color: "white"
            }
            MouseArea {
                objectName: "airPlayClose"
                anchors.fill: parent
                onClicked: stackView.pop()
            }
        }
    }

    // ── Setup: what the iPhone or OCTAVE still needs ──
    Flickable {
        id: setupFlick
        anchors.fill: parent
        visible: !airPlayView.mirroring
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, setupColumn.implicitHeight + dp(40))
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: setupColumn
            width: Math.min(setupFlick.width * 0.85, dp(680))
            x: (setupFlick.width - width) / 2
            y: Math.max(dp(20), (setupFlick.height - implicitHeight) / 2)
            spacing: dp(18)

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: airPlayView.apState === "pin" ? "Type this PIN on the iPhone"
                    : airPlayView.apState === "audio" ? "Playing from the iPhone"
                    : airPlayView.apState === "connected" ? "Connecting..."
                    : airPlayView.apState === "error" ? "AirPlay could not start"
                    : "iPhone"
                font.pixelSize: dp(26)
                font.bold: true
                font.family: airPlayView.globalFont
                color: airPlayView.apState === "error" ? App.Style.statusError : App.Style.primaryTextColor
                wrapMode: Text.WordWrap
            }

            // The PIN, big enough to read from the driver's seat
            Text {
                objectName: "airPlayPin"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: airPlayView.apState === "pin"
                text: airPlayView.ap ? airPlayView.ap.pin.split("").join(" ") : ""
                font.pixelSize: dp(72)
                font.bold: true
                font.family: airPlayView.globalFont
                color: App.Style.accent
            }

            // AirPlay audio: what is playing
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: airPlayView.apState === "audio" && text !== ""
                text: airPlayView.ap ? [airPlayView.ap.title, airPlayView.ap.artist].filter(function(s) { return s }).join(" - ") : ""
                font.pixelSize: dp(18)
                font.family: airPlayView.globalFont
                color: App.Style.primaryTextColor
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "airPlayStatus"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: airPlayView.ap ? airPlayView.ap.status : "This build of OCTAVE has no AirPlay receiver."
                visible: airPlayView.apState !== "pin"
                font.pixelSize: dp(16)
                font.family: airPlayView.globalFont
                color: App.Style.secondaryTextColor
                wrapMode: Text.WordWrap
            }

            Button {
                objectName: "airPlayTurnOn"
                Layout.alignment: Qt.AlignHCenter
                visible: airPlayView.apState === "off"
                text: "Turn on AirPlay"
                font.pixelSize: dp(18)
                font.family: airPlayView.globalFont
                onClicked: if (airPlayView.ap) airPlayView.ap.setEnabled(true)
                background: Rectangle {
                    implicitWidth: dp(220)
                    implicitHeight: dp(52)
                    radius: dpMin(10, 2)
                    color: parent.pressed ? Qt.darker(App.Style.accent, 1.3) : App.Style.accent
                }
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // How to connect
            Rectangle {
                Layout.fillWidth: true
                visible: airPlayView.apState === "waiting" || airPlayView.apState === "audio"
                implicitHeight: stepsColumn.implicitHeight + dp(28)
                radius: dpMin(10, 2)
                color: App.Style.cardBackground !== undefined ? App.Style.cardBackground : "#1a1a1a"
                border.color: App.Style.accent
                border.width: 1

                ColumnLayout {
                    id: stepsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: dp(14)
                    spacing: dp(8)

                    Text {
                        Layout.fillWidth: true
                        text: "Mirror the screen"
                        font.pixelSize: dp(17)
                        font.bold: true
                        font.family: airPlayView.globalFont
                        color: App.Style.primaryTextColor
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "1. Put the iPhone on the same Wi-Fi as OCTAVE"
                              + (airPlayView.ap && airPlayView.ap.addresses.length
                                 ? " (OCTAVE is at " + airPlayView.ap.addresses.join(", ") + ")." : ".")
                              + "\n2. Open Control Center and tap Screen Mirroring."
                              + "\n3. Pick \"" + (airPlayView.ap ? airPlayView.ap.receiverName : "OCTAVE") + "\". "
                              + "The first time, type the PIN OCTAVE shows."
                              + "\n\nFor music only, pick \"" + (airPlayView.ap ? airPlayView.ap.receiverName : "OCTAVE")
                              + "\" as the speaker in Control Center's music card instead."
                        font.pixelSize: dp(14)
                        font.family: airPlayView.globalFont
                        color: App.Style.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Touch through the Bluetooth mouse
            Rectangle {
                Layout.fillWidth: true
                visible: airPlayView.ap ? airPlayView.ap.touchAvailable && airPlayView.ap.enabled && airPlayView.ap.touchEnabled : false
                implicitHeight: touchColumn.implicitHeight + dp(28)
                radius: dpMin(10, 2)
                color: App.Style.cardBackground !== undefined ? App.Style.cardBackground : "#1a1a1a"
                border.color: airPlayView.touchState === "connected" ? App.Style.statusConnected : App.Style.hoverColor
                border.width: 1

                ColumnLayout {
                    id: touchColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: dp(14)
                    spacing: dp(8)

                    Text {
                        Layout.fillWidth: true
                        text: airPlayView.touchState === "connected"
                              ? "Touch: connected" + (airPlayView.ap.touchDevice ? " to " + airPlayView.ap.touchDevice : "")
                              : "Touch the iPhone from OCTAVE's screen"
                        font.pixelSize: dp(17)
                        font.bold: true
                        font.family: airPlayView.globalFont
                        color: airPlayView.touchState === "connected" ? App.Style.statusConnected : App.Style.primaryTextColor
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: airPlayView.touchState !== "connected"
                        text: "Once: tap Pair touch below, then on the iPhone"
                              + "\n1. Settings > Bluetooth: tap \"" + (airPlayView.ap ? airPlayView.ap.receiverName : "OCTAVE") + " Touch\"."
                              + "\n2. Settings > Accessibility > Touch > AssistiveTouch: turn it on."
                              + "\nAfter that it reconnects by itself."
                              + (airPlayView.ap && airPlayView.ap.touchStatus ? "\n\n" + airPlayView.ap.touchStatus : "")
                        font.pixelSize: dp(14)
                        font.family: airPlayView.globalFont
                        color: App.Style.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                    // New pairings are only taken for a few minutes after this
                    Button {
                        objectName: "airPlayPairTouch"
                        visible: airPlayView.touchState === "waiting"
                        text: airPlayView.ap && airPlayView.ap.touchPairing ? "Stop pairing" : "Pair touch"
                        font.pixelSize: dp(15)
                        font.family: airPlayView.globalFont
                        onClicked: if (airPlayView.ap) airPlayView.ap.pairTouch(!airPlayView.ap.touchPairing)
                        background: Rectangle {
                            implicitWidth: dp(160)
                            implicitHeight: dp(44)
                            radius: dpMin(8, 2)
                            color: "transparent"
                            border.color: App.Style.accent
                            border.width: 2
                        }
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: App.Style.primaryTextColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: airPlayView.touchState === "connected"
                        text: "If taps land in the wrong place, start mirroring and tap Calibrate touch."
                        font.pixelSize: dp(14)
                        font.family: airPlayView.globalFont
                        color: App.Style.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // Back (setup screen only; the mirrored screen has its own close)
    Button {
        z: 1
        visible: !airPlayView.mirroring
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: dp(15)
        text: "< Back"
        font.pixelSize: dp(16)
        font.family: airPlayView.globalFont
        background: Rectangle {
            color: parent.pressed ? App.Style.accent : "transparent"
            border.color: App.Style.accent
            border.width: 2
            radius: dpMin(8, 2)
        }
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: App.Style.primaryTextColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onClicked: stackView.pop()
    }
}
