// HomeGlass.qml
//
// Home around the music: the album art, blurred, fills the screen; the
// cover, the track and its controls stand on one side and the drive on the
// other as panes of glass (the time, the speed, four small readings).
// Upright, the music is on top and the panes below.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import ".." as App

HomeScreen {
    id: home
    objectName: "homeGlass"

    paramIds: ["SPEED", "RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "FUEL_LEVEL"]

    readonly property real pad: 4 * u
    readonly property color ink: "#FFFFFF"
    readonly property color dim: Qt.rgba(1, 1, 1, 0.62)
    // The two halves
    readonly property rect musicBox: portrait ? Qt.rect(pad, pad, width - 2 * pad, height * 0.46 - pad)
                                              : Qt.rect(pad, pad, width * 0.44 - pad, height - 2 * pad)
    readonly property rect driveBox: portrait ? Qt.rect(pad, height * 0.46 + pad, width - 2 * pad, height * 0.54 - 2 * pad)
                                              : Qt.rect(width * 0.44 + pad, pad, width * 0.56 - 2 * pad, height - 2 * pad)

    // ── The art, out of focus, behind everything ──
    Rectangle { anchors.fill: parent; color: "#101014" }
    Item {
        anchors.fill: parent
        clip: true
        Image {
            id: backdrop
            anchors.fill: parent
            anchors.margins: -home.u * 6      // the blur's soft edge stays off screen
            source: home.art
            // (a small source: the blur wants no detail, and a Pi no big texture)
            sourceSize: Qt.size(96, 96)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: home.hasTrack
            layer.enabled: status === Image.Ready
            layer.effect: MultiEffect { blurEnabled: true; blurMax: 48; blur: 1.0; saturation: 0.25 }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#66000000" }
            GradientStop { position: 1; color: "#B8000000" }
        }
    }

    // A pane of glass
    component Pane: Rectangle {
        radius: Math.min(width, height, 60 * home.u) * 0.12
        color: Qt.rgba(1, 1, 1, 0.09)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.22)
    }
    // A round button with a white glyph (the app's flat SVGs)
    component GlassButton: Rectangle {
        id: glassButton
        property url icon: ""
        property bool big: false
        signal tapped()
        width: height
        radius: height / 2
        color: glassMouse.pressed ? Qt.rgba(1, 1, 1, 0.30) : Qt.rgba(1, 1, 1, big ? 0.18 : 0.0)
        border.width: big ? 1 : 0
        border.color: Qt.rgba(1, 1, 1, 0.3)
        scale: glassMouse.pressed ? 0.9 : 1
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        App.ThemedIcon {
            anchors.centerIn: parent
            width: parent.width * 0.46
            height: width
            source: glassButton.icon
            color: home.ink
            shadow: false
        }
        MouseArea { id: glassMouse; anchors.fill: parent; onClicked: glassButton.tapped() }
    }

    // ── The music ──
    Item {
        id: music
        objectName: "glassMusic"
        x: home.musicBox.x; y: home.musicBox.y
        width: home.musicBox.width; height: home.musicBox.height

        App.MusicEmptyPrompt {
            objectName: "homeMusicPrompt"
            anchors.centerIn: parent
            width: parent.width - home.pad
            maxHeight: parent.height
            visible: home.musicEmpty && !home.hasTrack
            stackView: home.stackView
            mainWindow: home.mainWindow
            fontFamily: home.font
        }

        Item {
            id: player
            anchors.fill: parent
            visible: !(home.musicEmpty && !home.hasTrack)
            // Upright the cover stands beside the words; else above them
            readonly property bool side: home.portrait
            readonly property real cover: side ? Math.min(height, width * 0.42)
                                               : Math.min(width, height - 34 * home.u)

            Item {
                id: coverBox
                width: player.cover
                height: player.cover
                x: player.side ? 0 : (player.width - width) / 2
                y: 0
                Image {
                    id: coverImage
                    anchors.fill: parent
                    source: home.art
                    sourceSize: Qt.size(640, 640)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: false
                }
                Rectangle {
                    id: coverMask
                    anchors.fill: parent
                    radius: width * 0.07
                    visible: false
                    layer.enabled: true
                }
                MultiEffect {
                    anchors.fill: parent
                    source: coverImage
                    maskEnabled: true
                    maskSource: coverMask
                    // (a hard edge: without these the mask fades the whole cover a little)
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                    shadowEnabled: true
                    shadowColor: "#000000"
                    shadowOpacity: 0.5
                    shadowBlur: 0.8
                    shadowVerticalOffset: home.u
                }
            }

            Column {
                id: words
                x: player.side ? coverBox.width + home.pad : 0
                y: player.side ? Math.max(0, (player.height - height) / 2) : coverBox.height + 3 * home.u
                width: player.side ? player.width - x : player.width
                spacing: 1.2 * home.u

                Text {
                    width: parent.width
                    horizontalAlignment: player.side ? Text.AlignLeft : Text.AlignHCenter
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.title : "Nothing playing"
                    color: home.ink
                    font.family: home.font
                    font.pixelSize: 4.6 * home.u
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: player.side ? Text.AlignLeft : Text.AlignHCenter
                    elide: Text.ElideRight
                    text: home.hasTrack ? home.np.artist : ""
                    color: home.dim
                    font.family: home.font
                    font.pixelSize: 3.2 * home.u
                }
                // How far through, with the times under it
                Item {
                    width: parent.width
                    height: 5.4 * home.u
                    readonly property real part: home.hasTrack && home.np.durationMs > 0
                                                 ? Math.min(1, home.np.positionMs / home.np.durationMs) : 0
                    Rectangle {
                        id: track
                        y: 1.2 * home.u
                        width: parent.width
                        height: Math.max(3, 0.7 * home.u)
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.22)
                        Rectangle {
                            width: parent.width * parent.parent.part
                            height: parent.height
                            radius: height / 2
                            color: home.ink
                        }
                    }
                    Text {
                        anchors.top: track.bottom
                        anchors.topMargin: 0.8 * home.u
                        text: home.hasTrack ? home.duration(home.np.positionMs) : ""
                        color: home.dim
                        font.family: home.font
                        font.pixelSize: 2.4 * home.u
                    }
                    Text {
                        anchors.top: track.bottom
                        anchors.topMargin: 0.8 * home.u
                        anchors.right: parent.right
                        text: home.hasTrack ? home.duration(home.np.durationMs) : ""
                        color: home.dim
                        font.family: home.font
                        font.pixelSize: 2.4 * home.u
                    }
                }
                Row {
                    anchors.horizontalCenter: player.side ? undefined : parent.horizontalCenter
                    spacing: 3 * home.u
                    GlassButton {
                        objectName: "glassPrevious"
                        anchors.verticalCenter: parent.verticalCenter
                        height: 9 * home.u
                        icon: Qt.resolvedUrl("../assets/previous_button.svg")
                        onTapped: if (home.np) home.np.previous()
                    }
                    GlassButton {
                        objectName: "glassPlay"
                        big: true
                        height: 12 * home.u
                        icon: Qt.resolvedUrl(home.np && home.np.playing ? "../assets/pause_button.svg" : "../assets/play_button.svg")
                        onTapped: if (home.np) home.np.togglePlay()
                    }
                    GlassButton {
                        objectName: "glassNext"
                        anchors.verticalCenter: parent.verticalCenter
                        height: 9 * home.u
                        icon: Qt.resolvedUrl("../assets/next_button.svg")
                        onTapped: if (home.np) home.np.next()
                    }
                }
            }
        }
    }

    // ── The drive, in glass ──
    Item {
        id: drive
        objectName: "glassDrive"
        x: home.driveBox.x; y: home.driveBox.y
        width: home.driveBox.width; height: home.driveBox.height
        readonly property real gap: 2.4 * home.u
        readonly property real chipH: Math.min(18 * home.u, height * 0.24)
        readonly property real topH: height - chipH - gap

        // The time
        Pane {
            id: timePane
            width: (drive.width - drive.gap) * 0.44
            height: drive.topH
            Column {
                anchors.centerIn: parent
                spacing: home.u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: home.clockText
                    color: home.ink
                    font.family: home.font
                    font.pixelSize: Math.min(timePane.width * 0.22, timePane.height * 0.3)
                    font.weight: Font.Light
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Qt.formatDate(new Date(), "dddd d MMMM")
                    color: home.dim
                    font.family: home.font
                    font.pixelSize: Math.min(timePane.width * 0.075, timePane.height * 0.09)
                }
            }
        }
        // The speed, or the way to a car
        Pane {
            id: speedPane
            x: timePane.width + drive.gap
            width: drive.width - x
            height: drive.topH
            Column {
                anchors.centerIn: parent
                spacing: 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: home.text("SPEED")
                    color: home.ink
                    font.family: home.font
                    font.pixelSize: Math.min(speedPane.width * 0.36, speedPane.height * 0.46)
                    font.weight: Font.Light
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: home.unitOf("SPEED")
                    color: home.dim
                    font.family: home.font
                    font.pixelSize: Math.min(speedPane.width * 0.07, speedPane.height * 0.09)
                    font.letterSpacing: (Math.min(speedPane.width * 0.07, speedPane.height * 0.09)) * 0.2
                }
                Item { width: 1; height: 2 * home.u; visible: !home.obdConnected }
                App.OBDConnectButton {
                    objectName: "glassConnectButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !home.obdConnected
                    mainWindow: home.mainWindow
                    fontFamily: home.font
                }
            }
        }
        // Four small readings
        Row {
            y: drive.topH + drive.gap
            spacing: drive.gap
            Repeater {
                model: ["RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "FUEL_LEVEL"]
                Pane {
                    id: chip
                    required property string modelData
                    width: (drive.width - 3 * drive.gap) / 4
                    height: drive.chipH
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 2 * home.u
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: home.text(chip.modelData)
                            color: home.ink
                            font.family: home.font
                            font.pixelSize: Math.min(chip.height * 0.36, chip.width * 0.26)
                            font.weight: Font.Light
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            // ("RPM", not "RPM · RPM")
                            readonly property string unit: home.unitOf(chip.modelData)
                            text: home.titleOf(chip.modelData)
                                  + (unit !== "" && unit.toLowerCase() !== home.titleOf(chip.modelData).toLowerCase() ? " · " + unit : "")
                            color: home.dim
                            font.family: home.font
                            font.pixelSize: Math.min(chip.height * 0.15, chip.width * 0.11)
                        }
                    }
                    // Where it stands in its range
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: chip.height * 0.1
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width * 0.6
                        height: Math.max(2, 0.4 * home.u)
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.18)
                        Rectangle {
                            width: parent.width * home.norm(chip.modelData)
                            height: parent.height
                            radius: height / 2
                            color: home.ink
                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                        }
                    }
                }
            }
        }
    }
}
