pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

// The Media Room's "what does the art corner show" picker: the album art and
// the 3D players on a ring in one scene (one View3D for all of them), the
// picked one large and swaying at the front, the rest going round behind it,
// all in view, so it is plain where the ring comes round again.
//
// Touch and mouse: drag or flick sideways and the ring follows the finger,
// then settles on the nearest; a tap on another item brings it to the front;
// a tap on the front one picks it (chosen()); a tap on none is missed(); the
// wheel and Left / Right step, Return picks. Positions are worked out here in
// pixels (the camera is orthographic at 1 unit = 1 px), so the hit test needs
// no 3D picking.
Item {
    id: root
    objectName: "musicDeviceCarousel"

    property var options: []
    property url artSource: ""
    // Corner radius of the album art card when it is at the front, in pixels
    property real artCornerRadius: 0
    // The Vinyl item turns as the record on the page does: once round per
    // song, by how far the song is (degrees clockwise; AlbumArtCarousel's
    // rule). Smoothed between position updates as it is there.
    property real playbackPosition: 0
    property real playbackDuration: 0
    property real vinylAngle: playbackDuration > 0 ? (playbackPosition / playbackDuration) * 360 : 0
    Behavior on vinylAngle { NumberAnimation { duration: 200; easing.type: Easing.Linear } }
    // Where the ring stands, in items: item i is at the front at pos === i
    // (any multiple of count away is the same place)
    property real pos: 0
    readonly property int count: options.length
    readonly property int focusIndex: count > 0 ? ((Math.round(pos) % count) + count) % count : 0
    readonly property string focusValue: count > 0 ? options[focusIndex] : ""
    signal chosen(string value)
    // A tap on none of them (the popup closes on it)
    signal missed()

    // Put `value` in the middle at once (on open)
    function show(value) {
        settle.stop()
        var i = options.indexOf(value)
        pos = i >= 0 ? i : 0
    }
    function step(n) { glideTo(Math.round(settle.running ? settle.to : pos) + n) }
    function glideTo(target) {
        settle.stop()
        settle.from = pos
        settle.to = target
        settle.start()
    }
    // How far item `index` is from the front, in items, by the short way round
    function offsetOf(index) {
        if (count <= 0) return 0
        var d = (((index - pos) % count) + count) % count
        return d > count / 2 ? d - count : d
    }

    // The ring: every item is on it all the time, so you can see it come
    // round. It is an ellipse seen from a little above: the picked item at
    // the front (bottom, large), the others smaller the further round, the
    // one opposite at the back (top). All of it from `d`, an item's distance
    // from the front in items.
    function angleAt(d) { return count > 0 ? 2 * Math.PI * d / count : 0 }
    // 1 at the front, 0 at the back
    function frontAt(d) { return (1 + Math.cos(angleAt(d))) / 2 }
    function sizeAt(d) { return 0.3 + 0.7 * Math.pow(frontAt(d), 3) }
    // Radius of the front item, in pixels: the ring is 3.3 of them tall
    // (front item, the ring, the back item) and the sides need their room
    readonly property real unit: Math.max(8, Math.min(height / 3.3, width * 0.16))
    readonly property real ringX: Math.max(unit, (width / 2 - unit * 0.62) / 0.9)
    readonly property real ringY: unit * 0.95
    // The ring's centre sits above the middle, as the front item is the tall one
    readonly property real ringLift: unit * 0.35
    function xAt(d) { return Math.sin(angleAt(d)) * ringX }
    function yAt(d) { return ringLift - Math.cos(angleAt(d)) * ringY }

    // The item under a point, or -1: the nearest to the front of those the
    // point is on, as that one is drawn in front
    function itemAt(x, y) {
        var best = -1, bestDistance = 1e9
        for (var i = 0; i < count; ++i) {
            var d = offsetOf(i)
            var radius = unit * sizeAt(d) * 1.15
            var dx = x - (width / 2 + xAt(d)), dy = y - (height / 2 - yAt(d))
            if (dx * dx + dy * dy <= radius * radius && Math.abs(d) < bestDistance) {
                best = i
                bestDistance = Math.abs(d)
            }
        }
        return best
    }

    NumberAnimation {
        id: settle
        target: root
        property: "pos"
        duration: 320
        easing.type: Easing.OutCubic
    }

    // The front item sways while the picker is on screen
    property real swayPhase
    NumberAnimation on swayPhase {
        from: 0; to: 2 * Math.PI
        duration: 7000
        loops: Animation.Infinite
        running: root.visible
    }

    function modelUrl(name) { return assetStore ? assetStore.localUrl("music_devices/" + name) : "" }

    // A soft pool of light under the front item
    Rectangle {
        x: root.width / 2 - width / 2
        y: root.height / 2 - root.yAt(0) - height / 2
        width: root.unit * 2.3
        height: width
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    // One place on the ring. `radius` is the model's own size (the framing
    // radius MusicDeviceScene uses for it), scaled to `unit` at the front.
    component Slot: Node {
        property int index: 0
        property real radius: 150
        readonly property real d: root.offsetOf(index)
        readonly property real front: Math.max(0, 1 - Math.abs(d))
        position: Qt.vector3d(root.xAt(d), root.yAt(d), (root.frontAt(d) - 1) * 1200)
        scale: {
            var s = root.unit * root.sizeAt(d) / radius
            return Qt.vector3d(s, s, s)
        }
        // Turned a little towards the middle, and the front one sways
        eulerRotation: Qt.vector3d(-16,
                                   -18 - Math.sin(root.angleAt(d)) * 12 + front * 22 * Math.sin(root.swayPhase),
                                   -4)
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: camera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            lightProbe: Texture { source: "assets/music_devices/studio.hdr" }
            probeExposure: 0.85
            aoEnabled: false
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
        }
        OrthographicCamera {
            id: camera
            z: 3000
            clipNear: 1
            clipFar: 8000
        }
        DirectionalLight { eulerRotation: Qt.vector3d(-25, -30, 0); brightness: 0.6; ambientColor: "#000000" }
        DirectionalLight { eulerRotation: Qt.vector3d(20, 65, 0); brightness: 0.2; color: "#d3e5ff" }
        DirectionalLight { eulerRotation: Qt.vector3d(-65, 160, 0); brightness: 0.3; color: "#ffe3bd" }

        // The slots follow `options` by name, so the order there can change
        // without touching this file

        Slot {   // the plain album art, as a card
            id: artSlot
            index: root.options.indexOf("Album art")
            radius: 62
            // The card (100 units across) is this many pixels wide at the front
            readonly property real frontSide: 100 * root.unit / radius
            Model {
                source: "#Rectangle"
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    cullMode: Material.NoCulling
                    alphaMode: PrincipledMaterial.Blend
                    baseColorMap: Texture {
                        sourceItem: MusicDeviceCover {
                            width: 512; height: 512
                            artSource: root.artSource
                        }
                    }
                    // Rounded corners, as the art has on the page: read from
                    // the red channel, as the Vinyl item's disc is
                    opacityChannel: Material.R
                    opacityMap: Texture {
                        sourceItem: Rectangle {
                            width: 512; height: 512
                            color: "black"
                            Rectangle {
                                anchors.fill: parent
                                color: "white"
                                radius: Math.min(width / 2, width * root.artCornerRadius / Math.max(1, artSlot.frontSide))
                            }
                        }
                    }
                }
            }
        }

        Slot {   // the album art as a record (the Vinyl setting)
            index: root.options.indexOf("Vinyl")
            visible: index >= 0
            radius: 56
            Model {
                source: "#Rectangle"
                eulerRotation.z: -root.vinylAngle   // clockwise, as on the page
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    cullMode: Material.NoCulling
                    alphaMode: PrincipledMaterial.Blend
                    baseColorMap: Texture {
                        sourceItem: MusicDeviceCover {
                            width: 512; height: 512
                            artSource: root.artSource
                        }
                    }
                    // The disc and its spindle hole, as AlbumArtCarousel cuts
                    // them (a hole 12% across); read from the red channel
                    opacityChannel: Material.R
                    opacityMap: Texture {
                        sourceItem: Item {
                            width: 512; height: 512
                            Rectangle {
                                anchors.fill: parent
                                radius: width / 2
                                color: "white"
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width * 0.12; height: width
                                    radius: width / 2
                                    color: "black"
                                }
                            }
                        }
                    }
                }
            }
        }

        Slot {
            index: root.options.indexOf("Record player")
            radius: 214
            RuntimeLoader { source: root.modelUrl("record-player.glb") }
            Node {
                position: Qt.vector3d(-35, 8, 28)
                RuntimeLoader { source: root.modelUrl("record.glb") }
                MusicDeviceArtSurface {
                    artSource: root.artSource
                    maskSource: "assets/music_devices/mask-record.png"
                    z: 1.65
                    scale: Qt.vector3d(.64, .64, 1)
                }
            }
            RuntimeLoader {
                source: root.modelUrl("tonearm.glb")
                position: Qt.vector3d(103, 80, 41)
                eulerRotation.z: -12
            }
        }

        Slot {
            index: root.options.indexOf("MP3 player")
            radius: 154
            RuntimeLoader { source: root.modelUrl("mp3-player.glb") }
            Model {
                source: "#Rectangle"
                position: Qt.vector3d(0, 48, 15.65)
                scale: Qt.vector3d(.97, .52, 1)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColorMap: Texture {
                        sourceItem: Rectangle {
                            width: 444; height: 208
                            color: "#081923"
                            MusicDeviceCover {
                                x: 16; anchors.verticalCenter: parent.verticalCenter
                                width: 150; height: width
                                artSource: root.artSource
                            }
                        }
                    }
                }
            }
        }

        Slot {
            index: root.options.indexOf("CD player")
            radius: 190
            RuntimeLoader { source: root.modelUrl("cd-player.glb") }
            Node {
                position: Qt.vector3d(0, 9, 25)
                RuntimeLoader { source: root.modelUrl("cd.glb") }
                MusicDeviceArtSurface {
                    artSource: root.artSource
                    maskSource: "assets/music_devices/mask-cd.png"
                    z: 1.05
                    scale: Qt.vector3d(1.78, 1.78, 1)
                }
            }
            RuntimeLoader {
                source: root.modelUrl("cd-lid.glb")
                position: Qt.vector3d(0, 103, 31)
            }
        }

        Slot {
            index: root.options.indexOf("Cassette player")
            radius: 150
            RuntimeLoader { source: root.modelUrl("cassette-player.glb") }
            RuntimeLoader {
                source: root.modelUrl("cassette-door.glb")
                position: Qt.vector3d(-10, -109, 33)
            }
            RuntimeLoader { source: root.modelUrl("cassette.glb") }
            MusicDeviceArtSurface {
                artSource: root.artSource
                position: Qt.vector3d(-19, -2, 28.65)
                scale: Qt.vector3d(.35, .35, 1)
            }
            RuntimeLoader {
                source: root.modelUrl("tape-pack.glb")
                position: Qt.vector3d(-19, -39, 28.1)
            }
            RuntimeLoader {
                source: root.modelUrl("tape-pack.glb")
                position: Qt.vector3d(-19, 35, 28.1)
                scale: Qt.vector3d(9 / 16, 9 / 16, 1)
            }
            RuntimeLoader { source: root.modelUrl("reel.glb"); position: Qt.vector3d(-19, -39, 29.5) }
            RuntimeLoader { source: root.modelUrl("reel.glb"); position: Qt.vector3d(-19, 35, 29.5) }
        }

        Slot {
            index: root.options.indexOf("iPod")
            radius: 154
            RuntimeLoader { source: root.modelUrl("ipod.glb") }
            Model {
                source: "#Rectangle"
                position: Qt.vector3d(0, 49, 15.65)
                scale: Qt.vector3d(1.11, .83, 1)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColorMap: Texture {
                        sourceItem: Rectangle {
                            width: 444; height: 332
                            color: "#e9edf1"
                            MusicDeviceCover {
                                anchors.centerIn: parent
                                width: 240; height: width
                                artSource: root.artSource
                            }
                        }
                    }
                }
            }
        }
    }

    focus: true
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onReturnPressed: chosen(focusValue)
    Keys.onEnterPressed: chosen(focusValue)

    MouseArea {
        id: input
        anchors.fill: parent
        preventStealing: true
        cursorShape: pressed && dragged ? Qt.ClosedHandCursor : Qt.PointingHandCursor

        // The front of the ring keeps up with the finger
        readonly property real pxPerItem: Math.max(1, root.ringX * 2 * Math.PI / Math.max(1, root.count) * 0.8)
        readonly property real dragStart: Math.max(12, Qt.styleHints.startDragDistance)
        property real downX: 0
        property real lastX: 0
        property double lastTime: 0
        property real startPos: 0
        property real velocity: 0       // items per second, smoothed
        property bool dragged: false
        property real wheelRest: 0

        onPressed: function(mouse) {
            settle.stop()
            downX = mouse.x; lastX = mouse.x
            lastTime = Date.now()
            startPos = root.pos
            velocity = 0
            dragged = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            if (!dragged && Math.abs(mouse.x - downX) > dragStart) dragged = true
            if (dragged) root.pos = startPos - (mouse.x - downX) / pxPerItem
            var now = Date.now()
            if (now > lastTime) {
                var v = -(mouse.x - lastX) / pxPerItem / ((now - lastTime) / 1000)
                velocity = velocity * 0.5 + v * 0.5
            }
            lastX = mouse.x
            lastTime = now
        }
        onReleased: function(mouse) {
            if (dragged) {
                // A flick carries on a little; a finger that stopped first doesn't
                var carry = Date.now() - lastTime < 90 ? Math.max(-2, Math.min(2, velocity * 0.18)) : 0
                root.glideTo(Math.round(root.pos + carry))
                return
            }
            var hit = root.itemAt(mouse.x, mouse.y)
            if (hit < 0) { root.glideTo(Math.round(root.pos)); root.missed(); return }
            var d = root.offsetOf(hit)
            if (Math.abs(d) < 0.5) {
                root.glideTo(Math.round(root.pos))
                root.chosen(root.options[hit])
            } else {
                root.glideTo(Math.round(root.pos + d))
            }
        }
        onCanceled: root.glideTo(Math.round(root.pos))
        onWheel: function(wheel) {
            // Touchpads send many small deltas: one step per wheel notch's worth
            var delta = Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y) ? wheel.angleDelta.x : wheel.angleDelta.y
            wheelRest += delta
            while (wheelRest >= 120) { wheelRest -= 120; root.step(-1) }
            while (wheelRest <= -120) { wheelRest += 120; root.step(1) }
            wheel.accepted = true
        }
    }
}
