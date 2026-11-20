// VehicleChooser.qml
//
// "Which vehicle is this?": a tile per vehicle the app knows (the ones that
// ship with it, then the user's own models) and one to add a model from a
// file. A fresh install has no vehicle, so the 3D view opens on this; the
// view's menu brings it back to change the choice. A tap chooses
// (vehicleManager.select) and emits chosen().
//
// Settings > Device > Vehicle shows the same tiles inside its card
// (`embedded`: as tall as its content, at the top, scrolled by the card) and
// adds a "No vehicle" tile (`offerNone`), which the 3D view has no use for:
// with none chosen it shows this chooser.
import QtQuick
import QtQuick.Dialogs
import "." as App

Item {
    id: chooser
    objectName: "vehicleChooser"

    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    readonly property var vm: (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager : null
    signal chosen()
    // Inside another scrolling page: no scrolling or centring of its own
    property bool embedded: false
    // A "No vehicle" tile after the vehicles (vehicleManager.select(""))
    property bool offerNone: false
    implicitHeight: content.implicitHeight
    // Here, not in the tile: choosing changes vehicleManager.vehicles, which
    // rebuilds the tiles, and the tapped one is gone before select() returns
    function choose(id) {
        if (vm.select(id))
            chosen()
    }

    FileDialog {
        id: modelDialog
        title: "Add a 3D model of your vehicle"
        nameFilters: ["glTF binary models (*.glb)", "All files (*)"]
        // importModel makes the new vehicle the active one
        onAccepted: if (chooser.vm.importModel(selectedFile.toString()) !== "") chooser.chosen()
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: chooser.embedded ? height : Math.max(height, content.implicitHeight + chooser.dp(32))
        interactive: !chooser.embedded
        clip: !chooser.embedded

        Column {
            id: content
            width: chooser.embedded ? parent.width : Math.min(parent.width - chooser.dp(32), chooser.dp(900))
            anchors.horizontalCenter: parent.horizontalCenter
            y: chooser.embedded ? 0 : Math.max(chooser.dp(16), (parent.height - implicitHeight) / 2)
            spacing: chooser.dp(20)

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "Choose your vehicle"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.4
                font.bold: true
                font.family: App.Style.fontFamily
            }

            Flow {
                id: tiles
                // Centred: as wide as its rows of tiles, no wider
                readonly property int tileWidth: chooser.dp(280)
                readonly property int count: tileRepeater.count + (noneTile.visible ? 1 : 0) + (addTile.visible ? 1 : 0)
                readonly property int perRow: Math.max(1, Math.min(count, Math.floor((parent.width + spacing) / (tileWidth + spacing))))
                width: perRow * tileWidth + (perRow - 1) * spacing
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: chooser.dp(14)

                Repeater {
                    id: tileRepeater
                    model: chooser.vm ? chooser.vm.vehicles : []
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        objectName: "vehicleTile_" + modelData.id
                        width: tiles.tileWidth
                        height: chooser.dp(250)
                        radius: Math.max(2, chooser.dp(10))
                        color: tileMouse.pressed ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22)
                                                 : App.Style.contentColor
                        border.width: modelData.active ? 2 : 1
                        border.color: modelData.active ? App.Style.accent
                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)

                        App.VehiclePicture {
                            id: tilePicture
                            spec: tile.modelData
                            anchors { top: parent.top; left: parent.left; right: parent.right; bottom: tileName.top; margins: chooser.dp(12) }
                        }
                        Text {
                            id: tileName
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: chooser.dp(10) }
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: tile.modelData.name
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText
                            font.family: App.Style.fontFamily
                        }
                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chooser.choose(tile.modelData.id)
                        }
                    }
                }

                // No vehicle: a tile like a vehicle's, outlined while none is chosen
                Rectangle {
                    id: noneTile
                    objectName: "vehicleTileNone"
                    readonly property bool current: !!chooser.vm && !chooser.vm.active.id
                    visible: chooser.offerNone && !!chooser.vm
                    width: tiles.tileWidth
                    height: chooser.dp(250)
                    radius: Math.max(2, chooser.dp(10))
                    color: noneMouse.pressed ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22)
                                             : App.Style.contentColor
                    border.width: current ? 2 : 1
                    border.color: current ? App.Style.accent
                        : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)

                    // Where a vehicle's picture is: an empty ring, struck through
                    Item {
                        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: noneName.top; margins: chooser.dp(12) }
                        Rectangle {
                            id: noneRing
                            anchors.centerIn: parent
                            width: Math.min(parent.width, parent.height) * 0.5
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.width: Math.max(2, chooser.dp(4))
                            border.color: App.Style.secondaryTextColor
                            opacity: 0.6
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width - parent.border.width
                                height: parent.border.width
                                color: parent.border.color
                                rotation: -45
                                antialiasing: true
                            }
                        }
                    }
                    Text {
                        id: noneName
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: chooser.dp(10) }
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: "No vehicle"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                    }
                    MouseArea {
                        id: noneMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chooser.choose("")
                    }
                }

                Rectangle {
                    id: addTile
                    objectName: "vehicleTileAdd"
                    visible: !!chooser.vm && chooser.vm.canImport
                    width: tiles.tileWidth
                    height: chooser.dp(250)
                    radius: Math.max(2, chooser.dp(10))
                    color: addMouse.pressed ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.22) : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - chooser.dp(24)
                        spacing: chooser.dp(8)
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "+"
                            color: App.Style.accent
                            font.pixelSize: chooser.dp(44)
                            font.family: App.Style.fontFamily
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Add your own\n3D model (.glb)"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                        }
                    }
                    MouseArea {
                        id: addMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modelDialog.open()
                    }
                }
            }

            Text {
                objectName: "vehicleChooserError"
                width: parent.width
                visible: text !== ""
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                text: chooser.vm ? chooser.vm.lastError : ""
                color: App.Style.statusDanger
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: App.Style.fontFamily
            }
        }
    }
}
