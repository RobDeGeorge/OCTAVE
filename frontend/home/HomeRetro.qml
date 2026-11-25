// HomeRetro.qml
//
// Home as a 1980s digital dash: the speed in seven-segment figures (the
// unlit segments faintly there, as on the real thing), a bar-graph
// tachometer that climbs across the top, fuel and temperature as stacks of
// blocks, and one line of text for the rest. One colour, the theme's accent,
// on black, with scan lines over it.
import QtQuick
import QtQuick.Controls
import ".." as App

HomeScreen {
    id: home
    objectName: "homeRetro"

    paramIds: ["RPM", "SPEED", "COOLANT_TEMP", "FUEL_LEVEL", "CONTROL_MODULE_VOLTAGE"]

    readonly property color ink: App.Style.accent
    readonly property color ghost: Qt.rgba(ink.r, ink.g, ink.b, 0.11)
    readonly property color hot: App.Style.statusDanger
    readonly property string mono: "JetBrains Mono"
    readonly property real pad: 3 * u
    readonly property real tachH: (portrait ? 20 : 24) * u
    readonly property real lineH: 7 * u
    readonly property real midY: pad + tachH + 6 * u
    readonly property real midH: height - midY - lineH - 2 * pad

    Rectangle { anchors.fill: parent; color: "#040506" }

    // One seven-segment character: 0-9, "-" or blank
    component Seg: Item {
        id: seg
        property string ch: " "
        property color tone: home.ink
        readonly property real t: width * 0.17
        readonly property int mask: ({ "0": 0x3f, "1": 0x06, "2": 0x5b, "3": 0x4f, "4": 0x66, "5": 0x6d,
                                       "6": 0x7d, "7": 0x07, "8": 0x7f, "9": 0x6f, "-": 0x40 })[ch] || 0
        // bit 0 top, 1 top right, 2 bottom right, 3 bottom, 4 bottom left, 5 top left, 6 middle
        function ink(bit) { return ((mask >> bit) & 1) ? tone : home.ghost }
        Rectangle { x: seg.t * 0.6; y: 0; width: seg.width - seg.t * 1.2; height: seg.t; radius: seg.t / 2; color: seg.ink(0) }
        Rectangle { x: seg.width - seg.t; y: seg.t * 0.6; width: seg.t; height: seg.height / 2 - seg.t * 0.9; radius: seg.t / 2; color: seg.ink(1) }
        Rectangle { x: seg.width - seg.t; y: seg.height / 2 + seg.t * 0.3; width: seg.t; height: seg.height / 2 - seg.t * 0.9; radius: seg.t / 2; color: seg.ink(2) }
        Rectangle { x: seg.t * 0.6; y: seg.height - seg.t; width: seg.width - seg.t * 1.2; height: seg.t; radius: seg.t / 2; color: seg.ink(3) }
        Rectangle { x: 0; y: seg.height / 2 + seg.t * 0.3; width: seg.t; height: seg.height / 2 - seg.t * 0.9; radius: seg.t / 2; color: seg.ink(4) }
        Rectangle { x: 0; y: seg.t * 0.6; width: seg.t; height: seg.height / 2 - seg.t * 0.9; radius: seg.t / 2; color: seg.ink(5) }
        Rectangle { x: seg.t * 0.6; y: (seg.height - seg.t) / 2; width: seg.width - seg.t * 1.2; height: seg.t; radius: seg.t / 2; color: seg.ink(6) }
    }

    // A number in seven-segment figures, right-aligned in `places` figures
    // (a decimal point takes a narrow place of its own), leaning a little
    component SegNumber: Row {
        id: number
        property string text: ""
        property int places: 3
        property real size: 40
        property color tone: home.ink
        readonly property string shown: {
            var figures = text.replace(".", "").length
            var out = text
            for (var i = figures; i < places; i++) out = " " + out
            return out
        }
        spacing: size * 0.13
        transform: Matrix4x4 { matrix: Qt.matrix4x4(1, -0.12, 0, number.size * 0.12,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1) }
        Repeater {
            model: number.shown.length
            Item {
                required property int index
                readonly property string ch: number.shown.charAt(index)
                width: ch === "." ? number.size * 0.11 : number.size * 0.52
                height: number.size
                Seg { anchors.fill: parent; visible: parent.ch !== "."; ch: parent.ch; tone: number.tone }
                Rectangle {
                    visible: parent.ch === "."
                    anchors.bottom: parent.bottom
                    width: parent.width; height: width; radius: width / 2
                    color: number.tone
                }
            }
        }
    }

    // A stack of blocks, full at the top: fuel, temperature
    component Stack: Item {
        id: stack
        property string paramId: ""
        property string name: ""
        property string highMark: "F"
        property string lowMark: "E"
        // Red when the level is past this end ("high" or "low")
        property string dangerEnd: ""
        readonly property int blocks: 10
        readonly property real level: home.norm(paramId)
        readonly property bool danger: home.has(paramId) && (dangerEnd === "high" ? level > 0.85 : dangerEnd === "low" ? level < 0.15 : false)
        Text {
            id: stackName
            anchors.horizontalCenter: parent.horizontalCenter
            text: stack.name
            color: home.ink
            font.family: home.mono
            font.pixelSize: Math.max(9, 2.6 * home.u)
            font.letterSpacing: 0.6 * home.u
        }
        Column {
            id: stackBlocks
            anchors.top: stackName.bottom
            anchors.topMargin: home.u
            anchors.bottom: stackValue.top
            anchors.bottomMargin: home.u
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Math.max(2, 0.5 * home.u)
            Repeater {
                model: stack.blocks
                Rectangle {
                    required property int index
                    readonly property bool lit: home.has(stack.paramId) && (stack.blocks - index) <= Math.ceil(stack.level * stack.blocks - 0.001)
                    width: Math.min(stack.width * 0.5, 12 * home.u)
                    height: Math.max(2, (stackBlocks.height - (stack.blocks - 1) * stackBlocks.spacing) / stack.blocks)
                    color: lit ? (stack.danger ? home.hot : home.ink) : home.ghost
                }
            }
        }
        Text {
            anchors.right: stackBlocks.left; anchors.rightMargin: home.u; anchors.top: stackBlocks.top
            text: stack.highMark; color: home.ink; font.family: home.mono; font.pixelSize: Math.max(9, 2.4 * home.u)
        }
        Text {
            anchors.right: stackBlocks.left; anchors.rightMargin: home.u; anchors.bottom: stackBlocks.bottom
            text: stack.lowMark; color: home.ink; font.family: home.mono; font.pixelSize: Math.max(9, 2.4 * home.u)
        }
        Text {
            id: stackValue
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            text: home.text(stack.paramId) + " " + home.unitOf(stack.paramId)
            color: stack.danger ? home.hot : home.ink
            font.family: home.mono
            font.pixelSize: Math.max(9, 2.8 * home.u)
        }
    }

    // ── The tachometer: bars that climb ──
    Item {
        id: tach
        objectName: "retroTach"
        x: home.pad; y: home.pad
        width: home.width - 2 * home.pad
        height: home.tachH
        readonly property int bars: home.portrait ? 30 : 48
        readonly property real gap: Math.max(2, 0.5 * home.u)
        readonly property real barW: (width - (bars - 1) * gap) / bars
        readonly property var info: App.OBDParameterModel.displayInfo("RPM")
        readonly property real span: info ? info.max : 8000

        Repeater {
            model: tach.bars
            Rectangle {
                required property int index
                readonly property real at: (index + 1) / tach.bars
                readonly property bool lit: home.has("RPM") && home.norm("RPM") >= at - 0.5 / tach.bars
                x: index * (tach.barW + tach.gap)
                width: tach.barW
                height: tach.height * (0.22 + 0.78 * Math.pow(at, 1.7))
                y: tach.height - height
                color: lit ? (at > 0.82 ? home.hot : home.ink) : home.ghost
            }
        }
        // The scale under it, in thousands
        Repeater {
            model: Math.floor(tach.span / 1000) + 1
            Text {
                required property int index
                x: tach.width * (index * 1000 / tach.span) - (index === 0 ? 0 : width / 2)
                y: tach.height + 0.8 * home.u
                visible: x + width <= tach.width
                text: index
                color: home.ink
                font.family: home.mono
                font.pixelSize: Math.max(9, 2.6 * home.u)
            }
        }
        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            text: "RPM ×1000"
            color: home.ink
            font.family: home.mono
            font.pixelSize: Math.max(9, 2.6 * home.u)
            font.letterSpacing: 0.4 * home.u
        }
    }

    // ── The speed ──
    readonly property real figure: Math.min(midH * (portrait ? 0.46 : 0.78), width * (portrait ? 0.30 : 0.19))
    Item {
        id: speed
        objectName: "retroSpeed"
        width: speedNumber.width + 2 * home.u + speedUnit.width
        height: home.figure
        x: (home.width - width) / 2
        y: home.midY + (home.portrait ? 0 : (home.midH - height) / 2)
        SegNumber {
            id: speedNumber
            text: home.text("SPEED", 0)
            places: 3
            size: home.figure
        }
        Text {
            id: speedUnit
            anchors.left: speedNumber.right
            anchors.leftMargin: 2 * home.u
            anchors.bottom: parent.bottom
            text: home.unitOf("SPEED").toUpperCase()
            color: home.ink
            font.family: home.mono
            font.pixelSize: Math.max(10, home.figure * 0.16)
        }
    }

    // ── Fuel and temperature, a stack each side (or side by side, upright) ──
    readonly property real stackW: 22 * u
    readonly property real stackH: portrait ? midH - figure - 3 * pad : midH
    readonly property real stackY: portrait ? midY + figure + 3 * pad : midY
    Stack {
        paramId: "FUEL_LEVEL"; name: "FUEL"; highMark: "F"; lowMark: "E"; dangerEnd: "low"
        x: home.portrait ? home.width / 2 - width - home.pad : home.pad
        y: home.stackY
        width: home.stackW; height: home.stackH
    }
    Stack {
        paramId: "COOLANT_TEMP"; name: "TEMP"; highMark: "H"; lowMark: "C"; dangerEnd: "high"
        x: home.portrait ? home.width / 2 + home.pad : home.width - width - home.pad
        y: home.stackY
        width: home.stackW; height: home.stackH
    }

    // ── One line for the rest: the time, the track, the battery ──
    Item {
        id: line
        objectName: "retroLine"
        x: home.pad
        y: home.height - home.lineH - home.pad
        width: home.width - 2 * home.pad
        height: home.lineH
        Rectangle { width: parent.width; height: 1; color: home.ink; opacity: 0.5 }
        Text {
            id: lineClock
            anchors.verticalCenter: parent.verticalCenter
            text: home.clockText
            color: home.ink
            font.family: home.mono
            font.pixelSize: line.height * 0.5
        }
        Row {
            id: lineVolts
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            spacing: 1.5 * home.u
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "BATT"
                color: home.ink
                font.family: home.mono
                font.pixelSize: line.height * 0.34
            }
            SegNumber {
                anchors.verticalCenter: parent.verticalCenter
                text: home.text("CONTROL_MODULE_VOLTAGE", 1)
                places: 3
                size: line.height * 0.6
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: lineClock.right
            anchors.right: lineVolts.left
            anchors.leftMargin: 3 * home.u
            anchors.rightMargin: 3 * home.u
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: home.hasTrack ? ("♪ " + home.np.title + (home.np.artist ? " - " + home.np.artist : "")).toUpperCase()
                                : (home.obdConnected ? "SYSTEMS OK" : "NO VEHICLE")
            color: home.ink
            font.family: home.mono
            font.pixelSize: line.height * 0.36
        }
    }

    App.OBDConnectButton {
        objectName: "retroConnectButton"
        anchors.horizontalCenter: parent.horizontalCenter
        y: speed.y + speed.height + home.pad
        visible: !home.obdConnected
        mainWindow: home.mainWindow
        fontFamily: home.font
    }

    // ── Scan lines over the lot (painted once) ──
    Canvas {
        anchors.fill: parent
        opacity: 0.22
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#000000"
            var step = Math.max(3, Math.round(home.u * 0.55))
            for (var y = 0; y < height; y += step)
                ctx.fillRect(0, y, width, 1)
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
