// HomeScreen.qml
//
// What every hand-built Home screen (frontend/home/Home*.qml) is made on:
// the readings by id, the wording for them, what is playing, and the polling
// demand for exactly the readings the screen shows while it is on screen.
// A screen lists its OBD ids in `paramIds`, sets `usesImu` when it shows the
// motion sensor, and draws whatever it likes: these are not grid dashboards.
//
//   HomeScreen {
//       paramIds: ["SPEED", "RPM"]
//       Text { text: parent.text("SPEED") + " " + parent.unitOf("SPEED") }
//   }
//
// Sizes: `u` is 1 % of the short side, so a screen scales with its room in
// either orientation; `portrait` says which way up it is.
import QtQuick
import QtQuick.Controls
import ".." as App

Item {
    id: screen

    property StackView stackView: null
    property var mainWindow: null
    // No local songs and no phone with the media controls (MainMenu works it out)
    property bool musicEmpty: false

    property var paramIds: []
    property bool usesImu: false

    readonly property bool portrait: height > width
    readonly property real u: Math.max(1, Math.min(width, height) / 100)
    readonly property string font: App.Style.fontFamily

    readonly property bool obdConnected: typeof obdManager !== "undefined" && !!obdManager
                                         && obdManager.connected === true
    // (null-guarded: at shutdown the backend drops these before the page dies)
    readonly property bool imuConnected: typeof berryIMU !== "undefined" && !!berryIMU && berryIMU.connected
    readonly property var np: (typeof nowPlaying !== "undefined" && nowPlaying) ? nowPlaying : null
    readonly property bool hasTrack: !!np && np.hasTrack
    readonly property url art: np && np.artUrl !== "" ? np.artUrl : Qt.resolvedUrl("../assets/missing_art.png")
    readonly property string clockText: (typeof clock !== "undefined" && clock) ? clock.time : ""

    // ── Readings ──
    // NaN until there is one: "--", no fill, no needle
    function value(id) { return App.OBDParameterModel.sample(id) }
    function has(id) { return isFinite(value(id)) }
    function text(id, decimals) {
        var v = value(id)
        if (!isFinite(v)) return "--"
        var s = v.toFixed(decimals !== undefined ? decimals : App.OBDParameterModel.decimalsFor(id))
        return (s.charAt(0) === "-" && parseFloat(s) === 0) ? s.substring(1) : s
    }
    function unitOf(id) { var i = App.OBDParameterModel.displayInfo(id); return i ? i.unit : "" }
    function titleOf(id) { var i = App.OBDParameterModel.displayInfo(id); return i ? (i.shortTitle || i.title) : id }
    // Where the reading stands in its range, 0 to 1 (0 without one)
    function norm(id) {
        var i = App.OBDParameterModel.displayInfo(id), v = value(id)
        if (!i || !isFinite(v) || !(i.max > i.min)) return 0
        return Math.max(0, Math.min(1, (v - i.min) / (i.max - i.min)))
    }
    function duration(ms) {
        if (typeof units !== "undefined" && units) return units.formatDuration(ms)
        var s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    // "SW" for 225
    function cardinal(deg) {
        if (!isFinite(deg)) return ""
        return ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][Math.round(((deg % 360) + 360) % 360 / 45) % 8]
    }

    // ── Demand: what this screen shows, while it is on screen ──
    readonly property var _demandIds: visible ? paramIds : []
    function _pushDemand(ids) {
        if (typeof obdManager !== "undefined" && obdManager && obdManager.setParameterDemand)
            obdManager.setParameterDemand("homeScreen", ids)
    }
    on_DemandIdsChanged: _pushDemand(_demandIds)
    Component.onCompleted: _pushDemand(_demandIds)
    Component.onDestruction: _pushDemand([])
    App.ImuDemand {
        consumer: "homeScreen"
        wanted: screen.visible && screen.usesImu
    }
}
