import QtQuick
import QtQuick3D

Node {
    id: link
    required property var start
    required property var end
    property real radius: 0.016
    property color tint: "#424b52"
    property bool joints: true
    readonly property real length: Math.sqrt(Math.pow(end[0]-start[0],2)+Math.pow(end[1]-start[1],2)+Math.pow(end[2]-start[2],2))
    position: Qt.vector3d((start[0]+end[0])/2,(start[1]+end[1])/2,(start[2]+end[2])/2)
    rotation: {
        if (length < 1e-8) return Qt.quaternion(1,0,0,0)
        var x=(end[0]-start[0])/length,y=(end[1]-start[1])/length,z=(end[2]-start[2])/length
        if (y < -0.999999) return Qt.quaternion(0,1,0,0)
        var n=Math.sqrt((1+y)*(1+y)+z*z+x*x)
        return Qt.quaternion((1+y)/n,z/n,0,-x/n)
    }
    PrincipledMaterial { id: steel; baseColor: link.tint; metalness: 0.65; roughness: 0.42 }
    Model {
        source: "#Cylinder"
        scale: Qt.vector3d(link.radius/50,link.length/100,link.radius/50)
        materials: steel
    }
    Model {
        visible: link.joints
        source: "#Sphere"
        y: link.length/2
        scale: Qt.vector3d(0.00036,0.00036,0.00036)
        materials: steel
    }
    Model {
        visible: link.joints
        source: "#Sphere"
        y: -link.length/2
        scale: Qt.vector3d(0.00036,0.00036,0.00036)
        materials: steel
    }
}
