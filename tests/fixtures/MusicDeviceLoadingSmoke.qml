import QtQuick
import QtQuick.Window
import "../../frontend" as App

Window {
    id: window
    visible: true
    width: 640; height: 480
    property int phase: 0
    property int modelIndex: 0
    property real heldSpin: 0
    property var models: ["Record player", "CD player", "Cassette player", "iPod", "MP3 player"]
    App.MusicDeviceScene {
        id: scene
        anchors.fill: parent
        animateOnArrival: false
        device: window.models[window.modelIndex]
        playing: true
    }
    function check(condition, message) {
        if (!condition) { console.error(message); Qt.exit(1) }
    }
    Timer {
        interval: 120; running: true; repeat: true
        onTriggered: {
            window.check(scene.error === "", scene.error)
            if (!scene.ready) return
            if (window.phase === 0) {
                scene.loadMedia(false)
                window.check(scene.loading, "Exchange must start")
                window.heldSpin = scene.spin
                window.phase = 1
            } else if (window.phase === 1) {
                window.check(scene.spin === window.heldSpin, "Transport must not rotate during loading")
                var before = scene.loadProgress
                scene.trackTitle = "Latest requested track"
                scene.loadMedia(false)
                window.check(scene.loadProgress === before, "Rapid skips must coalesce, not restart")
                scene.playing = false
                window.phase = 2
            } else if (window.phase === 2) {
                if (scene.loading) return
                window.check(scene.loadProgress === 1 && scene.mediaLift === 0 && scene.doorOpen === 0 && scene.mediaAlpha === 1,
                             "Completed exchange must leave a closed, seated player")
                window.check(scene.spin === window.heldSpin, "Paused player must remain still after loading")
                scene.loadMedia(true)
                window.check(scene.loadProgress === .44, "Arrival should begin with incoming media")
                scene.visible = false
                window.check(!scene.loading && scene.loadProgress === 1, "Hiding must cancel and restore the pose")
                scene.visible = true
                scene.loadMedia(false)
                window.phase = 3
            } else {
                // Changing type during the exchange must discard every old transform.
                if (window.modelIndex < window.models.length - 1) {
                    window.modelIndex++
                    window.check(!scene.loading && scene.loadProgress === 1, "Device change must cancel previous loading")
                    scene.playing = true
                    window.phase = 0
                } else {
                    scene.cancelLoading()
                    scene.animateOnArrival = true
                    scene.visible = false
                    scene.visible = true
                    window.check(scene.loading, "Returning to the view should load incoming media")
                    scene.cancelLoading()
                    console.log("MUSIC_DEVICES_SMOKE_PASS")
                    Qt.quit()
                }
            }
        }
    }
    Timer { interval: 25000; running: true; onTriggered: { console.error("Loading lifecycle timed out"); Qt.exit(2) } }
}
