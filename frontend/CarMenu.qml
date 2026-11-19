import QtQuick
import QtQuick.Controls
import QtQuick3D
import "." as App

// The 3D view: the vehicle as an inclinometer. The car fills the page and
// tilts with the IMU against a level ring; pitch and roll are the two numbers
// under it. Nothing else is on screen: the level actions are behind the menu
// button, and the rig's test controls (VehicleRigPanel) load only on request.
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: carMenu
    objectName: "carMenu"

    // Full IMU rate while this page is on screen
    App.ImuDemand { consumer: "carMenu"; wanted: carMenu.visible }
    width: parent.width
    height: parent.height

    // Required properties
    required property var stackView
    property var mainWindow

    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Attitude shown by the readouts: the IMU's, or the test panel's sliders
    property real currentPitch: 0
    property real currentRoll: 0
    property real currentHeading: 0
    property bool imuConnected: false

    // Camera orbit (the view's; kept here for the tools that drive the page)
    property alias cameraYaw: view.cameraYaw
    property alias cameraPitch: view.cameraPitch
    property alias cameraZoom: view.cameraZoom

    // The menu's "Change vehicle" is up (VehicleChooser over the car)
    property bool choosing: false
    readonly property bool hasManager: typeof vehicleManager !== "undefined" && !!vehicleManager
    onVisibleChanged: if (!visible) choosing = false

    // The rig's test controls. Off for a driver; the menu offers them in a
    // --dev run, and tools/vehicle/check_jeep_view.py sets this.
    property bool rigControls: false
    readonly property bool devRun: typeof devMode !== "undefined" && devMode === true
    // Whether pitch and roll mean anything right now
    readonly property bool attitudeKnown: imuConnected || rigControls
    onRigControlsChanged: if (!rigControls && !imuConnected) setManualAttitude(0, 0)

    function setManualAttitude(pitch, roll) {
        currentPitch = pitch
        currentRoll = roll
        view.attitude = Quaternion.fromEulerAngles(pitch, 0, roll)
    }
    function degrees(value) {
        var n = Math.round(value)
        return (n === 0 ? 0 : n) + "°"   // never "-0"
    }

    Component.onCompleted: {
        // Read initial IMU connection state (signal may have fired before this component loaded)
        imuConnected = berryIMU.connected
    }

    // Live OBD drives the 3D Jeep's fan/crank (RPM) and wheels/driveshafts
    // (SPEED, reported in MPH) while connected or while the OBD simulator
    // runs; otherwise the test panel's sliders do. RPM and SPEED are
    // requested only while this page is on screen (demand-driven polling).
    readonly property bool liveDrive: (typeof obdManager !== "undefined" && obdManager !== null && obdManager.connected === true)
        || App.OBDParameterModel.simulationActive
    readonly property real liveRpm: Number(App.OBDParameterModel.liveValue("RPM")) || 0
    readonly property real liveSpeedKph: units.mphToKph(Number(App.OBDParameterModel.rawValue("SPEED")) || 0)   // native MPH, whatever the gauges show
    Binding { target: view.vehicle; property: "engineRpm"; value: carMenu.liveRpm; when: carMenu.liveDrive }
    Binding { target: view.vehicle; property: "wheelSpeedKph"; value: carMenu.liveSpeedKph; when: carMenu.liveDrive }
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
            view.attitude = Qt.quaternion(w, y, z, x)
        }
        // Euler angles for the readouts only — NOT applied to the model
        function onPitchChanged(val) { currentPitch = val }
        function onRollChanged(val) { currentRoll = val }
        function onHeadingChanged(val) { currentHeading = val }
    }
    // Connection state stays tracked while hidden (cheap, and rare).
    Connections {
        target: berryIMU
        function onConnectionStatusChanged(status) { imuConnected = (status === "Connected") }
    }

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor
    }

    VehicleView {
        id: view
        anchors.fill: parent
        active: carMenu.visible
    }

    Text {
        anchors.centerIn: parent
        visible: view.hasVehicle && !view.ready && !chooserLoader.active
        text: view.error ? "Unable to load the vehicle: " + view.error : "Loading…"
        color: App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText
        font.family: carMenu.globalFont
        width: parent.width - dp(48)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    // --- Readouts: heading above the car, pitch and roll under it ---

    component Readout: Column {
        property string label
        property string value
        spacing: 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.value
            color: App.Style.primaryTextColor
            font.pixelSize: carMenu.dp(84)
            font.family: carMenu.globalFont
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.label
            color: App.Style.secondaryTextColor
            font.pixelSize: carMenu.dp(16)
            font.letterSpacing: carMenu.dp(3)
            font.family: carMenu.globalFont
        }
    }

    Text {
        objectName: "carHeading"
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: dp(20) }
        visible: carMenu.imuConnected && !rigPanel.active && !chooserLoader.active
        // (null-guarded: at shutdown the backend drops units before the page dies)
        text: (units ? units.cardinal(currentHeading) + "  " : "") + degrees(currentHeading)
        color: App.Style.secondaryTextColor
        font.pixelSize: dp(18)
        font.letterSpacing: dp(1)
        font.family: carMenu.globalFont
    }

    Readout {
        objectName: "carPitch"
        anchors { left: parent.left; bottom: parent.bottom; leftMargin: dp(40); bottomMargin: dp(28) }
        visible: carMenu.attitudeKnown && !chooserLoader.active
        label: "PITCH"
        value: degrees(currentPitch)
    }

    Readout {
        objectName: "carRoll"
        anchors { right: parent.right; bottom: parent.bottom; rightMargin: dp(40); bottomMargin: dp(28) }
        visible: carMenu.attitudeKnown && !chooserLoader.active
        label: "ROLL"
        value: degrees(currentRoll)
    }

    Text {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: dp(28) }
        visible: !carMenu.attitudeKnown && view.ready && !chooserLoader.active
        text: "No motion sensor"
        color: App.Style.secondaryTextColor
        font.pixelSize: dp(13)
        font.family: carMenu.globalFont
    }

    // --- Menu: the level actions, and the test controls in a --dev run ---

    Rectangle {
        id: menuButton
        objectName: "carMenuButton"
        anchors { top: parent.top; right: parent.right; margins: dp(12) }
        // Nothing to offer before a vehicle is chosen: the chooser is the page then
        visible: view.hasVehicle && !chooserLoader.active
        width: dp(48); height: width; radius: width / 2
        color: menuMouse.pressed || menu.opened
            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22) : "transparent"
        Row {
            anchors.centerIn: parent
            spacing: dp(4)
            Repeater {
                model: 3
                Rectangle { width: carMenu.dpMin(5, 3); height: width; radius: width / 2; color: App.Style.primaryTextColor }
            }
        }
        MouseArea {
            id: menuMouse
            anchors.fill: parent
            onClicked: menu.opened ? menu.close() : menu.open()
        }
    }

    Popup {
        id: menu
        parent: menuButton
        x: menuButton.width - width
        y: menuButton.height + dp(6)
        padding: dp(6)
        background: Rectangle {
            color: App.Style.contentColor
            radius: carMenu.dpMin(10, 2)
            border.width: 1
            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
        }
        contentItem: Column {
            Repeater {
                model: [
                    { name: "changeVehicle", label: "Change vehicle", shown: carMenu.hasManager },
                    { name: "imuZero", label: "Set level here", shown: carMenu.imuConnected },
                    { name: "imuReset", label: "Reset level", shown: carMenu.imuConnected },
                    { name: "rigControlsToggle", label: carMenu.rigControls ? "Hide test controls" : "Test controls", shown: carMenu.devRun && view.rigged }
                ]
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: modelData.name
                    visible: modelData.shown
                    width: carMenu.dp(220); height: carMenu.dp(48)
                    radius: carMenu.dpMin(6, 2)
                    color: rowMouse.pressed
                        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22) : "transparent"
                    Text {
                        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: carMenu.dp(12) }
                        text: row.modelData.label
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: carMenu.globalFont
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        onClicked: {
                            menu.close()
                            if (row.modelData.name === "changeVehicle") carMenu.choosing = true
                            else if (row.modelData.name === "imuZero") berryIMU.calibrateTare()
                            else if (row.modelData.name === "imuReset") berryIMU.resetTare()
                            else carMenu.rigControls = !carMenu.rigControls
                        }
                    }
                }
            }
        }
    }

    // Which vehicle: the whole page while none is chosen (a fresh install),
    // and over the car when the menu's "Change vehicle" asks
    Rectangle {
        anchors.fill: parent
        visible: chooserLoader.active && view.hasVehicle
        color: Qt.rgba(App.Style.backgroundColor.r, App.Style.backgroundColor.g, App.Style.backgroundColor.b, 0.94)
        MouseArea { anchors.fill: parent; onClicked: carMenu.choosing = false }   // beside the tiles: keep the vehicle
    }
    Loader {
        id: chooserLoader
        objectName: "vehicleChooserLoader"
        anchors.fill: parent
        active: carMenu.hasManager && (!view.hasVehicle || carMenu.choosing)
        source: "VehicleChooser.qml"
    }
    Connections {
        target: chooserLoader.item
        function onChosen() { carMenu.choosing = false }
    }

    Loader {
        id: rigPanel
        anchors { top: parent.top; left: parent.left; right: menuButton.left; margins: dp(12) }
        // The panel drives the built-in rig; a model of the user's has no parts
        active: carMenu.rigControls && view.rigged && !!view.vehicle
        sourceComponent: VehicleRigPanel {
            vehicle: view.vehicle
            liveDrive: carMenu.liveDrive
            manualAttitude: !carMenu.imuConnected
            pitch: carMenu.currentPitch
            roll: carMenu.currentRoll
            onAttitudeMoved: (pitch, roll) => carMenu.setManualAttitude(pitch, roll)
        }
    }
}
