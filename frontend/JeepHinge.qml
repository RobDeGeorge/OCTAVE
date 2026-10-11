import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

Node {
    id: hinge
    required property var part
    property bool open: false
    property bool animate: true
    readonly property real angle: animatedAngle
    property real animatedAngle: open ? part.openAngle : 0
    readonly property bool moving: hingeAnimation.running
    readonly property bool closed: !moving && Math.abs(animatedAngle) < 0.1
    readonly property bool opened: !moving && Math.abs(animatedAngle - part.openAngle) < 0.1
    readonly property bool ready: loader.status === RuntimeLoader.Success
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
        source: assetStore ? assetStore.localUrl("jeep_tj/" + hinge.part.file) : ""
        onStatusChanged: if (status === RuntimeLoader.Error) console.warn("Jeep asset:", source, errorString)
    }
}
