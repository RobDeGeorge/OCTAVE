import QtQuick
import QtQuick.Window
import "../../frontend" as App

Window {
    id: window
    visible: true
    width: 640; height: 480
    property int phase: 0
    App.MusicDeviceScene {
        id: scene
        anchors.fill: parent
        animateOnArrival: false
        artSource: cover_a
    }
    App.MusicDeviceCover {
        id: probe
        width: 64; height: 64
        artSource: scene.displayedArtSource
    }
    function check(condition, message) {
        if (!condition) { console.error(message); Qt.exit(1) }
    }
    Timer {
        interval: 80; running: true; repeat: true
        onTriggered: {
            window.check(scene.error === "", scene.error)
            if (!scene.ready || probe.status !== Image.Ready) return
            switch (window.phase) {
            case 0:
                window.check(probe.source.toString() === cover_a.toString(), "Initial artwork must load")
                scene.loadMedia(false)
                scene.artSource = cover_b
                window.phase++
                break
            case 1:
                window.check(scene.displayedArtSource.toString() === cover_a.toString(), "Outgoing record must keep its artwork")
                window.phase++
                break
            case 2:
                if (scene.loadProgress < .6) return
                window.check(probe.source.toString() === cover_b.toString(), "Incoming media must show the new album")
                window.phase++
                break
            case 3:
                if (scene.loading) return
                scene.artSource = "file:///octave-artwork-test-does-not-exist.png"
                window.phase++
                break
            case 4:
                window.check(probe.failed && probe.source.toString() === probe.fallbackSource.toString(), "Broken artwork must use the placeholder")
                scene.artSource = cover_a
                window.phase++
                break
            case 5:
                window.check(!probe.failed && probe.source.toString() === cover_a.toString(), "A valid next cover must recover after failure")
                scene.device = "MP3 player"
                scene.loadMedia(false)
                scene.artSource = cover_b
                window.phase++
                break
            case 6:
                window.check(scene.displayedArtSource.toString() === cover_b.toString(), "Digital screens must update during their fade")
                scene.cancelLoading()
                scene.artSource = ""
                window.phase++
                break
            case 7:
                window.check(probe.source.toString() === probe.fallbackSource.toString(), "Empty artwork must use the placeholder")
                console.log("MUSIC_DEVICES_SMOKE_PASS")
                Qt.quit()
                break
            }
        }
    }
    Timer { interval: 15000; running: true; onTriggered: { console.error("Artwork test timed out"); Qt.exit(2) } }
}
