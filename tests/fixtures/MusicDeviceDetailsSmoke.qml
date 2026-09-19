import QtQuick
import QtQuick.Window
import "../../frontend" as App

Window {
    id: window
    visible: true
    width: 640; height: 480
    property int phase: 0
    property int modelIndex: 0
    property real stoppedSpin: 0
    property var models: ["CD player", "MP3 player", "iPod", "Record player", "Cassette player"]
    App.MusicDeviceScene {
        id: scene
        anchors.fill: parent
        animateOnArrival: false
        device: window.models[window.modelIndex]
    }
    function check(ok, why) { if (!ok) { console.error(why); Qt.exit(1) } }
    function screens(time, remaining) {
        window.check(scene.elapsedText === time, "CD LCD not wired to elapsed time")
        window.check(scene.remainingText === remaining, "Remaining time stale")
    }
    Timer {
        interval: 120; repeat: true; running: true
        onTriggered: {
            window.check(scene.error === "", scene.error)
            if (!scene.ready) return
            switch (window.phase) {
            case 0:
                scene.hasTrack = true; scene.playing = true
                scene.playbackDuration = 245000; scene.playbackPosition = 65000
                window.screens("1:05", "−3:00")
                window.check(scene.playbackLabel === "PLAY", "LCD play state stale")
                scene.playing = false
                window.stoppedSpin = scene.spin
                break
            case 1:
                window.screens("1:05", "−3:00")
                window.check(scene.spin === window.stoppedSpin, "Paused transport moved")
                window.check(scene.playbackLabel === "PAUSE", "LCD pause state stale")
                scene.playbackPosition = 125000
                window.screens("2:05", "−2:00")
                window.check(Math.abs(scene.trackProgress - 125 / 245) < .001, "Seek did not update progress")
                scene.loadMedia(false)
                window.check(scene.playbackLabel === "LOADING", "Loading state stale")
                scene.cancelLoading()
                break
            case 2:
                scene.playbackDuration = 7200000; scene.playbackPosition = 3661000
                window.screens("1:01:01", "−58:59")
                scene.playbackPosition = -100
                window.screens("0:00", "−2:00:00")
                scene.playbackPosition = 8000000
                window.screens("2:00:00", "−0:00")
                scene.playbackDuration = NaN; scene.playbackPosition = NaN
                window.screens("0:00", "--:--")
                break
            case 3:
                scene.playbackDuration = 200000; scene.playbackPosition = 0
                window.check(scene.supplyRadius > scene.takeupRadius, "Tape should begin on the supply reel")
                var startAngle = scene.tonearmAngle
                scene.playbackPosition = 200000
                window.check(scene.supplyRadius < scene.takeupRadius, "Tape must transfer to the takeup reel")
                window.check(scene.tonearmAngle < startAngle, "Stylus must move inward over the track")
                scene.hasTrack = false; scene.playing = true
                scene.loadMedia(false)
                window.check(!scene.loading && scene.trackProgress === 0, "Empty player should not simulate a track")
                window.screens("0:00", "--:--")
                window.check(scene.playbackLabel === "READY", "Empty LCD should say READY")
                window.stoppedSpin = scene.spin
                break
            case 4:
                window.check(scene.spin === window.stoppedSpin, "Empty transport should stop")
                if (window.modelIndex < window.models.length - 1) {
                    window.modelIndex++; window.phase = -1
                } else {
                    console.log("MUSIC_DEVICES_SMOKE_PASS"); Qt.quit()
                }
                break
            }
            window.phase++
        }
    }
    Timer { interval: 15000; running: true; onTriggered: { console.error("Playback details timed out"); Qt.exit(2) } }
}
