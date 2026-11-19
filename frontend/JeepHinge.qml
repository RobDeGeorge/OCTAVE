import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

Node {
    id: hinge
    required property var part
    // The folder under frontend/assets/ the part's file is in
    property string folder: "jeep_tj"
    property bool open: false
    property bool animate: true
    // A tap on the model is resolved by View3D.pick(), which sees only
    // pickable Models. A tappable hinge marks the Models of its part so, and
    // names in tapKey the JeepVehicle property the tap toggles ("" for a part
    // that only hides what is behind it, such as the body).
    property bool tappable: false
    property string tapKey: ""
    function _markPickable(node) {
        var kids = node.children
        for (var i = 0; i < kids.length; ++i) {
            if (kids[i].pickable !== undefined)
                kids[i].pickable = true
            _markPickable(kids[i])
        }
    }
    readonly property real angle: animatedAngle
    property real animatedAngle: open ? part.openAngle : 0
    readonly property bool moving: hingeAnimation.running
    readonly property bool closed: !moving && Math.abs(animatedAngle) < 0.1
    readonly property bool opened: !moving && Math.abs(animatedAngle - part.openAngle) < 0.1
    // (a part with no file, a pivot only, has nothing to wait for)
    readonly property bool ready: loader.status === RuntimeLoader.Success || !part.file
    readonly property string error: loader.status === RuntimeLoader.Error ? loader.errorString : ""
    position: Qt.vector3d(part.pivot[0], part.pivot[1], part.pivot[2])
    // Spinners (axis "Z") carry an "orient" quaternion that tilts their local
    // +Z onto the real shaft axis; hinges and wheels have no orient.
    readonly property quaternion orient: part.orient
        ? Qt.quaternion(part.orient[0], part.orient[1], part.orient[2], part.orient[3])
        : Qt.quaternion(1, 0, 0, 0)
    readonly property vector3d axisVector: part.axis === "Y" ? Qt.vector3d(0, 1, 0)
        : part.axis === "Z" ? Qt.vector3d(0, 0, 1) : Qt.vector3d(1, 0, 0)
    rotation: orient.times(Quaternion.fromAxisAndAngle(axisVector, animatedAngle))
    Behavior on animatedAngle { enabled: hinge.animate; NumberAnimation { id: hingeAnimation; duration: 450; easing.type: Easing.InOutCubic } }
    RuntimeLoader {
        id: loader
        // assetStore hands Assimp a real file (extracted from the APK on Android)
        // (null-guarded: at shutdown the backend drops assetStore before the scene dies)
        source: assetStore && hinge.part.file ? assetStore.localUrl(hinge.folder + "/" + hinge.part.file) : ""
        onStatusChanged: {
            if (status === RuntimeLoader.Error)
                console.warn("Jeep asset:", source, errorString)
            else if (status === RuntimeLoader.Success && hinge.tappable)
                hinge._markPickable(loader)
        }
    }
}
