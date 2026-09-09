import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls.Basic 2.15
import QtMultimedia
import OCTAVE.PhoneMirror 1.0
import "." as App

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

    // Mirror state
    // Both video paths feed the same image provider (image://scrcpyframe):
    //   "window" (Windows) - ScrcpyCapture screen-grabs the scrcpy window
    //   "v4l2"   (Linux)   - scrcpy streams headless into a v4l2loopback node
    //                        that ScrcpyCapture reads back through ffmpeg
    property bool mirrorRunning: false
    property int frameCounter: 0  // Bumped on every frame to refresh the Image
    property bool launchFailed: false
    property string errorMessage: ""
    readonly property string captureMode: (typeof phoneMirrorManager !== "undefined" && phoneMirrorManager
                                           && phoneMirrorManager.captureMode) ? phoneMirrorManager.captureMode : "window"
    readonly property bool v4l2Mode: captureMode === "v4l2" && !nativeMode
    // Built-in scrcpy-protocol client: frames arrive on a QVideoSink bound to
    // the VideoOutput below, touch goes over the control socket (multitouch).
    readonly property bool nativeMode: (typeof phoneMirrorManager !== "undefined" && phoneMirrorManager
                                        && phoneMirrorManager.nativeMode === true)
    // True when scrcpy and (on Linux) the video node are fine, so any failure
    // is about the phone. Re-evaluated whenever the error state changes.
    property bool setupOk: true
    // True when the last failure was the phone itself (unplugged, offline,
    // not authorized) rather than scrcpy or the video path — only those are
    // worth retrying automatically.
    property bool deviceError: false
    function refreshSetupOk() {
        setupOk = (phoneMirrorManager && phoneMirrorManager.environmentOk) ? phoneMirrorManager.environmentOk() : false
    }
    onLaunchFailedChanged: if (launchFailed) refreshSetupOk()

    // True once the capture has produced a real frame. In v4l2 mode the first
    // frame is seeded from the held buffer and a static phone emits no more
    // until something moves, so one frame is proof of life.
    readonly property bool hasVideo: (v4l2Mode || nativeMode) ? frameCounter >= 1 : frameCounter >= 10

    // Handle when this view becomes active again
    StackView.onActivated: {
        console.log("PhoneMirrorView activated")
        if (phoneMirrorManager && phoneMirrorManager.isRunning) {
            console.log("Scrcpy is running, resuming capture...")
            resumeCapture()
        }
    }

    // Handle when this view is deactivated
    StackView.onDeactivated: {
        console.log("PhoneMirrorView deactivated - pausing capture")
        pauseCapture()
    }

    function pauseCapture() {
        if (scrcpyCapture) {
            scrcpyCapture.stopCapture()
        }
    }

    function resumeCapture() {
        if (nativeMode) {
            if (phoneMirrorManager && phoneMirrorManager.isRunning) mirrorRunning = true
            return
        }
        if (scrcpyCapture && phoneMirrorManager && phoneMirrorManager.isRunning) {
            var hwnd = phoneMirrorManager.scrcpyWindowHandle
            if (hwnd) {
                scrcpyCapture.setWindowHandle(hwnd)
                scrcpyCapture.startCapture()
                mirrorRunning = true
            }
        }
    }

    // Dark background
    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // Phone Mirror display - shows captured frames from scrcpy
    Rectangle {
        id: phoneDisplay
        anchors.fill: parent
        color: "black"
        visible: mirrorRunning

        // Display captured scrcpy frames
        Image {
            id: phoneFrame
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: false
            smooth: true
            antialiasing: true
            mipmap: true
            // The source URL includes frameCounter to force refresh
            source: (mirrorRunning && !nativeMode) ? "image://scrcpyframe/frame?" + frameCounter : ""
            visible: !nativeMode
        }

        // Native mode: decoded frames land directly on this sink
        VideoOutput {
            id: nativeVideo
            anchors.fill: parent
            visible: nativeMode
            fillMode: VideoOutput.PreserveAspectFit
            Component.onCompleted: if (nativeMode && phoneMirrorManager) phoneMirrorManager.videoSink = videoSink
            Connections {
                target: phoneMirrorView
                function onNativeModeChanged() {
                    if (nativeMode && phoneMirrorManager) phoneMirrorManager.videoSink = nativeVideo.videoSink
                }
            }
        }

        // Native mode: real multitouch over the control socket. Mouse input
        // arrives as a single touch point. Coordinates map through the
        // letterboxed content rect to 0..1 of the mirrored display.
        MultiPointTouchArea {
            id: nativeTouch
            anchors.fill: parent
            enabled: nativeMode && mirrorRunning
            visible: enabled
            mouseEnabled: true
            minimumTouchPoints: 1
            maximumTouchPoints: 10

            function relPos(p) {
                var r = nativeVideo.contentRect
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

        // Native mode: Android navigation buttons (a virtual display has no
        // gesture nav bar the user can reach)
        Row {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: dp(6)
            spacing: dp(24)
            visible: nativeMode && mirrorRunning
            z: 10
            Repeater {
                model: [ { label: "◁", slot: "pressBack" }, { label: "○", slot: "pressHome" }, { label: "▢", slot: "pressAppSwitch" } ]
                Rectangle {
                    width: dp(44); height: dp(44); radius: dpMin(22, 2)
                    color: navMouse.pressed ? App.Style.accent : "#66000000"
                    Text { anchors.centerIn: parent; text: modelData.label; color: "white"; font.pixelSize: dp(20) }
                    MouseArea { id: navMouse; anchors.fill: parent
                        onClicked: if (phoneMirrorManager) phoneMirrorManager[modelData.slot]() }
                }
            }
        }

        // Touch/click forwarding to scrcpy (adb-based paths only)
        MouseArea {
            id: touchArea
            anchors.fill: parent
            hoverEnabled: false
            enabled: !nativeMode

            property bool isDragging: false
            property real lastX: 0
            property real lastY: 0

            function getRelativePosition(mouseX, mouseY) {
                // Get the painted image bounds (accounting for aspect ratio letterboxing)
                var imgX = (phoneFrame.width - phoneFrame.paintedWidth) / 2
                var imgY = (phoneFrame.height - phoneFrame.paintedHeight) / 2
                var imgW = phoneFrame.paintedWidth
                var imgH = phoneFrame.paintedHeight

                if (imgW <= 0 || imgH <= 0) {
                    return { x: 0, y: 0, valid: false }
                }

                // Check if within image bounds
                if (mouseX >= imgX && mouseX <= imgX + imgW &&
                    mouseY >= imgY && mouseY <= imgY + imgH) {
                    // Get position relative to image (0.0 to 1.0)
                    var relX = (mouseX - imgX) / imgW
                    var relY = (mouseY - imgY) / imgH
                    return { x: relX, y: relY, valid: true }
                }
                return { x: 0, y: 0, valid: false }
            }

            onPressed: function(mouse) {
                var pos = getRelativePosition(mouse.x, mouse.y)
                if (pos.valid && scrcpyCapture) {
                    console.log("Phone Mirror: Touch DOWN at rel(" + pos.x.toFixed(3) + "," + pos.y.toFixed(3) + ")")
                    isDragging = true
                    lastX = pos.x
                    lastY = pos.y
                    // Send relative position (0.0-1.0) - ADB will scale to device resolution
                    scrcpyCapture.sendTouchEvent(pos.x, pos.y, true)
                }
            }

            onReleased: function(mouse) {
                if (isDragging && scrcpyCapture) {
                    var pos = getRelativePosition(mouse.x, mouse.y)
                    var finalX = pos.valid ? pos.x : lastX
                    var finalY = pos.valid ? pos.y : lastY
                    console.log("Phone Mirror: Touch UP at rel(" + finalX.toFixed(3) + "," + finalY.toFixed(3) + ")")
                    scrcpyCapture.sendTouchEvent(finalX, finalY, false)
                }
                isDragging = false
            }

            onPositionChanged: function(mouse) {
                if (isDragging && scrcpyCapture) {
                    var pos = getRelativePosition(mouse.x, mouse.y)
                    if (pos.valid) {
                        lastX = pos.x
                        lastY = pos.y
                        scrcpyCapture.sendTouchMove(pos.x, pos.y)
                    }
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
                    if (scrcpyCapture) {
                        scrcpyCapture.stopCapture()
                    }
                    if (phoneMirrorManager) {
                        phoneMirrorManager.stopScrcpy()
                    }
                    mirrorRunning = false
                    stackView.pop()
                }
            }
        }

        // Loading indicator (show while frames start). On Linux an idle phone
        // may not draw anything for up to a minute (its clock ticks once a
        // minute), so tell the user what would speed it up.
        Text {
            anchors.centerIn: parent
            text: v4l2Mode ? "Connecting... (touch the phone to wake its screen)" : "Connecting..."
            z: 5
            font.pixelSize: dp(24)
            font.family: phoneMirrorView.globalFont
            color: "white"
            visible: mirrorRunning && !hasVideo
        }
    }

    // Error/Setup screen - shown when mirror is not running
    Item {
        anchors.fill: parent
        visible: !mirrorRunning

        // Back button (top left)
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
                if (scrcpyCapture) {
                    scrcpyCapture.stopCapture()
                }
                if (phoneMirrorManager) {
                    phoneMirrorManager.stopScrcpy()
                }
                stackView.pop()
            }
        }

        // Main content - centered
        ColumnLayout {
            anchors.centerIn: parent
            spacing: dp(25)
            width: parent.width * 0.8

            // Loading state (before we know if it failed)
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

                // Progress indicator
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: dp(200)
                    height: dp(4)
                    radius: 2
                    color: App.Style.secondaryTextColor
                    opacity: 0.3

                    Rectangle {
                        id: progressBar
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

                // Install instructions only when the setup itself is broken;
                // a disconnected or unauthorized phone gets a short hint instead.
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: dp(20)
                    visible: !setupOk
                    text: "Setup Instructions"
                    font.pixelSize: dp(20)
                    font.family: phoneMirrorView.globalFont
                    font.bold: true
                    color: App.Style.primaryTextColor
                }

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

                // Retry button
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
                        errorMessage = ""
                        frameCounter = 0
                        startMirror()
                    }
                }
            }
        }
    }

    // Function to start the mirror
    function startMirror() {
        if (!phoneMirrorManager) {
            launchFailed = true
            errorMessage = "Phone Mirror manager not available"
            return
        }

        // If already running, just resume capture
        if (phoneMirrorManager.isRunning) {
            console.log("Phone Mirror: already running, resuming capture")
            resumeCapture()
            return
        }

        if (!nativeMode && !phoneMirrorManager.isScrcpyInstalled) {
            launchFailed = true
            errorMessage = "scrcpy not installed. Download from https://github.com/Genymobile/scrcpy"
            return
        }

        // Start scrcpy - the manager validates the scrcpy version and device
        // state itself and reports specific errors (unauthorized, offline,
        // too old, ...) through scrcpyError.
        console.log("Starting scrcpy via manager, mode", captureMode)
        phoneMirrorManager.startScrcpy()
    }

    // Connect to manager signals
    Connections {
        target: phoneMirrorManager

        function onScrcpyStarted(hwnd) {
            console.log("Phone Mirror: scrcpy started with handle", hwnd, "mode", nativeMode ? "native" : captureMode)
            if (!nativeMode && scrcpyCapture) {
                scrcpyCapture.setWindowHandle(hwnd)
                scrcpyCapture.startCapture()
            }
            mirrorRunning = true
            launchFailed = false
        }

        function onFrameReady() {
            if (nativeMode) phoneMirrorView.frameCounter++
        }

        function onScrcpyStopped() {
            console.log("Phone Mirror: scrcpy stopped")
            if (scrcpyCapture) {
                scrcpyCapture.stopCapture()
            }
            mirrorRunning = false
            frameCounter = 0
        }

        function onScrcpyError(error) {
            console.log("Phone Mirror: error -", error)
            if (scrcpyCapture) {
                scrcpyCapture.stopCapture()
            }
            mirrorRunning = false
            launchFailed = true
            errorMessage = error
            deviceError = /disconnected|No Android device|not authorized|offline/i.test(error)
        }
    }

    // Connect to capture signals
    Connections {
        target: scrcpyCapture

        function onFrameReady() {
            // Increment counter to force image refresh
            phoneMirrorView.frameCounter++
        }

        function onError(errorMsg) {
            console.log("Phone Mirror capture error:", errorMsg)
            mirrorRunning = false
            launchFailed = true
            errorMessage = errorMsg
        }
    }

    // Auto-recovery: while the error screen is up and the setup is fine, poll
    // the phone and restart mirroring the moment it is back (a nudged cable in
    // a vehicle is the normal case). Stops when the view is left.
    Timer {
        id: reconnectTimer
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

    // Initial frame refresh timer (ensures smooth startup in window mode).
    // In v4l2 mode frames arrive from the reader thread and drive frameCounter
    // through onFrameReady; the capture reports its own timeout via onError.
    Timer {
        id: initialRefreshTimer
        interval: 50
        repeat: true
        running: mirrorRunning && !v4l2Mode && frameCounter < 100
        onTriggered: {
            phoneMirrorView.frameCounter++
        }
    }

    Component.onCompleted: {
        console.log("PhoneMirrorView loaded")
        console.log("phoneMirrorManager available:", phoneMirrorManager ? "yes" : "no")
        console.log("scrcpyCapture available:", scrcpyCapture ? "yes" : "no")

        if (phoneMirrorManager) {
            console.log("scrcpy installed:", phoneMirrorManager.isScrcpyInstalled)
            console.log("scrcpy path:", phoneMirrorManager.scrcpyPath, "version:", phoneMirrorManager.scrcpyVersion)
            console.log("capture mode:", nativeMode ? ("native (server " + phoneMirrorManager.serverVersion + ")") : captureMode,
                        v4l2Mode ? ("device " + phoneMirrorManager.videoDevice) : "")
            console.log("scrcpy already running:", phoneMirrorManager.isRunning)

            // Start mirror
            startMirror()
        } else {
            launchFailed = true
            errorMessage = "Phone Mirror manager not available"
        }
    }

    Component.onDestruction: {
        console.log("PhoneMirrorView destroyed")
        if (scrcpyCapture) {
            scrcpyCapture.stopCapture()
        }
    }
}
