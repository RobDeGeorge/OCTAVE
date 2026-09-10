import QtQuick
import QtQuick3D

// Dimensions are metres; the parent vehicle supplies the scene scale.
Node {
    id: lamp
    property bool on: false
    property bool round: false
    property vector3d lensSize: Qt.vector3d(0.13, 0.13, 0.004)
    property color tint: "#fff4dc"
    property vector3d emission: Qt.vector3d(1.5, 1.3, 1.0)
    property real beamBrightness: 3
    property real beamAngle: 90
    property real beamInnerAngle: 45

    Model {
        objectName: lamp.objectName + "Glow"
        visible: lamp.on
        source: lamp.round ? "#Sphere" : "#Cube"
        scale: Qt.vector3d(lamp.lensSize.x / 100, lamp.lensSize.y / 100, lamp.lensSize.z / 100)
        materials: PrincipledMaterial {
            baseColor: lamp.tint
            lighting: PrincipledMaterial.NoLighting
            emissiveFactor: lamp.emission
        }
    }
    SpotLight {
        objectName: lamp.objectName + "Beam"
        visible: lamp.on
        z: -0.005
        color: lamp.tint
        brightness: lamp.beamBrightness
        coneAngle: lamp.beamAngle
        innerConeAngle: lamp.beamInnerAngle
        constantFade: 1
        linearFade: 0.15
        quadraticFade: 0.04
        castsShadow: false
    }
}
