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

    // Loaded, and measured (see _measure): only then is it the right size and in the middle
    readonly property bool loaded: loader.status === RuntimeLoader.Success
    readonly property bool ready: loaded && _measured && _steady >= 2
    readonly property string error: loader.status === RuntimeLoader.Error ? loader.errorString : ""

    // The radius every model is scaled to, in the scene's metres: the built-in Jeep's
    readonly property real closedRadius: 2.13
    readonly property real sweptRadius: closedRadius
    readonly property real radius: closedRadius
    readonly property bool anyOpenRequested: false
    readonly property real groundY: _fit.groundY
    function toggle(key) {}

    // The loaded model's box, in the model's own space (before yaw / tilt).
    // RuntimeLoader.bounds is no use for this: it is worked out once, when the
    // load finishes, and the parts' meshes have no bounds until a frame or two
    // later, so it comes out as the box of the parts' origins (Qt 6.11). So
    // the box is measured here from every Model's own bounds, again and again
    // for a moment after the load, until it stops changing.
    property vector3d _lo
    property vector3d _hi
    property bool _measured: false
    property int _steady: 0
    function _measure() {
        var lo = Qt.vector3d(Infinity, Infinity, Infinity), hi = Qt.vector3d(-Infinity, -Infinity, -Infinity)
        var found = false
        var walk = function(node) {
            var kids = node.children
            for (var i = 0; i < kids.length; ++i) {
                var m = kids[i]
                if (m.geometry !== undefined && m.bounds !== undefined
                        && m.bounds.maximum.minus(m.bounds.minimum).length() > 0) {
                    var a = m.bounds.minimum, b = m.bounds.maximum
                    for (var c = 0; c < 8; ++c) {
                        var p = turn.mapPositionFromScene(m.mapPositionToScene(
                            Qt.vector3d(c & 1 ? b.x : a.x, c & 2 ? b.y : a.y, c & 4 ? b.z : a.z)))
                        lo = Qt.vector3d(Math.min(lo.x, p.x), Math.min(lo.y, p.y), Math.min(lo.z, p.z))
                        hi = Qt.vector3d(Math.max(hi.x, p.x), Math.max(hi.y, p.y), Math.max(hi.z, p.z))
                    }
                    found = true
                }
                walk(m)
            }
        }
        walk(loader)
        if (!found)
            return
        var same = _measured && lo.minus(_lo).length() < 1e-4 && hi.minus(_hi).length() < 1e-4
        _steady = same ? _steady + 1 : 0
        if (!same) {
            _lo = lo
            _hi = hi
            _measured = true
        }
    }
    Timer {
        property int ticks: 0
        interval: 100
        repeat: true
        running: vehicle.loaded && vehicle._steady < 5 && ticks < 100
        onRunningChanged: if (!running && !vehicle.loaded) ticks = 0
        onTriggered: { ticks += 1; vehicle._measure() }
    }

    // The model's box once turned by yaw / tilt: the scale that brings its
    // half-diagonal to closedRadius, its centre, and its underside
    readonly property var _fit: {
        var none = { scale: 1, centre: Qt.vector3d(0, 0, 0), groundY: -1 }
        if (!loaded || !_measured)
            return none
        var lo = _lo, hi = _hi
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
                onStatusChanged: {
                    if (status === RuntimeLoader.Error)
                        console.warn("Vehicle model:", source, errorString)
                    // A new model: measure it afresh
                    vehicle._measured = false
                    vehicle._steady = 0
                }
            }
        }
    }
}
