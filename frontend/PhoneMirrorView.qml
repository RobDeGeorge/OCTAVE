import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import QtMultimedia
import "." as App

// Phone mirror: OCTAVE's built-in scrcpy-protocol client streams the phone
// (or a virtual display on it) straight onto the VideoOutput below and
// forwards touch over the control socket. No external tools involved.
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: phoneMirrorView
    property StackView stackView
    property var mainWindow: null
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    property string globalFont: App.Style.fontFamily

    property bool mirrorRunning: false
    property int frameCounter: 0      // bumped on every frame; proof of life
    property bool launchFailed: false
    property string errorMessage: ""
    readonly property bool hasVideo: frameCounter >= 1

    // True when everything OCTAVE needs is present (decoder, server, adb), so
    // any failure is about the phone; the setup text is shown only otherwise.
    property bool setupOk: true
    // True when the last failure was the phone itself (unplugged, offline,
    // not authorized) — those are worth retrying automatically.
    property bool deviceError: false
    function refreshSetupOk() {
        setupOk = (phoneMirrorManager && phoneMirrorManager.environmentOk) ? phoneMirrorManager.environmentOk() : false
    }
    onLaunchFailedChanged: if (launchFailed) refreshSetupOk()

    StackView.onActivated: {
        console.log("PhoneMirrorView activated")
        if (phoneMirrorManager && phoneMirrorManager.isRunning)
            mirrorRunning = true
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Rectangle {
        id: phoneDisplay
        anchors.fill: parent
        color: "black"
        visible: mirrorRunning

        // Decoded frames land directly on this sink
        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            Component.onCompleted: if (phoneMirrorManager) phoneMirrorManager.videoSink = videoSink
        }

        // Real multitouch over the control socket. Mouse input arrives as a
        // single touch point. Coordinates map through the letterboxed
        // content rect to 0..1 of the mirrored display.
        MultiPointTouchArea {
            anchors.fill: parent
            enabled: mirrorRunning
            mouseEnabled: true
            minimumTouchPoints: 1
            maximumTouchPoints: 10

            function relPos(p) {
                var r = video.contentRect
                if (r.width <= 0 || r.height <= 0) return null
                var x = (p.x - r.x) / r.width, y = (p.y - r.y) / r.height
                return { x: Math.max(0, Math.min(1, x)), y: Math.max(0, Math.min(1, y)) }
            }
            function send(points, action) {
                for (var i = 0; i < points.length; ++i) {
                    var p = points[i], rp = relPos(p)
                    if (rp && phoneMirrorManager) phoneMirrorManager.injectTouch(p.pointId, action, rp.x, rp.y)
                }
            }
            onPressed: function(points) { send(points, 0) }
            onUpdated: function(points) { send(points, 2) }
            onReleased: function(points) { send(points, 1) }
            onCanceled: function(points) { send(points, 1) }
        }

        // Android navigation buttons (a virtual display has no gesture nav
        // bar the user can reach)
        // Only when mirroring the phone's own screen. A virtual display shows
        // system decorations, i.e. Samsung's DeX taskbar with its own
        // back/home/recents along the bottom, which this row would cover.
        // Sits on the bottom edge of the picture (not the letterbox band,
        // where a translucent button on black is invisible).
        Row {
            readonly property bool virtualDisplay: phoneMirrorManager ? phoneMirrorManager.activeDisplaySize !== "" : false
            readonly property rect content: video.contentRect
            y: (content.height > 0 ? content.y + content.height : parent.height) - height - dp(6)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: dp(24)
            visible: mirrorRunning && !virtualDisplay
            z: 10
            Repeater {
                model: [ { label: "◁", slot: "pressBack" }, { label: "○", slot: "pressHome" }, { label: "▢", slot: "pressAppSwitch" } ]
                Rectangle {
                    width: dp(44); height: dp(44); radius: dpMin(22, 2)
                    color: navMouse.pressed ? App.Style.accent : "#A0000000"
                    border.color: "#55FFFFFF"; border.width: 1
                    Text { anchors.centerIn: parent; text: modelData.label; color: "white"; font.pixelSize: dp(20) }
                    MouseArea { id: navMouse; anchors.fill: parent
                        onClicked: if (phoneMirrorManager) phoneMirrorManager[modelData.slot]() }
                }
            }
        }

        // Close button overlay
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: dp(10)
            width: dp(40)
            height: dp(40)
            radius: dpMin(20, 2)
            color: "#AA000000"
            z: 10

            Text {
                anchors.centerIn: parent
                text: "X"
                font.pixelSize: dp(20)
                font.bold: true
                color: "white"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (phoneMirrorManager) phoneMirrorManager.stopScrcpy()
                    mirrorRunning = false
                    stackView.pop()
                }
            }
        }

        Text {
            anchors.centerIn: parent
            text: "Connecting..."
            font.pixelSize: dp(24)
            font.family: phoneMirrorView.globalFont
            color: "white"
            visible: mirrorRunning && !hasVideo
            z: 5
        }

        // The user unlocked the phone in their hand: the mirror keeps working,
        // OCTAVE just stops blanking / waking it. Small pill, not an overlay.
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: dp(10)
            width: inUseLabel.implicitWidth + dp(28)
            height: dp(34)
            radius: dpMin(17, 2)
            color: "#CC000000"
            border.color: "#55FFFFFF"; border.width: 1
            visible: mirrorRunning && phoneMirrorManager && phoneMirrorManager.phoneInUse === true
            z: 9
            Text {
                id: inUseLabel
                anchors.centerIn: parent
                text: "Phone in use \u2014 tap to turn its screen back off"
                font.pixelSize: dp(14)
                font.family: phoneMirrorView.globalFont
                color: "white"
            }
            MouseArea {
                anchors.fill: parent
                onClicked: if (phoneMirrorManager) phoneMirrorManager.resumeMirroring()
            }
        }

        // The phone was locked (power button) mid-session: its virtual display
        // goes black and drops touch until it is awake again. The manager
        // wakes it; this tells the user what the black frame is and lets
        // them retry with a tap.
        Rectangle {
            anchors.fill: parent
            color: "#AA000000"
            visible: mirrorRunning && phoneMirrorManager && phoneMirrorManager.phoneAsleep === true
            z: 8
            Column {
                anchors.centerIn: parent
                spacing: dp(12)
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Phone is asleep"
                    font.pixelSize: dp(24)
                    font.family: phoneMirrorView.globalFont
                    color: "white"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Waking it up… tap to retry"
                    font.pixelSize: dp(16)
                    font.family: phoneMirrorView.globalFont
                    color: "#CCFFFFFF"
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: if (phoneMirrorManager) phoneMirrorManager.wakePhone()
            }
        }
    }

    // Error/Setup screen - shown when the mirror is not running
    Item {
        anchors.fill: parent
        visible: !mirrorRunning

        Button {
            id: backButton
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: dp(15)
            text: "< Back"
            font.pixelSize: dp(16)
            font.family: phoneMirrorView.globalFont

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

            onClicked: {
                if (phoneMirrorManager) phoneMirrorManager.stopScrcpy()
                stackView.pop()
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: dp(25)
            width: parent.width * 0.8

            // Loading state
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: dp(20)
                visible: !launchFailed

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Starting Phone Mirror..."
                    font.pixelSize: dp(24)
                    font.family: phoneMirrorView.globalFont
                    color: App.Style.primaryTextColor
                }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: dp(200)
                    height: dp(4)
                    radius: 2
                    color: App.Style.secondaryTextColor
                    opacity: 0.3

                    Rectangle {
                        width: dp(60)
                        height: parent.height
                        radius: 2
                        color: App.Style.accent

                        SequentialAnimation on x {
                            running: !launchFailed && !mirrorRunning
                            loops: Animation.Infinite
                            NumberAnimation { to: 140; duration: 1000; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 0; duration: 1000; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }

            // Error state
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: dp(15)
                visible: launchFailed

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Phone Mirror Failed"
                    font.pixelSize: dp(28)
                    font.family: phoneMirrorView.globalFont
                    font.bold: true
                    color: "#FF6666"
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: errorMessage
                    font.pixelSize: dp(16)
                    font.family: phoneMirrorView.globalFont
                    color: App.Style.secondaryTextColor
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.maximumWidth: parent.width
                }

                // Phone-side failure: we retry on our own
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: setupOk && deviceError
                    text: "Waiting for the phone... mirroring restarts automatically when it is connected and authorized."
                    font.pixelSize: dp(14)
                    font.family: phoneMirrorView.globalFont
                    color: App.Style.secondaryTextColor
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.maximumWidth: parent.width * 0.8
                }

                // Setup failure: say what is missing
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: dp(20)
                    visible: !setupOk
                    text: "Setup"
                    font.pixelSize: dp(20)
                    font.family: phoneMirrorView.globalFont
                    font.bold: true
                    color: App.Style.primaryTextColor
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !setupOk
                    text: phoneMirrorManager ? phoneMirrorManager.getInstallInstructions() : "Phone Mirror manager not available"
                    font.pixelSize: dp(14)
                    font.family: phoneMirrorView.globalFont
                    color: App.Style.secondaryTextColor
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.maximumWidth: parent.width * 0.8
                }

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: dp(20)
                    text: "Retry"
                    font.pixelSize: dp(18)
                    font.family: phoneMirrorView.globalFont

                    background: Rectangle {
                        color: parent.pressed ? App.Style.accent : "transparent"
                        border.color: App.Style.accent
                        border.width: 2
                        radius: dpMin(8, 2)
                        implicitWidth: dp(150)
                        implicitHeight: dp(50)
                    }

                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: App.Style.primaryTextColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        launchFailed = false
                        deviceError = false
                        errorMessage = ""
                        frameCounter = 0
                        startMirror()
                    }
                }
            }
        }
    }

    function startMirror() {
        if (!phoneMirrorManager) {
            launchFailed = true
            errorMessage = "Phone Mirror manager not available"
            return
        }
        if (phoneMirrorManager.isRunning) {
            mirrorRunning = true
            return
        }
        // The manager validates the environment and device state itself and
        // reports specific errors (unauthorized, offline, ...) via scrcpyError.
        phoneMirrorManager.startScrcpy()
    }

    Connections {
        target: phoneMirrorManager

        function onScrcpyStarted(handle) {
            console.log("Phone Mirror: stream started")
            mirrorRunning = true
            launchFailed = false
        }

        function onFrameReady() {
            phoneMirrorView.frameCounter++
        }

        function onScrcpyStopped() {
            console.log("Phone Mirror: stopped")
            mirrorRunning = false
            frameCounter = 0
        }

        function onScrcpyError(error) {
            console.log("Phone Mirror: error -", error)
            mirrorRunning = false
            launchFailed = true
            errorMessage = error
            deviceError = /disconnected|No Android device|not authorized|offline/i.test(error)
        }
    }

    // Auto-recovery: while the error screen is up and the setup is fine, poll
    // the phone and restart mirroring the moment it is back (a nudged cable in
    // a vehicle is the normal case). Stops when the view is left.
    Timer {
        interval: 2000
        repeat: true
        running: launchFailed && setupOk && deviceError && !mirrorRunning
                 && phoneMirrorView.StackView.status === StackView.Active
        onTriggered: {
            if (!phoneMirrorManager || phoneMirrorManager.isRunning)
                return
            if (phoneMirrorManager.getDeviceState() === "device") {
                console.log("Phone Mirror: phone is back, restarting")
                launchFailed = false
                deviceError = false
                errorMessage = ""
                frameCounter = 0
                startMirror()
            }
        }
    }

    Component.onCompleted: {
        console.log("PhoneMirrorView loaded")
        if (phoneMirrorManager) {
            console.log("phone mirror client available:", phoneMirrorManager.nativeAvailable,
                        "server", phoneMirrorManager.serverVersion, "adb:", phoneMirrorManager.adbPath)
            startMirror()
        } else {
            launchFailed = true
            errorMessage = "Phone Mirror manager not available"
        }
    }

    Component.onDestruction: {
        console.log("PhoneMirrorView destroyed")
    }
}
