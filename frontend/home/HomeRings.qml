// HomeRings.qml
//
// Home as rings: four readings as concentric rings that fill with their
// level (revs outermost, then speed, throttle and engine load), the speed in
// the middle, and beside them a legend of the four, the time and what is
// playing. Upright, the legend goes under the rings.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import ".." as App

HomeScreen {
    id: home
    objectName: "homeRings"

    paramIds: ["RPM", "SPEED", "THROTTLE_POS", "ENGINE_LOAD", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "FUEL_LEVEL"]

    readonly property real pad: 4 * u
    // The rings, outermost first
    readonly property var rings: [
        { "id": "RPM", "color": App.Style.obdBarColor },
        { "id": "SPEED", "color": App.Style.accent },
        { "id": "THROTTLE_POS", "color": App.Style.statusConnected },
        { "id": "ENGINE_LOAD", "color": App.Style.statusWarning }
    ]
    readonly property real ringSize: portrait ? Math.min(width - 2 * pad, height * 0.5)
                                              : Math.min(height - 2 * pad, width * 0.5)
    readonly property real stroke: ringSize * 0.058
    readonly property real ringGap: ringSize * 0.02

    Rectangle { anchors.fill: parent; color: App.Style.backgroundColor }

    // ── The rings ──
    Item {
        id: dial
        objectName: "ringsDial"
        width: home.ringSize
        height: home.ringSize
        x: home.portrait ? (home.width - width) / 2 : home.pad
        y: home.portrait ? home.pad : (home.height - height) / 2

        Repeater {
            model: home.rings
            Shape {
                id: ring
                required property var modelData
                required property int index
                anchors.fill: parent
                readonly property real r: dial.width / 2 - home.stroke / 2 - index * (home.stroke + home.ringGap)
                // Eased toward the reading, so the ring glides
                property real level: home.norm(modelData.id)
                Behavior on level { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

                ShapePath {
                    strokeColor: Qt.rgba(ring.modelData.color.r, ring.modelData.color.g, ring.modelData.color.b, 0.16)
                    strokeWidth: home.stroke
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: dial.width / 2; centerY: dial.height / 2
                        radiusX: ring.r; radiusY: ring.r
                        // (not a whole 360: the closed arc drew a stray hairline at its seam)
                        startAngle: -90; sweepAngle: 359.9
                    }
                }
                ShapePath {
                    strokeColor: home.has(ring.modelData.id) && ring.level > 0.002 ? ring.modelData.color : "transparent"
                    strokeWidth: home.stroke
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: dial.width / 2; centerY: dial.height / 2
                        radiusX: ring.r; radiusY: ring.r
                        startAngle: -90
                        sweepAngle: Math.max(0.5, 360 * ring.level)
                    }
                }
            }
        }

        // The speed, in the middle
        Column {
            anchors.centerIn: parent
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: home.text("SPEED")
                color: App.Style.obdValueColor
                font.family: home.font
                font.pixelSize: dial.width * 0.17
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: home.unitOf("SPEED")
                color: App.Style.obdLabelColor
                font.family: home.font
                font.pixelSize: dial.width * 0.045
            }
        }
    }

    // ── Beside (or under) the rings ──
    Column {
        id: side
        objectName: "ringsSide"
        x: home.portrait ? home.pad : dial.x + dial.width + home.pad
        y: home.portrait ? dial.y + dial.height + home.pad : home.pad
        width: home.width - x - home.pad
        height: home.height - y - home.pad
        spacing: 0
        readonly property real row: Math.min(11 * home.u, height * 0.105)

        // The legend: which ring is which, and its figure
        Repeater {
            model: home.rings
            Item {
                id: entry
                required property var modelData
                width: side.width
                height: side.row
                Rectangle {
                    id: dot
                    anchors.verticalCenter: parent.verticalCenter
                    width: side.row * 0.3; height: width; radius: width / 2
                    color: entry.modelData.color
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: dot.right
                    anchors.leftMargin: side.row * 0.35
                    text: App.OBDParameterModel.displayInfo(entry.modelData.id)
                          ? App.OBDParameterModel.displayInfo(entry.modelData.id).title : entry.modelData.id
                    color: App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: side.row * 0.36
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    text: home.text(entry.modelData.id) + " " + home.unitOf(entry.modelData.id)
                    color: App.Style.obdValueColor
                    font.family: home.font
                    font.pixelSize: side.row * 0.46
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1
                    color: Qt.rgba(App.Style.obdLabelColor.r, App.Style.obdLabelColor.g, App.Style.obdLabelColor.b, 0.18)
                }
            }
        }

        Item { width: 1; height: side.row * 0.35 }

        // Three quiet ones in a row
        Row {
            width: side.width
            height: side.row * 1.25
            Repeater {
                model: ["COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "FUEL_LEVEL"]
                Column {
                    required property string modelData
                    width: side.width / 3
                    Text {
                        text: home.text(modelData) + " " + home.unitOf(modelData)
                        color: App.Style.obdValueColor
                        font.family: home.font
                        font.pixelSize: side.row * 0.42
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: home.titleOf(modelData)
                        color: App.Style.obdLabelColor
                        font.family: home.font
                        font.pixelSize: side.row * 0.28
                    }
                }
            }
        }

        Item { width: 1; height: Math.max(0, side.height - 4 * side.row - side.row * 0.35 - side.row * 1.25 - footer.height) }

        // The time over what is playing
        Item {
            id: footer
            width: side.width
            height: Math.min(side.row * 3.2, side.height * 0.34)
            Text {
                id: bigClock
                text: home.clockText
                color: App.Style.obdValueColor
                font.family: home.font
                font.pixelSize: footer.height * 0.42
                font.weight: Font.Light
            }
            App.OBDConnectButton {
                objectName: "ringsConnectButton"
                anchors.right: parent.right
                anchors.verticalCenter: bigClock.verticalCenter
                visible: !home.obdConnected
                mainWindow: home.mainWindow
                fontFamily: home.font
            }
            Image {
                id: footerArt
                anchors.bottom: parent.bottom
                height: footer.height * 0.36
                width: height
                visible: home.hasTrack
                source: home.art
                sourceSize: Qt.size(128, 128)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Column {
                anchors.verticalCenter: footerArt.verticalCenter
                anchors.left: footerArt.visible ? footerArt.right : parent.left
                anchors.leftMargin: footerArt.visible ? footer.height * 0.1 : 0
                anchors.right: parent.right
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.title : (home.musicEmpty ? "No Music Loaded" : "Nothing playing")
                    color: home.hasTrack ? App.Style.obdValueColor : App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: footer.height * 0.15
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    visible: home.hasTrack
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.artist : ""
                    color: App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: footer.height * 0.12
                }
            }
        }
    }
}
