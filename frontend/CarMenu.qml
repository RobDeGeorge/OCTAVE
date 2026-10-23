import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
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
    property real cameraZoom: 1
    function zoomCamera(factor) {
        if (isFinite(factor) && factor > 0)
            cameraZoom = Math.max(0.4, Math.min(3, cameraZoom * factor))
    }

    // Helper: convert heading to cardinal direction
    function headingToCardinal(h) { return units.cardinal(h) }

    // Debugging output
    Component.onCompleted: {
        console.log("CarMenu component created successfully")
        // Read initial IMU connection state (signal may have fired before this component loaded)
        imuConnected = berryIMU.connected
        console.log("CarMenu: IMU connected =", imuConnected)
    }

    // Live OBD drives the 3D Jeep's fan/crank (RPM) and wheels/driveshafts
    // (SPEED, reported in MPH) while connected or while the OBD simulator
    // runs; otherwise the manual test sliders below do. RPM and SPEED are
    // requested only while this page is on screen (demand-driven polling).
    readonly property bool liveDrive: (typeof obdManager !== "undefined" && obdManager !== null && obdManager.connected === true)
        || App.OBDParameterModel.simulationActive
    readonly property real liveRpm: Number(App.OBDParameterModel.liveValue("RPM")) || 0
    readonly property real liveSpeedKph: units.mphToKph(Number(App.OBDParameterModel.liveValue("SPEED")) || 0)
    Binding { target: jeep; property: "engineRpm"; value: carMenu.liveRpm; when: carMenu.liveDrive }
    Binding { target: jeep; property: "wheelSpeedKph"; value: carMenu.liveSpeedKph; when: carMenu.liveDrive }
    readonly property var _obdDemandIds: visible ? ["RPM", "SPEED"] : []
    function _pushObdDemand(ids) {
        if (typeof obdManager !== "undefined" && obdManager && obdManager.setParameterDemand)
            obdManager.setParameterDemand("carMenu", ids)
    }
    on_ObdDemandIdsChanged: _pushObdDemand(_obdDemandIds)
    Component.onDestruction: _pushObdDemand([])

    // Connections to live BerryIMU data. Only while this page is visible:
    // it stays alive hidden (StackView cache, pre-built at startup), and the
    // IMU streams at 60 Hz while OBD or a sensor page is open, so updating
    // the 3D model's rotation off-screen kept the render loop at full rate
    // (Orange Pi, 0.9.4 testing). The next sample after the page returns
    // brings the model up to date.
    Connections {
        target: berryIMU
        enabled: carMenu.visible
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
    }
    // Connection state stays tracked while hidden (cheap, and rare).
    Connections {
        target: berryIMU
        function onConnectionStatusChanged(status) { imuConnected = (status === "Connected") }
    }

    // Timer to update simulated values (only when IMU not connected)
    Timer {
        id: simulationTimer
        interval: 50
        running: simulationRunning && !imuConnected && carMenu.visible
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

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor

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
            // Same colour as the scene clearColor so the vehicle controls and
            // the 3D view read as one panel.
            color: "#18232F"
            radius: dpMin(10, 2)
            clip: true
            border.width: 1
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.45)

            // The stock Basic Buttons/Sliders/Labels here sit on the dark scene
            // whatever the theme, so they get light text and accent fills rather
            // than the default light-grey widgets. Basic reads: button/buttonText
            // (idle), dark/brightText (checked button, slider fill + handle
            // border), mid (pressed blend), midlight (slider groove),
            // window/light (handle).
            palette.button: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22)
            palette.buttonText: "white"
            palette.dark: App.Style.accent
            palette.brightText: "#18232F"
            palette.mid: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.5)
            palette.midlight: Qt.rgba(1, 1, 1, 0.18)
            palette.window: "white"
            palette.light: App.Style.accent
            palette.windowText: "white"

            View3D {
                id: view3d
                objectName: "jeepScene"
                anchors { top: vehicleControls.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

                environment: SceneEnvironment {
                    clearColor: "#18232F"
                    backgroundMode: SceneEnvironment.Color
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.Medium
                    aoEnabled: false
                    lightProbe: Texture { source: "./assets/vehicle_studio.hdr" }
                    probeExposure: 0.8
                }

                Node {
                    id: cameraOrbit
                    eulerRotation: Qt.vector3d(cameraPitch, cameraYaw, 0)

                    PerspectiveCamera {
                        id: camera
                        objectName: "jeepCamera"
                        // Start fitted even in narrow views; zoom operates on
                        // that distance while keeping the camera outside the rig.
                        readonly property real fittedDistance: Math.max(cameraDistance,
                            (jeep.radius * 30 + 3) / Math.sin(Math.atan(Math.tan(Math.PI / 8)
                                * Math.min(1, view3d.width / Math.max(1, view3d.height)))))
                        position: Qt.vector3d(0, 0, Math.max(jeep.radius * 30 + clipNear + 2,
                            fittedDistance / carMenu.cameraZoom))
                        fieldOfView: 45
                        clipNear: 10
                        clipFar: Math.max(1000, position.z + jeep.radius * 30 + 100)
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

                    JeepVehicle { id: jeep }
                }
            }

            Flow {
                id: vehicleControls
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 6 }
                spacing: 4
                enabled: jeep.ready
                Repeater {
                    model: [
                        {label: "Driver door", key: "driverDoorOpen"},
                        {label: "Passenger door", key: "passengerDoorOpen"},
                        {label: "Rear glass", key: "rearGlassOpen", glass: true},
                        {label: "Tailgate", key: "tailgateOpen"},
                        {label: "Hood", key: "hoodOpen"},
                        {label: "Headlights", key: "headlightsOn"},
                        {label: "Fog lights", key: "fogLightsOn"},
                        {label: "Brake lights", key: "brakeLightsOn"},
                        {label: "Reverse lights", key: "reverseLightsOn"}
                    ]
                    delegate: Button {
                        required property var modelData
                        objectName: "jeepControl_" + modelData.key
                        visible: !modelData.glass || jeep.hasRearGlass
                        text: modelData.label
                        checkable: true
                        checked: jeep[modelData.key]
                        onClicked: jeep[modelData.key] = !jeep[modelData.key]
                    }
                }
                Button { objectName: "jeepOpenAll"; text: "Open all"; onClicked: jeep.openAll() }
                Button { text: "Close all"; onClicked: jeep.closeAll() }
                Row {
                    spacing: 6
                    Label { text: "Steer"; color: "white"; anchors.verticalCenter: parent.verticalCenter }
                    Slider {
                        objectName: "jeepSteering"
                        width: carMenu.dp(130)
                        from: -1; to: 1; value: -jeep.steeringInput
                        onMoved: jeep.steeringInput = -value
                    }
                    Button { objectName: "centerWheels"; text: "Center wheels"; onClicked: jeep.steeringInput = 0 }
                }
                Row {
                    spacing: 6
                    Button {
                        objectName: "jeepSpinWheels"; text: "Spin wheels"
                        enabled: !carMenu.liveDrive
                        checkable: true; checked: jeep.wheelSpeedKph !== 0
                        onClicked: jeep.wheelSpeedKph = jeep.wheelSpeedKph !== 0 ? 0 : 10
                    }
                    Slider {
                        objectName: "jeepWheelSpeed"; width: carMenu.dp(130)
                        enabled: !carMenu.liveDrive
                        from: -30; to: 30; value: jeep.wheelSpeedKph
                        onMoved: jeep.wheelSpeedKph = value
                    }
                    Label {
                        text: jeep.wheelSpeedKph.toFixed(0) + " km/h"
                        color: "white"; anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 6
                    Label { text: "Engine"; color: "white"; anchors.verticalCenter: parent.verticalCenter }
                    Slider {
                        objectName: "jeepEngineRpm"; width: carMenu.dp(130)
                        enabled: !carMenu.liveDrive
                        from: 0; to: 6000; stepSize: 50; value: jeep.engineRpm
                        onMoved: jeep.engineRpm = value
                    }
                    Label {
                        text: jeep.engineRpm.toFixed(0) + " rpm" + (carMenu.liveDrive ? " · live OBD" : "")
                        color: "white"; anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            Label {
                anchors.centerIn: view3d
                visible: !jeep.ready
                text: jeep.error ? "Unable to load Jeep: " + jeep.error : "Loading Jeep…"
                color: "white"
                width: parent.width - 24
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            // Separate one-point orbit and two-point zoom gestures so a pinch
            // does not also rotate the vehicle. Controls above remain separate.
            Item {
                anchors.fill: view3d
                PinchHandler {
                    id: scenePinch
                    target: null
                    minimumPointCount: 2
                    maximumPointCount: 2
                    onScaleChanged: (delta) => carMenu.zoomCamera(delta)
                }
                DragHandler {
                    target: null
                    maximumPointCount: 1
                    property vector2d previousTranslation: Qt.vector2d(0, 0)
                    onActiveChanged: previousTranslation = Qt.vector2d(0, 0)
                    onActiveTranslationChanged: {
                        if (active && !scenePinch.active) {
                            var delta = activeTranslation.minus(previousTranslation)
                            cameraYaw -= delta.x * 0.5
                            cameraPitch = Math.max(-89, Math.min(89, cameraPitch + delta.y * 0.3))
                        }
                        previousTranslation = activeTranslation
                    }
                }
                WheelHandler {
                    target: null
                    // Hyprland's Qt Wayland labels mouse wheels as touchpads.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => carMenu.zoomCamera(Math.pow(1.0015, event.angleDelta.y))
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
            color: App.Style.contentColor
            radius: dpMin(10, 2)
            border.width: 1
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)

            // Buttons and simulation sliders in this strip follow the theme
            palette.button: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.14)
            palette.buttonText: App.Style.primaryTextColor
            palette.brightText: App.Style.backgroundColor
            palette.dark: App.Style.accent
            palette.mid: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.45)
            palette.midlight: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.18)
            palette.window: App.Style.primaryTextColor
            palette.light: App.Style.accent
            palette.windowText: App.Style.primaryTextColor

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
                    color: App.Style.statusConnected

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
                    objectName: "imuZero"
                    width: dp(70)
                    height: dp(30)
                    radius: 5
                    color: tareMouseArea.pressed ? Qt.darker(App.Style.accent, 1.3) : App.Style.accent

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
                    objectName: "imuReset"
                    width: dp(70)
                    height: dp(30)
                    radius: 5
                    color: resetTareMouseArea.pressed ? Qt.darker(App.Style.statusWarning, 1.3) : App.Style.statusWarning

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
                    color: App.Style.primaryTextColor
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Roll: " + currentRoll.toFixed(1) + "\u00B0"
                    color: App.Style.primaryTextColor
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Heading: " + currentHeading.toFixed(1) + "\u00B0 " + headingToCardinal(currentHeading)
                    color: App.Style.primaryTextColor
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: "Alt: " + currentAltitude.toFixed(1) + " m"
                    color: App.Style.primaryTextColor
                    font.pixelSize: dp(13)
                    font.family: carMenu.globalFont
                }

                Text {
                    text: currentBaroTemp.toFixed(1) + " \u00B0C"
                    color: App.Style.secondaryTextColor
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
                    color: "transparent"
                    border.width: 1
                    border.color: App.Style.secondaryTextColor

                    Text {
                        anchors.centerIn: parent
                        text: "NO IMU"
                        color: App.Style.secondaryTextColor
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
                        color: App.Style.secondaryTextColor
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
                        color: App.Style.secondaryTextColor
                        font.pixelSize: dp(12)
                        font.family: carMenu.globalFont
                    }
                }
            }
        }
    }
}
