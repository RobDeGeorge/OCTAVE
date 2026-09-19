pragma Singleton
import QtQuick

QtObject {
    id: root
    readonly property var options: ["Album art", "Record player", "MP3 player", "CD player", "Cassette player", "iPod"]
    property string mode: readMode()
    readonly property bool enabled: mode !== "Album art"
    // Probing compiles MusicDeviceScene.qml and therefore loads the Quick3D
    // plugin. Do it on first demand rather than at singleton creation so the
    // default "Album art" mode never pays for the 3D module at startup.
    // The result is cached in a plain JS object rather than a QML property:
    // isAvailable() is called from bindings (SettingCategory.description in
    // the Now Playing studio and Media settings), and assigning a property the
    // binding had just read logged "Binding loop detected for description".
    readonly property var _probe: ({ availability: -1 })
    function isAvailable() {
        if (_probe.availability < 0) {
            var component = Qt.createComponent("MusicDeviceScene.qml", Component.PreferSynchronous)
            _probe.availability = component.status === Component.Ready ? 1 : 0
        }
        return _probe.availability === 1
    }
    function readMode() {
        var value = settingsManager ? settingsManager.get_setting_with_default("musicDeviceModel", "Album art") : "Album art"
        return options.indexOf(value) >= 0 ? value : "Album art"
    }
    function select(value) {
        if (options.indexOf(value) < 0) return
        if (settingsManager) settingsManager.save_setting("musicDeviceModel", value)
        mode = value
    }
    property Connections settingsConnection: Connections {
        target: settingsManager
        function onGenericSettingChanged(key) {
            if (key === "musicDeviceModel") root.mode = root.readMode()
        }
    }
}
