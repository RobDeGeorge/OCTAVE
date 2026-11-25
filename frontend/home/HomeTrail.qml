// HomeTrail.qml
//
// Home for the trail: a heading tape across the top, an artificial horizon
// with the vehicle standing on it (the world tilts with pitch and roll, as
// in an aircraft), the G-force bubble, pitch and roll in figures, and a row
// of readings along the bottom. Upright, the horizon takes the width and
// the rest stacks under it.
import QtQuick
import QtQuick.Controls
import ".." as App
import "../gauges" as Gauges

HomeScreen {
    id: home
    objectName: "homeTrail"

    paramIds: ["SPEED", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE"]
    usesImu: true

    readonly property real pad: 2.6 * u
    readonly property real tapeH: 12 * u
    readonly property real footH: portrait ? 30 * u : 17 * u
    readonly property real midY: pad + tapeH + pad
    readonly property real midH: height - midY - footH - 2 * pad
    readonly property real sideW: 26 * u
    // Upright: the horizon over a row of bubble + figures
    readonly property real rowH: 24 * u

    readonly property color line: App.Style.accent
    readonly property color faint: Qt.rgba(App.Style.obdLabelColor.r, App.Style.obdLabelColor.g, App.Style.obdLabelColor.b, 0.35)
    readonly property color skyColor: Qt.tint(App.Style.obdBoxBackground, Qt.rgba(line.r, line.g, line.b, 0.22))
    readonly property color groundColor: Qt.tint(App.Style.obdBoxBackground,
        Qt.rgba(App.Style.statusWarning.r, App.Style.statusWarning.g, App.Style.statusWarning.b, 0.20))

    readonly property real heading: has("HEADING") ? value("HEADING") : 0
    readonly property real pitch: has("PITCH") ? value("PITCH") : 0
    readonly property real roll: has("ROLL") ? value("ROLL") : 0

    Rectangle { anchors.fill: parent; color: App.Style.backgroundColor }

    // Four corner marks around a panel (still: nothing here animates by itself)
    component Brackets: Item {
        id: brackets
        anchors.fill: parent
        property real arm: Math.max(6, 2.2 * home.u)
        property color ink: home.line
        Repeater {
            model: 4
            Item {
                required property int index
                id: corner
                readonly property bool atRight: index % 2 === 1
                readonly property bool atBottom: index >= 2
                x: atRight ? brackets.width - brackets.arm : 0
                y: atBottom ? brackets.height - brackets.arm : 0
                width: brackets.arm; height: brackets.arm
                Rectangle { width: corner.width; height: 2; y: corner.atBottom ? corner.height - 2 : 0; color: brackets.ink }
                Rectangle { width: 2; height: corner.height; x: corner.atRight ? corner.width - 2 : 0; color: brackets.ink }
            }
        }
    }

    // A reading in a bracketed box: NAME over the figure and its unit
    component Tile: Rectangle {
        id: tile
        property string paramId: ""
        property string name: home.titleOf(paramId)
        property int decimals: -1
        color: App.Style.obdBoxBackground
        Brackets {}
        Column {
            anchors.centerIn: parent
            width: parent.width - 3 * home.u
            spacing: tile.height * 0.03
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: tile.name.toUpperCase()
                color: App.Style.obdLabelColor
                font.family: home.font
                font.pixelSize: Math.max(9, Math.min(tile.height * 0.15, tile.width * 0.1))
                font.letterSpacing: (Math.max(9, Math.min(tile.height * 0.15, tile.width * 0.1))) * 0.22
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: tile.height * 0.05
                Text {
                    id: tileFigure
                    text: tile.decimals >= 0 ? home.text(tile.paramId, tile.decimals) : home.text(tile.paramId)
                    color: App.Style.obdValueColor
                    font.family: home.font
                    font.pixelSize: Math.max(12, Math.min(tile.height * 0.42, tile.width * 0.26))
                    font.bold: true
                }
                Text {
                    anchors.baseline: tileFigure.baseline
                    text: home.unitOf(tile.paramId)
                    color: App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: Math.max(9, tileFigure.font.pixelSize * 0.4)
                }
            }
        }
    }

    // ── Heading tape ──
    Rectangle {
        id: tape
        objectName: "trailTape"
        x: home.pad; y: home.pad
        width: home.width - 2 * home.pad
        height: home.tapeH
        color: App.Style.obdBoxBackground
        clip: true
        readonly property real perDegree: width / 120          // 60 degrees each side
        readonly property int base: Math.round(home.heading / 5) * 5

        Repeater {
            model: 27
            Item {
                required property int index
                readonly property int degrees: tape.base + (index - 13) * 5
                readonly property int on: ((degrees % 360) + 360) % 360
                readonly property bool major: on % 30 === 0
                x: tape.width / 2 + (degrees - home.heading) * tape.perDegree
                Rectangle {
                    x: -width / 2
                    y: tape.height - height
                    width: parent.major ? 2 : 1
                    height: tape.height * (parent.major ? 0.34 : 0.18)
                    color: parent.major ? App.Style.obdValueColor : home.faint
                }
                Text {
                    visible: parent.major
                    anchors.horizontalCenter: parent.left
                    y: tape.height * 0.12
                    text: parent.on % 90 === 0 ? "NESW".charAt(parent.on / 90) : parent.on
                    color: parent.on % 90 === 0 ? home.line : App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: tape.height * (parent.on % 90 === 0 ? 0.36 : 0.26)
                    font.bold: parent.on % 90 === 0
                }
            }
        }
        // Where the nose points
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(2, 0.4 * home.u)
            height: parent.height
            color: home.line
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: headingText.implicitWidth + 3 * home.u
            height: parent.height * 0.5
            color: home.line
            Text {
                id: headingText
                anchors.centerIn: parent
                text: home.has("HEADING") ? Math.round(home.heading) % 360 + "° " + home.cardinal(home.heading) : "--"
                color: App.Style.obdBoxBackground
                font.family: home.font
                font.pixelSize: parent.height * 0.62
                font.bold: true
            }
        }
        Brackets {}
    }

    // ── Artificial horizon ──
    Rectangle {
        id: horizon
        objectName: "trailHorizon"
        x: home.portrait ? home.pad : home.pad + home.midH + home.pad
        y: home.midY
        width: home.portrait ? home.width - 2 * home.pad : home.width - x - home.sideW - 2 * home.pad
        height: home.portrait ? home.midH - home.rowH - home.pad : home.midH
        color: home.skyColor
        clip: true
        readonly property real perDegree: height / 50

        // The world: turned by roll, then slid by pitch
        Item {
            anchors.centerIn: parent
            width: 1; height: 1
            rotation: -home.roll
            Behavior on rotation { RotationAnimation { duration: 180; direction: RotationAnimation.Shortest } }
            Item {
                y: home.pitch * horizon.perDegree
                Behavior on y { NumberAnimation { duration: 180 } }
                Rectangle {
                    x: -width / 2
                    width: (horizon.width + horizon.height) * 2
                    height: width
                    color: home.groundColor
                }
                Rectangle { x: -width / 2; y: -1; width: (horizon.width + horizon.height) * 2; height: 2; color: App.Style.obdValueColor }
                // The pitch ladder, every ten degrees
                Repeater {
                    model: [-30, -20, -10, 10, 20, 30]
                    Item {
                        required property int modelData
                        y: -modelData * horizon.perDegree
                        Rectangle { x: -width / 2; y: -0.5; width: horizon.width * 0.16; height: 1; color: home.faint }
                        Text {
                            x: horizon.width * 0.09
                            anchors.verticalCenter: parent.top
                            text: Math.abs(modelData)
                            color: home.faint
                            font.family: home.font
                            font.pixelSize: Math.max(8, horizon.height * 0.045)
                        }
                    }
                }
            }
        }

        // The vehicle, fixed, with its wings
        App.VehiclePicture {
            id: trailVehicle
            anchors.centerIn: parent
            width: parent.width * 0.5
            height: parent.height * 0.62
            opacity: 0.95
        }
        Repeater {
            model: 2
            Rectangle {
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                x: index === 0 ? horizon.width * 0.06 : horizon.width * 0.94 - width
                width: horizon.width * 0.14
                height: Math.max(3, 0.6 * home.u)
                radius: height / 2
                color: home.line
            }
        }
        Text {
            visible: !home.imuConnected
            x: 2 * home.u; y: 1.5 * home.u
            text: "NO SENSOR"
            color: App.Style.obdLabelColor
            font.family: home.font
            font.pixelSize: Math.max(9, 2.4 * home.u)
            font.letterSpacing: (Math.max(9, 2.4 * home.u)) * 0.2
        }
        App.OBDConnectButton {
            objectName: "trailConnectButton"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2 * home.u
            visible: !home.obdConnected
            mainWindow: home.mainWindow
            fontFamily: home.font
        }
        Brackets {}
    }

    // ── G-force bubble ──
    Rectangle {
        id: bubble
        objectName: "trailBubble"
        x: home.pad
        y: home.portrait ? horizon.y + horizon.height + home.pad : home.midY
        width: home.portrait ? home.rowH : home.midH
        height: width
        color: App.Style.obdBoxBackground
        Gauges.GForceGauge { anchors.fill: parent; anchors.margins: home.u }
        Brackets {}
    }

    // ── Pitch and roll, in figures ──
    Tile {
        id: pitchTile
        objectName: "trailPitch"
        paramId: "PITCH"; decimals: 0
        x: home.portrait ? bubble.x + bubble.width + home.pad : home.width - home.sideW - home.pad
        y: home.portrait ? bubble.y : home.midY
        width: home.portrait ? (home.width - x - 2 * home.pad) / 2 : home.sideW
        height: home.portrait ? home.rowH : (home.midH - home.pad) / 2
    }
    Tile {
        objectName: "trailRoll"
        paramId: "ROLL"; decimals: 0
        x: home.portrait ? pitchTile.x + pitchTile.width + home.pad : pitchTile.x
        y: home.portrait ? pitchTile.y : pitchTile.y + pitchTile.height + home.pad
        width: pitchTile.width
        height: pitchTile.height
    }

    // ── Along the bottom ──
    Grid {
        id: foot
        objectName: "trailFoot"
        x: home.pad
        y: home.height - home.footH - home.pad
        columns: home.portrait ? 2 : 4
        spacing: home.pad
        readonly property real tileW: (home.width - 2 * home.pad - (columns - 1) * spacing) / columns
        readonly property real tileH: (home.footH - (Math.ceil(4 / columns) - 1) * spacing) / Math.ceil(4 / columns)
        Tile { paramId: "SPEED"; width: foot.tileW; height: foot.tileH }
        Tile { paramId: "ALTITUDE"; decimals: 0; width: foot.tileW; height: foot.tileH }
        Tile { paramId: "COOLANT_TEMP"; width: foot.tileW; height: foot.tileH }
        Tile { paramId: "CONTROL_MODULE_VOLTAGE"; name: "Battery"; width: foot.tileW; height: foot.tileH }
    }
}
