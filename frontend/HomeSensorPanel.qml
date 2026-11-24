// HomeSensorPanel.qml
//
// The sensors on the Home screen, the third panel beside music and the OBD
// readings: G-force, compass and tilt (pitch / roll) from the motion sensor,
// one tile each. The tiles stand in a column, or in a row while `inRow`
// (Home in an upright window, where the panel is a strip under the
// readings). With no sensor connected it says so, as the OBD panel says
// "No vehicle connected". A tap anywhere opens the Sensors page.
//
// A further sensor is a further entry in `tiles` and a Component for it.
import QtQuick
import QtQuick.Layouts
import "." as App
import "gauges" as Gauges

Rectangle {
    id: panel
    objectName: "homeSensorPanel"

    // Local dp wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    signal clicked()

    // Tiles side by side (an upright window) instead of one above the other
    property bool inRow: false

    // (null-guarded: at shutdown the backend drops berryIMU before the page dies)
    readonly property bool imuConnected: typeof berryIMU !== "undefined" && !!berryIMU && berryIMU.connected

    readonly property var tiles: [
        { title: "G-FORCE", kind: "gforce" },
        { title: "COMPASS", kind: "compass" },
        { title: "TILT", kind: "tilt" }
    ]

    function degrees(value) {
        if (!isFinite(value)) return "--"
        var n = Math.round(value)
        return (n === 0 ? 0 : n) + "°"   // never "-0"
    }

    color: "transparent"
    border.color: App.Style.accent
    border.width: 2
    radius: dpMin(8, 2)

    GridLayout {
        visible: panel.imuConnected
        anchors.fill: parent
        anchors.margins: panel.dp(15)
        columns: panel.inRow ? panel.tiles.length : 1
        columnSpacing: panel.dp(10)
        rowSpacing: panel.dp(10)
        // Equal tiles whatever is in them
        uniformCellWidths: true
        uniformCellHeights: true

        Repeater {
            model: panel.tiles
            // The look of the reading cards (HomeOBDView)
            delegate: Rectangle {
                id: tile
                required property var modelData
                objectName: "homeSensorTile_" + modelData.kind
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: App.Style.backgroundColor
                border.color: App.Style.accent
                border.width: 1
                radius: 3

                Text {
                    id: tileTitle
                    anchors { top: parent.top; left: parent.left; margins: panel.dp(5) }
                    text: tile.modelData.title
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.mainMenuOBDTextSize * 0.8
                    font.family: App.Style.fontFamily
                }
                Loader {
                    anchors {
                        top: tileTitle.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
                        margins: panel.dp(6)
                    }
                    sourceComponent: tile.modelData.kind === "gforce" ? gforceTile
                                   : tile.modelData.kind === "compass" ? compassTile : tiltTile
                }
            }
        }
    }

    Component {
        id: gforceTile
        Gauges.GForceGauge { }
    }
    Component {
        id: compassTile
        Gauges.CompassGauge { }
    }
    Component {
        id: tiltTile
        Item {
            id: tilt
            readonly property real pitch: App.OBDParameterModel.sample("PITCH")
            readonly property real roll: App.OBDParameterModel.sample("ROLL")
            // Side by side while both fit, else one above the other
            GridLayout {
                anchors.centerIn: parent
                columns: implicitTwoWide <= tilt.width ? 2 : 1
                readonly property real implicitTwoWide: pitchReadout.implicitWidth + rollReadout.implicitWidth + columnSpacing
                columnSpacing: panel.dp(24)
                rowSpacing: panel.dp(6)
                TiltReadout { id: pitchReadout; label: "PITCH"; value: panel.degrees(tilt.pitch) }
                TiltReadout { id: rollReadout; label: "ROLL"; value: panel.degrees(tilt.roll) }
            }
        }
    }

    component TiltReadout: Column {
        property string label
        property string value
        Layout.alignment: Qt.AlignHCenter
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.value
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.mainMenuOBDDataSize * 0.7
            font.bold: true
            font.family: App.Style.fontFamily
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.label
            color: App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.mainMenuOBDTextSize * 0.8
            font.family: App.Style.fontFamily
        }
    }

    // No motion sensor
    Column {
        objectName: "homeSensorPrompt"
        visible: !panel.imuConnected
        anchors.centerIn: parent
        width: parent.width - panel.dp(30)
        spacing: panel.dp(8)
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "No sensors connected"
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText * 1.2
            font.bold: true
            font.family: App.Style.fontFamily
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "G-force, compass and tilt show here with a motion sensor"
            color: App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.overallText * 0.9
            font.family: App.Style.fontFamily
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: panel.clicked()
    }
}
