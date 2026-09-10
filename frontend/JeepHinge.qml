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
    eulerRotation: part.axis === "Y" ? Qt.vector3d(0, animatedAngle, 0) : Qt.vector3d(animatedAngle, 0, 0)
    Behavior on animatedAngle { enabled: hinge.animate; NumberAnimation { id: hingeAnimation; duration: 450; easing.type: Easing.InOutCubic } }
    RuntimeLoader {
        id: loader
        source: "./assets/jeep_tj/" + hinge.part.file
        onStatusChanged: if (status === RuntimeLoader.Error) console.warn("Jeep asset:", source, errorString)
    }
}
