// ImuDemand.qml
//
// A page or widget that shows IMU readings drops one in: while `wanted`
// (typically its own visibility) berryIMU runs at the full emit rate, and at
// the idle rate once nothing wants it. The IMU's twin of
// obdManager.setParameterDemand(consumer, ids).
import QtQuick

QtObject {
    // Stable name for this consumer (the page's objectName)
    property string consumer
    property bool wanted: false

    function _push(w) {
        if (typeof berryIMU !== "undefined" && berryIMU && consumer.length > 0)
            berryIMU.setDemand(consumer, w)
    }
    onWantedChanged: _push(wanted)
    Component.onCompleted: _push(wanted)
    Component.onDestruction: _push(false)
}
