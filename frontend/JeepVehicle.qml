pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D
import "assets/jeep_tj/Rig.js" as Rig
import "JeepSteering.js" as Steering

Node {
    id: vehicle
    objectName: "jeepVehicle"
    property bool driverDoorOpen: false
    property bool passengerDoorOpen: false
    property bool rearGlassOpen: false
    property bool tailgateOpen: false
    property bool hoodOpen: false
    property bool headlightsOn: false
    property bool fogLightsOn: false
    property bool brakeLightsOn: false
    property bool reverseLightsOn: false
    property real wheelSpeedKph: 0
    property real wheelSpinAngle: 0
    readonly property real wheelDegreesPerSecond: Math.max(-30, Math.min(30, wheelSpeedKph)) / 3.6 / 0.3937 * 180 / Math.PI
    Timer {
        interval: 16; repeat: true
        running: vehicle.ready && vehicle.visible && vehicle.wheelSpeedKph !== 0
        property double lastTick: 0
        onRunningChanged: lastTick = Date.now()
        onTriggered: {
            var now = Date.now()
            vehicle.wheelSpinAngle = ((vehicle.wheelSpinAngle + vehicle.wheelDegreesPerSecond * (now - lastTick) / 1000) % 360 + 360) % 360
            lastTick = now
        }
    }
    // Exported parts use vehicle-space pivots; front rolling parts sit beneath
    // steering pivots so their spin axis turns with the steering knuckle.
    function relativeWheelPart(part, parentPart) {
        return {file: part.file, axis: part.axis, openAngle: 0,
            pivot: [part.pivot[0] - parentPart.pivot[0], part.pivot[1] - parentPart.pivot[1], part.pivot[2] - parentPart.pivot[2]]}
    }
    property real steeringInput: 0
    property real animatedSteering: Math.max(-1, Math.min(1, steeringInput))
    Behavior on animatedSteering { NumberAnimation { duration: 180 } }
    readonly property var steeringPose: Steering.solve(animatedSteering, Rig.steering)
    readonly property var tiePoints: Steering.tiePoints(steeringPose, Rig.steering)
    readonly property bool ready: body.ready && driver.ready && passenger.ready && glass.ready && gate.ready && hood.ready && driverWheel.ready && passengerWheel.ready && driverFrontSpin.ready && passengerFrontSpin.ready && driverRearSpin.ready && passengerRearSpin.ready
    readonly property string error: body.error || driver.error || passenger.error || glass.error || gate.error || hood.error || driverWheel.error || passengerWheel.error || driverFrontSpin.error || passengerFrontSpin.error || driverRearSpin.error || passengerRearSpin.error
    readonly property real radius: (driverDoorOpen || passengerDoorOpen || rearGlassOpen || tailgateOpen || hoodOpen
        || !driver.closed || !passenger.closed || !glass.closed || !gate.closed || !hood.closed)
        ? Rig.bounds.sweptRadius : Rig.bounds.closedRadius

    // Open the swing gate fully before raising the glass; reverse this on closing.
    // The Tailgate button requests the complete two-panel sequence in both directions.
    onTailgateOpenChanged: rearGlassOpen = tailgateOpen
    onRearGlassOpenChanged: if (rearGlassOpen) tailgateOpen = true
    function openAll() {
        driverDoorOpen = true
        passengerDoorOpen = true
        hoodOpen = true
        rearGlassOpen = true
        tailgateOpen = true
    }
    function closeAll() {
        driverDoorOpen = false
        passengerDoorOpen = false
        tailgateOpen = false
        rearGlassOpen = false
        hoodOpen = false
    }
    JeepHinge { id: body; part: Rig.parts.body }
    JeepHinge { id: driver; objectName: "driverHinge"; part: Rig.parts.driver_door; open: vehicle.driverDoorOpen }
    JeepHinge { id: passenger; objectName: "passengerHinge"; part: Rig.parts.passenger_door; open: vehicle.passengerDoorOpen }
    JeepHinge { id: glass; objectName: "glassHinge"; part: Rig.parts.rear_glass; open: vehicle.rearGlassOpen && gate.opened }
    JeepHinge {
        id: gate; objectName: "gateHinge"; part: Rig.parts.tailgate
        open: vehicle.tailgateOpen || !glass.closed
        // The high brake lamp is mounted on the spare carrier and follows the gate.
        JeepLamp {
            objectName: "thirdBrakeLight"
            on: vehicle.brakeLightsOn
            position: Qt.vector3d(-gate.part.pivot[0], 0.59 - gate.part.pivot[1], -1.899 - gate.part.pivot[2])
            lensSize: Qt.vector3d(0.180, 0.038, 0.004)
            tint: "#ff2410"; emission: Qt.vector3d(2.0, 0.025, 0.008)
            beamBrightness: 0.5; beamAngle: 110; beamInnerAngle: 65
        }
    }
    JeepHinge { id: hood; objectName: "hoodHinge"; part: Rig.parts.hood; open: vehicle.hoodOpen }

    JeepHinge {
        id: driverWheel; objectName: "driverWheel"
        part: Rig.parts.driver_wheel; animate: false
        animatedAngle: vehicle.steeringPose.driverAngle
        JeepHinge {
            id: driverFrontSpin; objectName: "driverFrontSpin"
            part: vehicle.relativeWheelPart(Rig.parts.driver_front_spin, Rig.parts.driver_wheel)
            animate: false; animatedAngle: vehicle.wheelSpinAngle
        }
    }
    JeepHinge {
        id: passengerWheel; objectName: "passengerWheel"
        part: Rig.parts.passenger_wheel; animate: false
        animatedAngle: vehicle.steeringPose.passengerAngle
        JeepHinge {
            id: passengerFrontSpin; objectName: "passengerFrontSpin"
            part: vehicle.relativeWheelPart(Rig.parts.passenger_front_spin, Rig.parts.passenger_wheel)
            animate: false; animatedAngle: vehicle.wheelSpinAngle
        }
    }
    JeepHinge {
        id: driverRearSpin; objectName: "driverRearSpin"
        part: Rig.parts.driver_rear_spin
        animate: false; animatedAngle: vehicle.wheelSpinAngle
    }
    JeepHinge {
        id: passengerRearSpin; objectName: "passengerRearSpin"
        part: Rig.parts.passenger_rear_spin
        animate: false; animatedAngle: vehicle.wheelSpinAngle
    }
    Node {
        visible: vehicle.ready
        JeepLink { objectName: "pitmanArm"; start: Rig.steering.pitmanPivot; end: vehicle.steeringPose.pitman; radius: 0.030 }
        JeepLink { objectName: "dragLink"; start: vehicle.steeringPose.passenger; end: vehicle.steeringPose.pitman; radius: 0.019 }
        JeepLink { objectName: "tieRodDriver"; start: vehicle.tiePoints[0]; end: vehicle.tiePoints[1] }
        JeepLink { objectName: "tieRodCenter"; start: vehicle.tiePoints[1]; end: vehicle.tiePoints[2]; joints: false }
        JeepLink { objectName: "tieRodPassenger"; start: vehicle.tiePoints[2]; end: vehicle.tiePoints[3] }
        JeepLink {
            start: Steering.mix(vehicle.steeringPose.passenger,vehicle.steeringPose.pitman,.65)
            end: Steering.mix(vehicle.steeringPose.passenger,vehicle.steeringPose.pitman,.78)
            radius: .026; joints: false; tint: "#646d73"
        }
        JeepLink {
            start: Steering.mix(vehicle.tiePoints[1],vehicle.tiePoints[2],.68)
            end: Steering.mix(vehicle.tiePoints[1],vehicle.tiePoints[2],.82)
            radius: .022; joints: false; tint: "#646d73"
        }
        // The steering damper telescopes; the steering rods remain rigid.
        readonly property var damperStart: Rig.steering.damperStart
        readonly property var damperEnd: Steering.mix(vehicle.tiePoints[1],vehicle.tiePoints[2],Rig.steering.damperFraction)
        id: links
        JeepLink { start: links.damperStart; end: Steering.mix(links.damperStart,links.damperEnd,.60); radius: .027; tint: "#232a30" }
        JeepLink { start: Steering.mix(links.damperStart,links.damperEnd,.55); end: links.damperEnd; radius: .009; tint: "#adb7be" }
    }

    Repeater3D {
        model: [-1, 1]
        delegate: Node {
            id: lamps
            required property int modelData
            JeepLamp {
                objectName: "fogLight"
                on: vehicle.fogLightsOn
                position: Qt.vector3d(lamps.modelData * 0.495, -0.13, 1.877)
                eulerRotation: Qt.vector3d(-8, 180, 0)
                round: true; lensSize: Qt.vector3d(0.142, 0.142, 0.005)
                beamBrightness: 5; beamAngle: 80; beamInnerAngle: 50
            }
            JeepLamp {
                objectName: "brakeLight"
                on: vehicle.brakeLightsOn
                position: Qt.vector3d(lamps.modelData * 0.61, 0.145, -1.842)
                lensSize: Qt.vector3d(0.131, 0.126, 0.004)
                tint: "#ff2410"; emission: Qt.vector3d(2.0, 0.025, 0.008)
                beamBrightness: 0.8; beamAngle: 110; beamInnerAngle: 65
            }
            JeepLamp {
                objectName: "reverseLight"
                on: vehicle.reverseLightsOn
                position: Qt.vector3d(lamps.modelData * 0.61, 0.051, -1.845)
                lensSize: Qt.vector3d(0.125, 0.018, 0.003)
                tint: "#fff9ec"; emission: Qt.vector3d(1.6, 1.5, 1.3)
                beamBrightness: 3; beamAngle: 100; beamInnerAngle: 55
            }
        }
    }

    Repeater3D {
        model: [-0.423, 0.423]
        delegate: Node {
            required property real modelData
            position: Qt.vector3d(modelData, 0.197, 1.797)
            Model {
                objectName: "headlightGlow"
                visible: vehicle.headlightsOn
                source: "#Sphere"
                scale: Qt.vector3d(0.00206, 0.00206, 0.00032)
                materials: PrincipledMaterial {
                    baseColor: "#fff3d5"
                    lighting: PrincipledMaterial.NoLighting
                    emissiveFactor: Qt.vector3d(1.5, 1.3, 0.9)
                }
            }
            SpotLight {
                objectName: "headlightBeam"
                visible: vehicle.headlightsOn
                eulerRotation: Qt.vector3d(-5, 180, 0)
                color: "#fff0d0"
                brightness: 8
                coneAngle: 48
                innerConeAngle: 28
                constantFade: 1
                linearFade: 0.1
                quadraticFade: 0.02
                castsShadow: false
            }
        }
    }
}
