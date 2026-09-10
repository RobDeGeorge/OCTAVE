import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.AssetUtils
import "." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: carMenu
    objectName: "carMenu"
    width: parent.width
    height: parent.height

    // Required properties
    required property var stackView
    property var mainWindow

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Simulation properties for accelerometer
    property real currentPitch: 0
    property real currentRoll: 0
    property bool simulationRunning: false

    // Live IMU properties
    property real currentHeading: 0
    property real currentAltitude: 0
    property real currentGForce: 0
    property real currentBaroTemp: 0
    property bool imuConnected: false

    // Camera orbit properties
    property real cameraYaw: -35
    property real cameraPitch: -25
    property real cameraDistance: 200

    // Helper: convert heading to cardinal direction
    function headingToCardinal(h) {
        var dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        var idx = Math.round(h / 45) % 8
        return dirs[idx]
    }

    // Debugging output
    Component.onCompleted: {
        console.log("CarMenu component created successfully")
        // Read initial IMU connection state (signal may have fired before this component loaded)
        imuConnected = berryIMU.connected
        console.log("CarMenu: IMU connected =", imuConnected)
    }

    // Connections to live BerryIMU data
    Connections {
        target: berryIMU
        // Quaternion drives the 3D model directly — no gimbal lock
        // Remap sensor frame (Z-up) to Qt3D frame (Y-up): (w,x,y,z) → (w,y,z,x)
        function onOrientationChanged(w, x, y, z) {
            carModel.rotation = Qt.quaternion(w, y, z, x)
        }
        // Euler angles for gauge displays only — NOT applied to the model
        function onPitchChanged(val) { currentPitch = val }
        function onRollChanged(val) { currentRoll = val }
        function onHeadingChanged(val) { currentHeading = val }
        function onAltitudeChanged(val) { currentAltitude = val }
        function onAccelMagnitudeChanged(val) { currentGForce = val }
        function onBaroTempChanged(val) { currentBaroTemp = val }
        function onConnectionStatusChanged(status) { imuConnected = (status === "Connected") }
    }

    // Timer to update simulated values (only when IMU not connected)
    Timer {
        id: simulationTimer
        interval: 50
        running: simulationRunning && !imuConnected
        repeat: true
        onTriggered: {
            // Create some simple motion for demonstration
            currentPitch = 30 * Math.sin(Date.now() * 0.001)
            currentRoll = 45 * Math.cos(Date.now() * 0.0015)

            // Apply to model
            carModel.eulerRotation.x = currentPitch
            carModel.eulerRotation.z = currentRoll
        }
    }

    // Background with accent color
    Rectangle {
        anchors.fill: parent
        color: App.Style.accent

        // 3D View container
        Rectangle {
            id: modelContainer
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                bottom: controlPanel.top
                margins: App.Spacing.overallMargin
            }
            color: "black"

            View3D {
                id: view3d
                anchors.fill: parent

                environment: SceneEnvironment {
                    clearColor: "#18232F"
                    backgroundMode: SceneEnvironment.Color
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.Medium
                    aoEnabled: false
                }

                Node {
                    id: cameraOrbit
                    eulerRotation: Qt.vector3d(cameraPitch, cameraYaw, 0)

                    PerspectiveCamera {
                        id: camera
                        // Fit the vehicle's rotation envelope even in a narrow view.
                        position: Qt.vector3d(0, 0, Math.max(cameraDistance,
                            70 / Math.sin(Math.atan(Math.tan(Math.PI / 8)
                                * Math.min(1, view3d.width / Math.max(1, view3d.height))))))
                        fieldOfView: 45
                        clipNear: 10
                        clipFar: 1000
                    }
                }

                DirectionalLight {
                    id: mainLight
                    eulerRotation.x: -45
                    eulerRotation.y: 45
                    brightness: 1.3
                    ambientColor: "#777777"
                    castsShadow: false
                }

                DirectionalLight {
                    eulerRotation: Qt.vector3d(-25, -135, 0)
                    brightness: 0.7
                    color: "#C3D9FF"
                }

                // Metre-scale TJ, +Z forward / +Y up, pivot at chassis centre.
                Node {
                    id: carModel
                    position: Qt.vector3d(0, 0, 0)
                    scale: Qt.vector3d(30, 30, 30)

                    RuntimeLoader {
                        id: modelLoader
                        source: "./assets/jeep_tj_2003.glb"
                        // RuntimeLoader exposes `status` + `errorString`; there is no
                        // `statusString`, so the old handler threw a ReferenceError and
                        // swallowed the very message needed to diagnose a failed load
                        // (e.g. a missing assimp asset-import plugin).
                        onStatusChanged: {
                            if (status === RuntimeLoader.Error)
                                console.warn("[CarMenu] Model load failed:", modelLoader.errorString)
                            else
                                console.log("[CarMenu] Model status:", status)
                        }
                    }
                }
            }

            // Mouse area for orbiting the camera around the model
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                property real lastX: 0
                property real lastY: 0

                onPressed: (event) => {
                    lastX = event.x
                    lastY = event.y
                }

                onPositionChanged: (event) => {
                    if (pressed) {
                        let dx = event.x - lastX
                        let dy = event.y - lastY
                        cameraYaw -= dx * 0.5
                        cameraPitch = Math.max(-89, Math.min(89, cameraPitch + dy * 0.3))
                        lastX = event.x
                        lastY = event.y
                    }
                }
            }

        }

        // Control panel
        Rectangle {
            id: controlPanel
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.margins: App.Spacing.overallMargin
            width: parent.width - (App.Spacing.overallMargin * 2)
            height: dp(60)
            color: "#333333"
            radius: dpMin(10, 2)

            // LIVE mode: show status and IMU readings
            RowLayout {
                anchors.centerIn: parent
                spacing: dp(20)
                visible: imuConnected

                // LIVE badge
                Rectangle {
                    width: dp(70)
                    height: dp(30)
                    radius: 5
                    color: "#00AA00"

                    Text {
                        anchors.centerIn: parent
                        text: "LIVE"
                        color: "white"
                        font.pixelSize: dp(14)
                        font.bold: true
                        font.family: carMenu.globalFont
                    }
                }

                // Tare button — zero out current orientation
                Rectangle {
                    width: dp(70)
                    height: dp(30)
                    radius: 5
                    color: tareMouseArea.pressed ? "#0066AA" : "#0088CC"

                    Text {
                        anchors.centerIn: parent
                        text: "ZERO"
                        color: "white"
                        font.pixelSize: dp(14)
                        font.bold: true
                        font.family: carMenu.globalFont
                    }

                    MouseArea {
                        id: tareMouseArea
                        anchors.fill: parent
                        onClicked: berryIMU.calibrateTare()
                    }
                }

                // Reset tare
                Rectangle {
                    width: dp(70)
                    height: dp(30)
                    radius: 5
                    color: resetTareMouseArea.pressed ? "#664400" : "#886600"

                    Text {
                        anchors.centerIn: parent
                        text: "RESET"
                        color: "white"
                        font.pixelSize: dp(14)
                        font.bold: true
                        font.family: carMenu.globalFont
                    }

                    MouseArea {
                        id: resetTareMouseArea
                        anchors.fill: parent
                        onClicked: berryIMU.resetTare()
                    }
                }

                Text {
                    text: "Pitch: " + currentPitch.toFixed(1) + "\u00B0"
                    color: "white"
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Roll: " + currentRoll.toFixed(1) + "\u00B0"
                    color: "white"
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Heading: " + currentHeading.toFixed(1) + "\u00B0 " + headingToCardinal(currentHeading)
                    color: "white"
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Alt: " + currentAltitude.toFixed(1) + " m"
                    color: "white"
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: currentBaroTemp.toFixed(1) + " \u00B0C"
                    color: "#AAAAAA"
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }
            }

            // Simulation fallback mode: show controls when IMU not connected
            RowLayout {
                anchors.centerIn: parent
                spacing: dp(20)
                visible: !imuConnected

                // Disconnected badge
                Rectangle {
                    width: dp(90)
                    height: dp(30)
                    radius: 5
                    color: "#666666"

                    Text {
                        anchors.centerIn: parent
                        text: "NO IMU"
                        color: "#CCCCCC"
                        font.pixelSize: dp(12)
                        font.bold: true
                        font.family: carMenu.globalFont
                    }
                }

                Button {
                    text: simulationRunning ? "Stop Sim" : "Start Sim"
                    onClicked: {
                        simulationRunning = !simulationRunning
                    }
                }

                Button {
                    text: "Reset"
                    onClicked: {
                        currentPitch = 0
                        currentRoll = 0
                        carModel.eulerRotation = Qt.vector3d(0, carModel.eulerRotation.y, 0)
                    }
                }

                Slider {
                    Layout.preferredWidth: dp(150)
                    from: -90
                    to: 90
                    value: currentPitch
                    onMoved: {
                        if (!simulationRunning) {
                            currentPitch = value
                            carModel.eulerRotation.x = currentPitch
                        }
                    }

                    Text {
                        anchors.bottom: parent.top
                        text: "Pitch"
                        color: "white"
                        font.pixelSize: dp(12)
                        font.family: carMenu.globalFont
                    }
                }

                Slider {
                    Layout.preferredWidth: dp(150)
                    from: -90
                    to: 90
                    value: currentRoll
                    onMoved: {
                        if (!simulationRunning) {
                            currentRoll = value
                            carModel.eulerRotation.z = currentRoll
                        }
                    }

                    Text {
                        anchors.bottom: parent.top
                        text: "Roll"
                        color: "white"
                        font.pixelSize: dp(12)
                        font.family: carMenu.globalFont
                    }
                }
            }
        }
    }
}
