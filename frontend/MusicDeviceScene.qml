import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

Item {
    id: root
    objectName: "musicDeviceScene"
    property string device: "Record player"
    property bool playing: false
    property url artSource: ""
    property url displayedArtSource: ""
    function refreshArtwork() {
        if (!hasTrack) { displayedArtSource = ""; return }
        if (!loading || digital || loadProgress >= .435) displayedArtSource = artSource
    }
    // Defer one event-loop turn so a track-change signal can start the exchange
    // before replacing the outgoing label. Late artwork still updates immediately.
    onArtSourceChanged: Qt.callLater(refreshArtwork)
    onLoadProgressChanged: refreshArtwork()
    onLoadingChanged: if (!loading) refreshArtwork()
    property string trackTitle: "OCTAVE"
    property string artist: "Your music, in motion"
    // Managers and MediaRoom expose milliseconds. Never run an independent UI clock.
    property real playbackPosition: 0
    property real playbackDuration: 0
    property bool hasTrack: true
    onHasTrackChanged: {
        if (!hasTrack) cancelLoading()
        refreshArtwork()
    }
    property real progress: 0
    readonly property real safeDuration: isFinite(playbackDuration) ? Math.max(0, playbackDuration) : 0
    readonly property real safePosition: !hasTrack || !isFinite(playbackPosition) ? 0
        : Math.max(0, safeDuration > 0 ? Math.min(playbackPosition, safeDuration) : playbackPosition)
    readonly property real trackProgress: !hasTrack ? 0 : safeDuration > 0 ? safePosition / safeDuration
        : isFinite(progress) ? Math.max(0, Math.min(1, progress)) : 0
    function formatPlaybackTime(milliseconds) {
        var seconds = Math.floor(Math.max(0, isFinite(milliseconds) ? milliseconds : 0) / 1000)
        var minutes = Math.floor(seconds / 60)
        var tail = (seconds % 60).toString().padStart(2, "0")
        return minutes >= 60 ? Math.floor(minutes / 60) + ":" + (minutes % 60).toString().padStart(2, "0") + ":" + tail
                             : minutes + ":" + tail
    }
    readonly property string elapsedText: formatPlaybackTime(safePosition)
    readonly property string remainingText: hasTrack && safeDuration > 0 ? "−" + formatPlaybackTime(safeDuration - safePosition) : "--:--"
    readonly property string playbackLabel: !hasTrack ? "READY" : loading ? "LOADING" : playing ? "PLAY" : "PAUSE"
    readonly property real supplyRadius: Math.sqrt(81 + 175 * (1 - trackProgress)) / 16
    readonly property real takeupRadius: Math.sqrt(81 + 175 * trackProgress) / 16
    readonly property real tonearmAngle: doorOpen * 24 + (1 - doorOpen) * (-12 - 18 * trackProgress)
    property real supplySpin: 0
    property real takeupSpin: 0
    property bool animateOnArrival: true
    property bool pendingArrival: false
    property bool initialized: false
    property real loadProgress: 1
    readonly property bool loading: loadingAnimation.running
    signal loadingFinished()

    // Smooth, deterministic choreography. Rapid skips reuse the current exchange;
    // metadata bindings already contain the latest track, so no stale queue builds up.
    function ramp(start, end) {
        var t = Math.max(0, Math.min(1, (loadProgress - start) / (end - start)))
        return t * t * (3 - 2 * t)
    }
    readonly property real doorOpen: ramp(0, .16) * (1 - ramp(.80, 1))
    readonly property real mediaLift: ramp(.16, .36) * (1 - ramp(.46, .78))
    readonly property real mediaAlpha: (1 - ramp(.33, .41)) + ramp(.46, .54)
    readonly property real mediaSide: loadProgress < .435 ? -1 : 1
    function cancelLoading() {
        pendingArrival = false
        loadingAnimation.stop()
        loadProgress = 1
    }
    function loadMedia(arrival) {
        if (!visible || !hasTrack) return
        if (!ready) { pendingArrival = true; return }
        if (loading) return
        pendingArrival = false
        loadProgress = arrival ? .44 : 0
        loadingAnimation.from = loadProgress
        loadingAnimation.duration = digital ? 480 : compactDisc ? (arrival ? 1350 : 2450) : (arrival ? 1150 : 2050)
        loadingAnimation.start()
    }
    NumberAnimation {
        id: loadingAnimation
        target: root
        property: "loadProgress"
        to: 1
        easing.type: Easing.Linear
        onFinished: root.loadingFinished()
    }
    Component.onCompleted: {
        initialized = true
        refreshArtwork()
        if (animateOnArrival) loadMedia(true)
    }
    onReadyChanged: {
        if (ready && pendingArrival) Qt.callLater(function() { if (root.pendingArrival) root.loadMedia(true) })
    }
    onVisibleChanged: {
        if (!visible) cancelLoading()
        else if (initialized && animateOnArrival) loadMedia(true)
    }
    readonly property bool ready: body.status === RuntimeLoader.Success
        && (!disc.source.toString() || disc.status === RuntimeLoader.Success)
        && (!leftReel.source.toString() || leftReel.status === RuntimeLoader.Success)
        && (!rightReel.source.toString() || rightReel.status === RuntimeLoader.Success)
        && (!tonearm.source.toString() || tonearm.status === RuntimeLoader.Success)
        && (!lid.source.toString() || lid.status === RuntimeLoader.Success)
        && (!tape.source.toString() || tape.status === RuntimeLoader.Success)
        && (!door.source.toString() || door.status === RuntimeLoader.Success)
        && (!supplyTape.source.toString() || supplyTape.status === RuntimeLoader.Success)
        && (!takeupTape.source.toString() || takeupTape.status === RuntimeLoader.Success)
    readonly property string error: body.status === RuntimeLoader.Error ? body.errorString
        : disc.status === RuntimeLoader.Error ? disc.errorString
        : leftReel.status === RuntimeLoader.Error ? leftReel.errorString
        : rightReel.status === RuntimeLoader.Error ? rightReel.errorString
        : tonearm.status === RuntimeLoader.Error ? tonearm.errorString
        : lid.status === RuntimeLoader.Error ? lid.errorString
        : tape.status === RuntimeLoader.Error ? tape.errorString
        : door.status === RuntimeLoader.Error ? door.errorString
        : supplyTape.status === RuntimeLoader.Error ? supplyTape.errorString
        : takeupTape.status === RuntimeLoader.Error ? takeupTape.errorString : ""
    readonly property bool record: device === "Record player"
    readonly property bool cassette: device === "Cassette player"
    readonly property bool compactDisc: device === "CD player"
    readonly property bool ipod: device === "iPod"
    readonly property bool digital: ipod || device === "MP3 player"
    property real spin: 0
    property real yaw: -18
    property real pitch: -18
    onDeviceChanged: {
        cancelLoading()
        spin = 0; supplySpin = 0; takeupSpin = 0; yaw = -18; pitch = -18
        if (initialized && animateOnArrival) Qt.callLater(function() { root.loadMedia(true) })
    }

    Timer {
        objectName: "deviceMotionTimer"
        interval: 33
        repeat: true
        running: root.visible && root.hasTrack && root.playing && root.ready && !root.digital && !root.loading
        property double previousTick: 0
        property double motorStarted: 0
        onRunningChanged: { previousTick = Date.now(); motorStarted = previousTick }
        onTriggered: {
            var now = Date.now()
            // The CD motor gently spins up after the latch closes. Pausing still
            // stops immediately, matching the playback state and other players.
            var acceleration = root.compactDisc ? Math.min(1, (now - motorStarted) / 650) : 1
            root.spin = (root.spin + Math.min(now - previousTick, 100) * (root.record ? 0.2 : root.cassette ? 0.09 : 0.12) * acceleration) % 360
            if (root.cassette) {
                var tapeStep = Math.min(now - previousTick, 100) * .09
                root.supplySpin = (root.supplySpin + tapeStep / root.supplyRadius) % 360
                root.takeupSpin = (root.takeupSpin + tapeStep / root.takeupRadius) % 360
            }
            previousTick = now
        }
    }

    // Rotation-safe room for the entire assembly, including lifted media and
    // hinged doors. Smooth choreography already eases these inputs in and out.
    readonly property real framingExpansion: Math.max(doorOpen, mediaLift)
    readonly property real framingRadius: record ? 214 + 80 * framingExpansion
        : compactDisc ? 190 + 105 * framingExpansion
        : cassette ? 150 + 120 * framingExpansion : 154

    View3D {
        anchors.fill: parent
        camera: camera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            lightProbe: Texture { source: "assets/music_devices/studio.hdr" }
            probeExposure: 0.85
            // No screen-space AO and 4x rather than 8x MSAA: the head unit and
            // phones render this beside the media list every frame while a
            // disc spins, and the three-light studio probe already carries the
            // shading. Matches the vehicle view in CarMenu.qml.
            aoEnabled: false
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
        }
        OrthographicCamera {
            id: camera
            z: 700
            // Fit every aspect ratio, including the narrow live settings preview.
            horizontalMagnification: Math.max(0.01, Math.min(root.width, root.height) / (2 * root.framingRadius + 20))
            verticalMagnification: horizontalMagnification
            clipNear: 1
            clipFar: 1500
        }
        DirectionalLight { eulerRotation: Qt.vector3d(-25, -30, 0); brightness: 0.6; ambientColor: "#000000" }
        DirectionalLight { eulerRotation: Qt.vector3d(20, 65, 0); brightness: 0.2; color: "#d3e5ff" }
        DirectionalLight { eulerRotation: Qt.vector3d(-65, 160, 0); brightness: 0.3; color: "#ffe3bd" }
        Node {
            id: rig
            eulerRotation: Qt.vector3d(root.pitch, root.yaw, -4)
            y: 0
            RuntimeLoader {
                id: body
                objectName: "deviceBody"
                source: "assets/music_devices/" + (root.record ? "record-player" : root.cassette ? "cassette-player"
                    : root.compactDisc ? "cd-player" : root.ipod ? "ipod" : "mp3-player") + ".glb"
            }
            Node {
                x: (root.record ? -35 : 0) + root.mediaSide * root.mediaLift * 50
                y: (root.record ? 8 : 9) + root.mediaLift * 45
                z: (root.record ? 28 : 25) + root.mediaLift * 100
                opacity: root.mediaAlpha
                eulerRotation.x: root.mediaLift * -12
                eulerRotation.z: -root.spin
                RuntimeLoader {
                    id: disc
                    source: root.record ? "assets/music_devices/record.glb" : root.compactDisc ? "assets/music_devices/cd.glb" : ""
                }
                MusicDeviceArtSurface {
                    objectName: "discArtwork"
                    visible: root.record || root.compactDisc
                    artSource: root.displayedArtSource
                    maskSource: root.record ? "assets/music_devices/mask-record.png" : "assets/music_devices/mask-cd.png"
                    z: root.record ? 1.65 : 1.05
                    scale: Qt.vector3d(root.record ? .64 : 1.78, root.record ? .64 : 1.78, 1)
                }
            }
            Model {
                visible: root.record
                source: "#Sphere"
                position: Qt.vector3d(-132, -79, 18.5)
                scale: Qt.vector3d(.036, .036, .018)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: root.hasTrack && root.playing && !root.loading ? "#53d8ed" : "#273b42"
                }
            }
            // Live LCD replaces the baked-in timer and fictitious track number.
            Model {
                visible: root.compactDisc
                source: "#Rectangle"
                position: Qt.vector3d(-60, -100, 25.7)
                scale: Qt.vector3d(.55, .17, 1)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColorMap: Texture {
                        sourceItem: Rectangle {
                            width: 440; height: 136
                            color: "#96a994"
                            Text {
                                objectName: "cdPlaybackStatus"
                                x: 16; y: 5; width: 408; height: 34
                                text: root.playbackLabel
                                color: "#304537"; font.pixelSize: 25; font.letterSpacing: 3
                            }
                            Text {
                                objectName: "cdElapsedTime"
                                x: 14; y: 40; width: 410; height: 90
                                text: root.elapsedText
                                color: "#203326"; font.family: "monospace"
                                font.pixelSize: 77; fontSizeMode: Text.Fit; minimumPixelSize: 40
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }
            }
            RuntimeLoader {
                id: tonearm
                source: root.record ? "assets/music_devices/tonearm.glb" : ""
                position: Qt.vector3d(103, 80, 41 + root.doorOpen * 8)
                eulerRotation.z: root.tonearmAngle
            }
            RuntimeLoader {
                id: lid
                source: root.compactDisc ? "assets/music_devices/cd-lid.glb" : ""
                position: Qt.vector3d(0, 103, 31)
                eulerRotation.x: -105 * root.doorOpen
            }
            RuntimeLoader {
                id: door
                source: root.cassette ? "assets/music_devices/cassette-door.glb" : ""
                position: Qt.vector3d(-10, -109, 33)
                eulerRotation.x: 65 * root.doorOpen
            }
            Node {
                y: root.mediaLift * 55
                z: root.mediaLift * 110
                x: root.mediaSide * root.mediaLift * 28
                eulerRotation.y: root.mediaSide * root.mediaLift * 12
                opacity: root.mediaAlpha
                RuntimeLoader {
                    id: tape
                    source: root.cassette ? "assets/music_devices/cassette.glb" : ""
                }
                MusicDeviceArtSurface {
                    objectName: "cassetteArtwork"
                    visible: root.cassette
                    artSource: root.displayedArtSource
                    position: Qt.vector3d(-19, -2, 28.65)
                    scale: Qt.vector3d(.35, .35, 1)
                }
                RuntimeLoader {
                    id: supplyTape
                    source: root.cassette ? "assets/music_devices/tape-pack.glb" : ""
                    position: Qt.vector3d(-19, -39, 28.1)
                    scale: Qt.vector3d(root.supplyRadius, root.supplyRadius, 1)
                }
                RuntimeLoader {
                    id: takeupTape
                    source: root.cassette ? "assets/music_devices/tape-pack.glb" : ""
                    position: Qt.vector3d(-19, 35, 28.1)
                    scale: Qt.vector3d(root.takeupRadius, root.takeupRadius, 1)
                }
                RuntimeLoader {
                    id: leftReel
                    source: root.cassette ? "assets/music_devices/reel.glb" : ""
                    position: Qt.vector3d(-19, -39, 29.5)
                    eulerRotation.z: -root.supplySpin
                }
                RuntimeLoader {
                    id: rightReel
                    source: root.cassette ? "assets/music_devices/reel.glb" : ""
                    position: Qt.vector3d(-19, 35, 29.5)
                    eulerRotation.z: -root.takeupSpin
                }
            }
            Model {
                visible: root.digital
                source: "#Rectangle"
                position: Qt.vector3d(0, root.ipod ? 49 : 48, 15.65)
                scale: Qt.vector3d(root.ipod ? 1.11 : .97, root.ipod ? .83 : .52, 1)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColorMap: Texture {
                        sourceItem: Rectangle {
                            width: 444; height: root.ipod ? 332 : 208
                            color: root.ipod ? "#e9edf1" : "#081923"
                            Rectangle {
                                width: parent.width; height: 38
                                color: root.ipod ? "#cbd5df" : "#144d66"
                                Text { x: 16; anchors.verticalCenter: parent.verticalCenter; text: !root.hasTrack ? "Ready" : root.loading ? "Loading…" : root.playing ? "Now playing  ▷" : "Paused  Ⅱ"; color: root.ipod ? "#243244" : "#c8f3ff"; font.pixelSize: 21 }
                            }
                            MusicDeviceCover {
                                id: cover
                                objectName: "digitalArtwork"
                                x: 16; y: root.ipod ? 60 : 54
                                width: root.ipod ? 156 : 100; height: width
                                artSource: root.displayedArtSource
                            }
                            Text {
                                x: root.ipod ? 185 : 132; y: root.ipod ? 67 : 60
                                width: root.ipod ? 240 : 292
                                text: root.hasTrack ? (root.trackTitle || "Unknown track") : "No track selected"
                                color: root.ipod ? "#172638" : "#b9efff"
                                font.pixelSize: 26; font.bold: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                            }
                            Text {
                                x: root.ipod ? 185 : 132; y: root.ipod ? 145 : 125
                                width: root.ipod ? 240 : 292; text: root.hasTrack ? root.artist : ""
                                color: root.ipod ? "#536172" : "#70acc3"; font.pixelSize: 21; elide: Text.ElideRight
                            }
                            Rectangle {
                                x: 20; y: parent.height - 48; width: parent.width - 40; height: 10; radius: 5
                                color: root.ipod ? "#bcc6d0" : "#254453"
                                Rectangle { width: parent.width * root.trackProgress; height: parent.height; radius: 5; color: "#36a9d4" }
                            }
                            Text {
                                objectName: "digitalElapsedTime"
                                x: 20; y: parent.height - 29
                                text: root.elapsedText
                                color: root.ipod ? "#536172" : "#70acc3"; font.pixelSize: 21
                            }
                            Text {
                                objectName: "digitalRemainingTime"
                                anchors.right: parent.right; anchors.rightMargin: 20
                                y: parent.height - 29
                                text: root.remainingText
                                color: root.ipod ? "#536172" : "#70acc3"; font.pixelSize: 21
                            }
                            Rectangle {
                                anchors.fill: parent
                                color: root.ipod ? "#e9edf1" : "#081923"
                                opacity: root.loading ? Math.sin(root.loadProgress * Math.PI) * .9 : 0
                            }
                        }
                    }
                }
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real lastX: 0
        property real lastY: 0
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            // Wrap equivalent angles to keep precision over repeated full turns.
            root.yaw = (root.yaw + (mouse.x - lastX) * .25) % 360
            root.pitch = (root.pitch + (mouse.y - lastY) * .2) % 360
            lastX = mouse.x; lastY = mouse.y
        }
        onDoubleClicked: { root.yaw = -18; root.pitch = -18 }
    }
}
