// GenericVehicle.qml
//
// Someone's own model as the vehicle: one glTF file, shown whole. It offers
// VehicleView the same members as the rigged JeepVehicle (ready, error, the
// radii, groundY, toggle(), the speeds a host binds), with nothing that
// moves. The model is centred and scaled to the size the camera frames, so
// a file in millimetres and one in metres look the same; `yaw` and `tilt`
// stand it the right way up first (vehicleManager.setOrientation).
import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

Node {
    id: vehicle
    objectName: "genericVehicle"

    // The model file (a file: URL) and its corrections, in degrees
    property url source
    property real yaw: 0
    property real tilt: 0
    // Unused: here so a host's bindings fit any vehicle
    property bool active: true
    property real engineRpm: 0
    property real wheelSpeedKph: 0

    readonly property bool ready: loader.status === RuntimeLoader.Success
    readonly property string error: loader.status === RuntimeLoader.Error ? loader.errorString : ""

    // The radius every model is scaled to, in the scene's metres: the built-in Jeep's
    readonly property real closedRadius: 2.13
    readonly property real sweptRadius: closedRadius
    readonly property real radius: closedRadius
    readonly property bool anyOpenRequested: false
    readonly property real groundY: _fit.groundY
    function toggle(key) {}

    // The model's box once turned by yaw / tilt: the scale that brings its
    // half-diagonal to closedRadius, its centre, and its underside
    readonly property var _fit: {
        var none = { scale: 1, centre: Qt.vector3d(0, 0, 0), groundY: -1 }
        if (!ready)
            return none
        var lo = loader.bounds.minimum, hi = loader.bounds.maximum
        var q = turn.rotation
        var min = Qt.vector3d(Infinity, Infinity, Infinity), max = Qt.vector3d(-Infinity, -Infinity, -Infinity)
        for (var i = 0; i < 8; ++i) {
            var p = q.times(Qt.vector3d(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z))
            min = Qt.vector3d(Math.min(min.x, p.x), Math.min(min.y, p.y), Math.min(min.z, p.z))
            max = Qt.vector3d(Math.max(max.x, p.x), Math.max(max.y, p.y), Math.max(max.z, p.z))
        }
        var half = max.minus(min).length() / 2
        if (!(half > 0) || !isFinite(half))
            return none
        var s = closedRadius / half
        return { scale: s, centre: min.plus(max).times(0.5), groundY: (min.y - max.y) / 2 * s }
    }

    Node {
        scale: Qt.vector3d(vehicle._fit.scale, vehicle._fit.scale, vehicle._fit.scale)
        position: vehicle._fit.centre.times(-vehicle._fit.scale)
        Node {
            id: turn
            eulerRotation: Qt.vector3d(vehicle.tilt, vehicle.yaw, 0)
            RuntimeLoader {
                id: loader
                source: vehicle.source
                onStatusChanged: if (status === RuntimeLoader.Error) console.warn("Vehicle model:", source, errorString)
            }
        }
    }
}
