// RigVehicle.qml
//
// A rigged vehicle put together from data: the folder of a build script's
// output under frontend/assets/ (tools/vehicle/carkit.py: a .glb per moving
// part, each about its own pivot, and rig.json, which says where the pivots
// are, what each part is and where the lamps sit). It offers VehicleView and
// the test panel the members JeepVehicle does, so nothing else has to know
// which of the two it has.
//
// rig.json: { parts: { name: { file, pivot, axis, openAngle, role, tap } },
//             bounds: { closedRadius, sweptRadius }, groundY, maxSteer,
//             lamps: [ { circuit, position, size, round, facing } ] }
//   role  "body" (fixed), "panel" (a hinge: `tap` names the property that
//         opens it), "wheel" (spins), "steered_wheel" (turns and spins)
//   tap   one of driverDoorOpen, passengerDoorOpen, rearLeftDoorOpen,
//         rearRightDoorOpen, hoodOpen, tailgateOpen
//   lamps circuit "head", "fog", "brake" or "reverse"; facing "front" / "rear"
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Node {
    id: vehicle
    objectName: "rigVehicle"

    // The rig's folder under frontend/assets/, e.g. "vehicles/bmw-e46-330i-zhp"
    property string folder: ""
    // (null-guarded: at shutdown the backend drops assetStore before the scene dies)
    readonly property var rig: (typeof assetStore !== "undefined" && assetStore && folder !== "")
        ? assetStore.readJson(folder + "/rig.json") : ({})
    readonly property var parts: rig.parts || ({})
    function _withRole(roles) {
        var out = []
        for (var name in parts) {
            if (roles.indexOf(parts[name].role) >= 0)
                out.push({ name: name, part: parts[name] })
        }
        return out
    }
    readonly property var hingedParts: _withRole(["body", "panel"])
    readonly property var wheelParts: _withRole(["wheel", "steered_wheel"])

    // What a tap, the test panel or a host can open
    property bool driverDoorOpen: false
    property bool passengerDoorOpen: false
    property bool rearLeftDoorOpen: false
    property bool rearRightDoorOpen: false
    property bool hoodOpen: false
    property bool tailgateOpen: false
    // (a member of the Jeep's rig the test panel asks about; no such part here)
    property bool rearGlassOpen: false
    readonly property bool hasRearGlass: false
    readonly property var openKeys: {
        var keys = []
        for (var name in parts) {
            var tap = parts[name].tap
            if (tap && keys.indexOf(tap) < 0)
                keys.push(tap)
        }
        return keys
    }
    readonly property bool anyOpenRequested: openKeys.some(function(key) { return vehicle[key] === true })
    function toggle(key) {
        if (openKeys.indexOf(key) >= 0)
            vehicle[key] = !vehicle[key]
    }
    function openAll() { openKeys.forEach(function(key) { vehicle[key] = true }) }
    function closeAll() { openKeys.forEach(function(key) { vehicle[key] = false }) }

    property bool headlightsOn: false
    property bool fogLightsOn: false
    property bool brakeLightsOn: false
    property bool reverseLightsOn: false

    // The view frames these, and puts its level ring at groundY (rig metres)
    readonly property real closedRadius: rig.bounds ? rig.bounds.closedRadius : 2.13
    readonly property real sweptRadius: rig.bounds ? rig.bounds.sweptRadius : closedRadius
    readonly property real radius: anyOpenRequested ? sweptRadius : closedRadius
    readonly property real groundY: rig.groundY !== undefined ? rig.groundY : -1

    // Wheels: speed turns them, the steering input (-1 .. 1, positive to the
    // left) turns the front ones, the inner one a little further
    property real wheelSpeedKph: 0
    property real wheelSpinAngle: 0
    property real engineRpm: 0
    property real steeringInput: 0
    property real animatedSteering: Math.max(-1, Math.min(1, steeringInput))
    Behavior on animatedSteering { NumberAnimation { duration: 180 } }
    readonly property real maxSteer: rig.maxSteer !== undefined ? rig.maxSteer : 32
    function steerAngle(left) {
        var inner = (animatedSteering > 0) === left
        return animatedSteering * maxSteer * (inner ? 1 : 0.86)
    }
    readonly property real tyreRadius: wheelParts.length > 0 && wheelParts[0].part.radius ? wheelParts[0].part.radius : 0.32
    readonly property real wheelDegreesPerSecond: Math.max(-30, Math.min(30, wheelSpeedKph)) / 3.6 / tyreRadius * 180 / Math.PI

    // Whether the model is on screen. A Quick3D Node's own `visible` stays
    // true while the page that holds its View3D is hidden, so the host says.
    property bool active: true
    Timer {
        interval: 16; repeat: true
        running: vehicle.ready && vehicle.active && vehicle.visible && vehicle.wheelSpeedKph !== 0
        property double lastTick: 0
        onRunningChanged: lastTick = Date.now()
        onTriggered: {
            var now = Date.now()
            vehicle.wheelSpinAngle = ((vehicle.wheelSpinAngle + vehicle.wheelDegreesPerSecond * (now - lastTick) / 1000) % 360 + 360) % 360
            lastTick = now
        }
    }

    // Counts the parts the repeaters have made. `ready` and `error` read it:
    // a repeater's `count` changes before its parts exist when the page is
    // built in the background (asynchronous incubation), objectAt() is not
    // something a binding hears about, and without this `ready` stayed
    // false ("Loading…" over a vehicle that had drawn).
    property int _partsMade: 0

    function _all(test) {
        _partsMade
        var total = hinges.count + wheels.count
        if (total === 0)
            return false
        for (var i = 0; i < hinges.count; ++i) {
            if (!hinges.objectAt(i) || !test(hinges.objectAt(i)))
                return false
        }
        for (var j = 0; j < wheels.count; ++j) {
            if (!wheels.objectAt(j) || !test(wheels.objectAt(j).hinge))
                return false
        }
        return true
    }
    readonly property bool ready: _all(function(h) { return h.ready })
    readonly property string error: {
        _partsMade
        for (var i = 0; i < hinges.count; ++i) {
            if (hinges.objectAt(i) && hinges.objectAt(i).error)
                return hinges.objectAt(i).error
        }
        for (var j = 0; j < wheels.count; ++j) {
            if (wheels.objectAt(j) && wheels.objectAt(j).hinge.error)
                return wheels.objectAt(j).hinge.error
        }
        return (folder !== "" && !rig.parts) ? "This vehicle's rig.json could not be read" : ""
    }

    // The body and the panels that swing on it
    Repeater3D {
        id: hinges
        model: vehicle.hingedParts
        onObjectAdded: ++vehicle._partsMade
        onObjectRemoved: ++vehicle._partsMade
        delegate: JeepHinge {
            required property var modelData
            objectName: "rigPart_" + modelData.name
            folder: vehicle.folder
            part: modelData.part
            open: modelData.part.tap ? vehicle[modelData.part.tap] === true : false
            tappable: true
            tapKey: modelData.part.tap || ""
        }
    }

    // A wheel: a node on its axle that turns with the steering, and in it
    // the wheel itself, which spins
    Repeater3D {
        id: wheels
        model: vehicle.wheelParts
        onObjectAdded: ++vehicle._partsMade
        onObjectRemoved: ++vehicle._partsMade
        delegate: Node {
            id: wheel
            required property var modelData
            readonly property var part: modelData.part
            readonly property alias hinge: spin
            objectName: "rigWheel_" + modelData.name
            position: Qt.vector3d(part.pivot[0], part.pivot[1], part.pivot[2])
            eulerRotation.y: part.role === "steered_wheel" ? vehicle.steerAngle(part.pivot[0] > 0) : 0
            JeepHinge {
                id: spin
                folder: vehicle.folder
                part: ({ file: wheel.part.file, pivot: [0, 0, 0], axis: "X", openAngle: 0 })
                animate: false
                animatedAngle: vehicle.wheelSpinAngle
            }
        }
    }

    Repeater3D {
        model: vehicle.rig.lamps || []
        delegate: JeepLamp {
            id: lamp
            required property var modelData
            readonly property string circuit: modelData.circuit
            readonly property bool front: modelData.facing === "front"
            objectName: circuit + "Light"
            on: circuit === "head" ? vehicle.headlightsOn : circuit === "fog" ? vehicle.fogLightsOn
              : circuit === "brake" ? vehicle.brakeLightsOn : circuit === "reverse" ? vehicle.reverseLightsOn : false
            position: Qt.vector3d(modelData.position[0], modelData.position[1], modelData.position[2])
            // A lamp shines along its own -Z; one that faces forward is turned round
            eulerRotation: front ? Qt.vector3d(-4, 180, 0) : Qt.vector3d(0, 0, 0)
            round: modelData.round === true
            lensSize: Qt.vector3d(modelData.size[0], modelData.size[1], 0.004)
            tint: circuit === "brake" ? "#ff2410" : circuit === "reverse" ? "#fff9ec" : "#fff3d5"
            emission: circuit === "brake" ? Qt.vector3d(2.0, 0.025, 0.008)
                    : circuit === "reverse" ? Qt.vector3d(1.6, 1.5, 1.3) : Qt.vector3d(1.5, 1.3, 0.9)
            beamBrightness: circuit === "head" ? 8 : circuit === "fog" ? 5 : circuit === "brake" ? 0.8 : 3
            beamAngle: circuit === "head" ? 48 : circuit === "fog" ? 80 : 105
            beamInnerAngle: circuit === "head" ? 28 : circuit === "fog" ? 50 : 60
        }
    }
}
