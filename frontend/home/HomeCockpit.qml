// HomeCockpit.qml
//
// Home as an instrument cluster: the tachometer in the middle with the
// speed inside it, a shift strip across the top, two leaning readouts on
// each side and what is playing along the bottom. Upright, the readouts go
// under the dial in two columns.
import QtQuick
import QtQuick.Controls
import ".." as App
import "../gauges" as Gauges

HomeScreen {
    id: home
    objectName: "homeCockpit"

    paramIds: ["RPM", "SPEED", "COOLANT_TEMP", "FUEL_LEVEL", "CONTROL_MODULE_VOLTAGE", "THROTTLE_POS",
               "ENGINE_LOAD", "INTAKE_TEMP"]

    readonly property real pad: 3 * u
    readonly property real stripH: 2.4 * u
    readonly property real footH: (portrait ? 13 : 12) * u
    readonly property real topY: pad + stripH + pad
    readonly property real dialRoom: height - topY - footH - 2 * pad - (portrait ? gridH + pad : 0)
    readonly property real dialSize: Math.max(10 * u, portrait ? Math.min(width - 2 * pad, dialRoom)
                                                           : Math.min(dialRoom, width * 0.42))
    readonly property real gridH: 30 * u      // the readouts under the dial, upright
    readonly property real wingW: (width - dialSize) / 2 - 2 * pad

    readonly property var sl: (typeof shiftLightManager !== "undefined" && shiftLightManager) ? shiftLightManager : null
    readonly property real redline: sl && sl.maxRpm > 0 ? sl.maxRpm : NaN
    readonly property color lampColor: sl && sl.lit && sl.activeFlag ? sl.activeFlag.color : "transparent"

    Rectangle { anchors.fill: parent; color: App.Style.backgroundColor }

    // A reading that leans: name, number, unit, and a strip of slanted
    // segments for where it stands in its range. `flip` leans it the other way
    // and sets it against the right edge.
    component Lean: Item {
        id: lean
        property string paramId: ""
        property bool flip: false
        // Red past these, in the reading's own unit (NaN: never)
        property real dangerAbove: NaN
        property real dangerBelow: NaN
        readonly property real raw: {
            var r = App.OBDParameterModel.rawValue(paramId)
            return typeof r === "number" ? r : NaN
        }
        readonly property bool danger: isFinite(raw) && ((isFinite(dangerAbove) && raw >= dangerAbove)
                                                         || (isFinite(dangerBelow) && raw <= dangerBelow))
        readonly property color ink: danger ? App.Style.statusDanger : App.Style.obdValueColor
        readonly property real level: home.norm(paramId)
        readonly property int segments: 12

        Text {
            id: leanTitle
            anchors.top: parent.top
            x: lean.flip ? lean.width - width : 0
            text: home.titleOf(lean.paramId).toUpperCase()
            color: App.Style.obdLabelColor
            font.family: home.font
            font.pixelSize: Math.max(9, lean.height * 0.16)
            font.letterSpacing: (Math.max(9, lean.height * 0.16)) * 0.18
        }
        Row {
            id: leanValue
            anchors.top: leanTitle.bottom
            x: lean.flip ? lean.width - width : 0
            spacing: lean.height * 0.06
            Text {
                id: leanNumber
                text: home.text(lean.paramId)
                color: lean.ink
                font.family: home.font
                font.pixelSize: Math.max(12, lean.height * 0.46)
                font.bold: true
                font.italic: true
            }
            Text {
                anchors.baseline: leanNumber.baseline
                text: home.unitOf(lean.paramId)
                color: App.Style.obdLabelColor
                font.family: home.font
                font.pixelSize: Math.max(9, lean.height * 0.18)
            }
        }
        Row {
            anchors.bottom: parent.bottom
            x: lean.flip ? lean.width - width : 0
            spacing: Math.max(2, lean.width * 0.012)
            layoutDirection: lean.flip ? Qt.RightToLeft : Qt.LeftToRight
            Repeater {
                model: lean.segments
                Rectangle {
                    required property int index
                    readonly property bool lit: home.has(lean.paramId) && index < Math.ceil(lean.level * lean.segments - 0.001)
                    width: Math.max(3, (Math.min(lean.width, 46 * home.u) - (lean.segments - 1) * parent.spacing) / lean.segments)
                    height: Math.max(4, lean.height * 0.13)
                    color: lit ? (lean.danger ? App.Style.statusDanger : App.Style.obdBarColor)
                               : Qt.rgba(App.Style.obdLabelColor.r, App.Style.obdLabelColor.g, App.Style.obdLabelColor.b, 0.16)
                    Behavior on color { ColorAnimation { duration: 160 } }
                    // x' = x + k y: a parallelogram
                    transform: Matrix4x4 {
                        matrix: Qt.matrix4x4(1, lean.flip ? 0.45 : -0.45, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1)
                    }
                }
            }
        }
    }

    // ── Shift strip: the revs across the top ──
    Row {
        id: shiftStrip
        objectName: "cockpitShiftStrip"
        y: home.pad
        anchors.horizontalCenter: parent.horizontalCenter
        readonly property int count: 28
        spacing: Math.max(2, 0.5 * home.u)
        Repeater {
            model: shiftStrip.count
            Rectangle {
                required property int index
                readonly property real at: (index + 1) / shiftStrip.count
                readonly property bool lit: home.has("RPM") && home.norm("RPM") >= at - 0.5 / shiftStrip.count
                width: Math.max(3, (home.width * (home.portrait ? 0.9 : 0.6) - (shiftStrip.count - 1) * shiftStrip.spacing) / shiftStrip.count)
                height: home.stripH
                radius: height * 0.3
                color: !lit ? Qt.rgba(App.Style.obdLabelColor.r, App.Style.obdLabelColor.g, App.Style.obdLabelColor.b, 0.14)
                            : at > 0.86 ? App.Style.statusDanger : at > 0.66 ? App.Style.statusWarning : App.Style.obdBarColor
            }
        }
    }

    // ── The dial ──
    Item {
        id: dial
        objectName: "cockpitDial"
        width: home.dialSize
        height: home.dialSize
        x: (home.width - width) / 2
        y: home.topY + (home.dialRoom - height) / 2 + home.pad / 2

        // The shift light, as a halo
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 1.04
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(2, parent.width * 0.02)
            border.color: home.lampColor
            visible: !!home.sl && home.sl.lit
        }
        Gauges.CircularGauge {
            anchors.fill: parent
            paramId: "RPM"
            showCenterReadout: false
            redlineStart: home.redline
        }
        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -parent.height * 0.02
            spacing: 0
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: home.text("SPEED")
                color: App.Style.obdValueColor
                font.family: home.font
                font.pixelSize: dial.height * 0.30
                font.bold: true
                font.italic: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: home.unitOf("SPEED").toUpperCase()
                color: App.Style.obdLabelColor
                font.family: home.font
                font.pixelSize: dial.height * 0.06
                font.letterSpacing: (dial.height * 0.06) * 0.3
            }
            Item { width: 1; height: dial.height * 0.04 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: home.obdConnected
                text: home.text("RPM") + "  RPM"
                color: App.Style.obdLabelColor
                font.family: home.font
                font.pixelSize: dial.height * 0.055
                font.letterSpacing: (dial.height * 0.055) * 0.15
            }
            App.OBDConnectButton {
                objectName: "cockpitConnectButton"
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !home.obdConnected
                mainWindow: home.mainWindow
                fontFamily: home.font
            }
        }
    }

    // ── The readouts: a wing each side, or a grid under the dial ──
    readonly property real leanH: portrait ? (gridH - pad) / 2 : Math.min(18 * u, (dialSize - 2 * pad) / 2)
    readonly property real leanW: portrait ? (width - 3 * pad) / 2 : wingW
    readonly property real wingY: dial.y + (dial.height - (2 * leanH + 2 * pad)) / 2
    readonly property real gridY: dial.y + dial.height + pad

    Lean {
        paramId: "COOLANT_TEMP"; dangerAbove: 110
        width: home.leanW; height: home.leanH
        x: home.pad
        y: home.portrait ? home.gridY : home.wingY
    }
    Lean {
        paramId: "FUEL_LEVEL"; dangerBelow: 10
        width: home.leanW; height: home.leanH
        x: home.pad
        y: (home.portrait ? home.gridY : home.wingY) + home.leanH + (home.portrait ? home.pad : 2 * home.pad)
    }
    Lean {
        paramId: "CONTROL_MODULE_VOLTAGE"; dangerBelow: 11.5; flip: true
        width: home.leanW; height: home.leanH
        x: home.width - home.pad - width
        y: home.portrait ? home.gridY : home.wingY
    }
    Lean {
        paramId: "THROTTLE_POS"; flip: true
        width: home.leanW; height: home.leanH
        x: home.width - home.pad - width
        y: (home.portrait ? home.gridY : home.wingY) + home.leanH + (home.portrait ? home.pad : 2 * home.pad)
    }

    // ── Along the bottom: the time, what is playing, two more readings ──
    Rectangle {
        id: foot
        objectName: "cockpitFoot"
        x: home.pad
        y: home.height - home.footH - home.pad
        width: home.width - 2 * home.pad
        height: home.footH
        radius: height * 0.18
        color: App.Style.obdBoxBackground
        border.width: 1
        border.color: Qt.rgba(App.Style.obdBarColor.r, App.Style.obdBarColor.g, App.Style.obdBarColor.b, 0.35)
        clip: true

        Text {
            id: footClock
            anchors.verticalCenter: parent.verticalCenter
            x: foot.height * 0.3
            text: home.clockText
            color: App.Style.obdValueColor
            font.family: home.font
            font.pixelSize: foot.height * 0.36
            font.bold: true
            font.italic: true
        }

        // Two small readings on the right (landscape has the room)
        Row {
            id: footStats
            visible: !home.portrait
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: foot.height * 0.3
            spacing: foot.height * 0.4
            Repeater {
                model: ["ENGINE_LOAD", "INTAKE_TEMP"]
                Column {
                    required property string modelData
                    Text {
                        anchors.right: parent.right
                        text: home.titleOf(modelData).toUpperCase()
                        color: App.Style.obdLabelColor
                        font.family: home.font
                        font.pixelSize: foot.height * 0.16
                        font.letterSpacing: (foot.height * 0.16) * 0.15
                    }
                    Text {
                        anchors.right: parent.right
                        text: home.text(modelData) + " " + home.unitOf(modelData)
                        color: App.Style.obdValueColor
                        font.family: home.font
                        font.pixelSize: foot.height * 0.28
                        font.bold: true
                    }
                }
            }
        }

        // What is playing, in the middle
        Item {
            id: footTrack
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: footClock.right
            anchors.right: home.portrait ? parent.right : footStats.left
            anchors.leftMargin: foot.height * 0.35
            anchors.rightMargin: foot.height * 0.35
            Image {
                id: footArt
                anchors.verticalCenter: parent.verticalCenter
                height: foot.height * 0.64
                width: height
                visible: home.hasTrack
                source: home.art
                sourceSize: Qt.size(128, 128)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: footArt.visible ? footArt.right : parent.left
                anchors.leftMargin: footArt.visible ? foot.height * 0.22 : 0
                anchors.right: parent.right
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.title : (home.musicEmpty ? "No Music Loaded" : "Nothing playing")
                    color: home.hasTrack ? App.Style.obdValueColor : App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: foot.height * 0.26
                    font.bold: true
                }
                Text {
                    width: parent.width
                    visible: home.hasTrack
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.artist : ""
                    color: App.Style.obdLabelColor
                    font.family: home.font
                    font.pixelSize: foot.height * 0.19
                }
            }
        }

        // How far through the track, as a hairline along the bottom edge
        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            height: Math.max(2, foot.height * 0.035)
            width: home.hasTrack && home.np.durationMs > 0
                   ? parent.width * Math.min(1, home.np.positionMs / home.np.durationMs) : 0
            color: App.Style.obdBarColor
        }
    }
}
