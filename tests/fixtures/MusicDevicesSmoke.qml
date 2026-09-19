import QtQuick
import QtQuick.Window
import "../../frontend" as App

Window {
    id: window
    visible: true
    width: 640
    height: 480
    property int step: 0
    property real stoppedAngle: 0
    property var models: ["Record player", "MP3 player", "CD player", "Cassette player", "iPod"]
    App.MusicDeviceScene {
        id: device
        animateOnArrival: false
        anchors.fill: parent
        device: window.models[0]
        trackTitle: "Test track"
        artist: "Test artist"
        progress: 0.5
    }
    function check(condition, message) {
        if (!condition) { console.error(message); Qt.exit(1) }
    }
    Timer {
        interval: 350
        running: true
        repeat: true
        onTriggered: {
            window.check(device.error === "", device.error)
            if (!device.ready) return
            switch (window.step) {
            case 0:
                window.check(App.MusicDevicePreference.isAvailable(), "Optional module probe failed")
                window.check(App.MusicDevicePreference.mode === "Album art", "Default must retain album art")
                App.MusicDevicePreference.select("Cassette player")
                window.check(App.MusicDevicePreference.mode === "Cassette player", "Selection failed")
                testSettings.save_setting("musicDeviceModel", "iPod")
                window.check(App.MusicDevicePreference.mode === "iPod", "External changes must propagate")
                testSettings.save_setting("musicDeviceModel", "unknown")
                window.check(App.MusicDevicePreference.mode === "Album art", "Invalid selection must fall back")
                device.playing = true
                break
            case 1:
                window.check(device.spin > 0, "Playing record must rotate")
                device.playing = false
                window.stoppedAngle = device.spin
                break
            case 2:
                window.check(device.spin === window.stoppedAngle, "Pause must hold rotation")
                device.playing = true
                device.visible = false
                break
            case 3:
                window.check(device.spin === window.stoppedAngle, "Hidden scene must stop its timer")
                device.visible = true
                device.device = window.models[1]
                break
            case 4:
                window.check(device.spin === 0, "Digital player must not run a rotation timer")
                device.device = window.models[2]
                break
            case 5: device.device = window.models[3]; break
            case 6: device.device = window.models[4]; break
            case 7:
                device.trackTitle = "Updated title"
                device.progress = 0.8
                device.width = 180
                device.height = 240
                testSettings.reset_to_defaults()
                window.check(App.MusicDevicePreference.mode === "Album art", "Reset must restore album art")
                console.log("MUSIC_DEVICES_SMOKE_PASS")
                Qt.quit()
                break
            }
            window.step++
        }
    }
    Timer { interval: 20000; running: true; onTriggered: { console.error("Model loading timed out"); Qt.exit(2) } }
}
