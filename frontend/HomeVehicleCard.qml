// HomeVehicleCard.qml
//
// The vehicle on the Home screen, beside the reading cards: its picture and,
// while a motion sensor is connected, pitch and roll under it. A picture, not
// the 3D scene: Home is on screen most of the drive. Shown while the
// homeVehicleCard setting is on (Settings > OBD > Parameters).
import QtQuick
import "." as App

Rectangle {
    id: card
    objectName: "homeVehicleCard"

    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    signal clicked()

    // (null-guarded: at shutdown the backend drops berryIMU before the page dies)
    readonly property bool imuConnected: typeof berryIMU !== "undefined" && !!berryIMU && berryIMU.connected
    readonly property real pitch: Number(App.OBDParameterModel.liveValue("PITCH")) || 0
    readonly property real roll: Number(App.OBDParameterModel.liveValue("ROLL")) || 0
    function degrees(value) {
        var n = Math.round(value)
        return (n === 0 ? 0 : n) + "°"   // never "-0"
    }

    // The look of the reading cards beside it (HomeOBDView)
    color: App.Style.backgroundColor
    border.color: App.Style.accent
    border.width: 1
    radius: 3

    App.VehiclePicture {
        anchors {
            top: parent.top; left: parent.left; right: parent.right
            bottom: readouts.visible ? readouts.top : parent.bottom
            margins: card.dp(12)
        }
    }

    Row {
        id: readouts
        visible: card.imuConnected
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: card.dp(8) }
        spacing: card.dp(28)
        Repeater {
            model: [{ label: "PITCH", value: card.degrees(card.pitch) }, { label: "ROLL", value: card.degrees(card.roll) }]
            delegate: Column {
                id: readout
                required property var modelData
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: readout.modelData.value
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.mainMenuOBDDataSize * 0.7
                    font.bold: true
                    font.family: App.Style.fontFamily
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: readout.modelData.label
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mainMenuOBDTextSize * 0.8
                    font.family: App.Style.fontFamily
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: card.clicked()
    }
}
