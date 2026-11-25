// HomeDashboard.qml
//
// Home as a dashboard: the dashboard dashboardManager.homeDashboardId names
// (the built-in "home" until the user picks or makes another), drawn by the
// DashboardRenderer. Dashboards are not made in the app: each is a JSON
// file in <config>/dashboards/ (docs/GAUGE_AUTHORING.md), and Settings >
// Display > Home Screen picks the one shown here.
//
// A tap on a widget can open its page (music, the OBD menu, Sensors, the
// vehicle settings; the action DashboardRenderer.cellTapped() gives). That
// is switched off for now: tapsOpenPages.
//
// With nothing connected the widgets would only read "--", so this page lays
// the first-run prompts over them: "No vehicle connected" + Connect over the
// OBD readings (over their block when that holds nothing else, else a strip
// along the bottom), the music prompt over the Now Playing widget, and "No
// sensor" on the motion widgets.
import QtQuick
import QtQuick.Controls
import "." as App
import "dashboards" as Dashboards
import OCTAVE.OBD 1.0

Item {
    id: home
    objectName: "homeDashboard"

    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property StackView stackView
    property var mainWindow
    // No local songs and no phone with the media controls (MainMenu works it out)
    property bool musicEmpty: false

    readonly property var _dm: (typeof dashboardManager !== "undefined" && dashboardManager) ? dashboardManager : null
    readonly property string homeId: _dm ? _dm.homeDashboardId : ""
    readonly property bool obdConnected: typeof obdManager !== "undefined" && !!obdManager
                                         && obdManager.connectionState === OBDManager.Connected
    // (null-guarded: at shutdown the backend drops berryIMU before the page dies)
    readonly property bool imuConnected: typeof berryIMU !== "undefined" && !!berryIMU && berryIMU.connected

    // The spec keeps its identity while its content is the same: a new
    // object would tear the widgets down and load them again (OBDMenu's
    // _specFor has the same rule), and dashboardsChanged fires for any file
    property var spec: null
    property string _specJson: ""
    function reload() {
        var loaded = (_dm && homeId !== "") ? _dm.loadDashboard(homeId) : null
        var usable = loaded && loaded.cells ? loaded : null
        var json = usable ? JSON.stringify(usable) : ""
        if (json === _specJson)
            return
        _specJson = json
        spec = usable
    }
    onHomeIdChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: home._dm
        function onDashboardsChanged() { home.reload() }
    }

    // ── Demand: exactly what this dashboard shows, while it is on screen ──
    readonly property var _demandIds: visible ? renderer.paramIds : []
    function _pushDemand(ids) {
        if (typeof obdManager !== "undefined" && obdManager && obdManager.setParameterDemand)
            obdManager.setParameterDemand("homeDashboard", ids)
    }
    on_DemandIdsChanged: _pushDemand(_demandIds)
    Component.onDestruction: _pushDemand([])
    App.ImuDemand {
        consumer: "homeDashboard"
        wanted: home.visible && renderer.usesImu
    }

    // Off while the new Home is being worked out (2026-11-24): a tap on a
    // widget or beside a prompt goes nowhere. The prompts' own buttons still
    // work.
    property bool tapsOpenPages: false
    function openAction(action) {
        if (!stackView || !tapsOpenPages) return
        if (action === "media") {
            var page = (typeof settingsManager !== "undefined" && settingsManager) ? settingsManager.musicButtonDefaultPage : "mediaRoom"
            stackView.openPage(page === "mediaPlayer" ? "MediaPlayer.qml" : "MediaRoom.qml")
        } else if (action === "obd") {
            stackView.openPage("OBDMenu.qml")
        } else if (action === "sensors") {
            stackView.openPage("SensorHome.qml")
        } else if (action === "vehicle") {
            if (mainWindow) mainWindow.navigateToSettings("deviceSettings", "device_vehicle")
        }
    }

    Dashboards.DashboardRenderer {
        id: renderer
        objectName: "homeDashboardRenderer"
        anchors.fill: parent
        spec: home.spec
        interactive: true
        onCellTapped: function(action, index) { home.openAction(action) }
    }

    // ── What the cells are, for the prompts ──
    // [{index, kind: "obd" | "imu" | "media" | "other", rect}] as laid out now
    readonly property var _cells: {
        var out = []
        var cells = (spec && spec.cells) ? spec.cells : []
        for (var i = 0; i < cells.length; i++) {
            var c = cells[i], kind = "other"
            var meta = App.WidgetCatalog.metaFor(c.type)
            if (c.type === "NowPlayingWidget") kind = "media"
            else if ((meta && meta.usesImu === true) || (c.paramId && App.OBDParameterModel.isSensorParameter(c.paramId))) kind = "imu"
            else if (c.paramId || (meta && meta.demand && meta.demand.length > 0)) kind = "obd"
            out.push({ "index": i, "kind": kind, "rect": renderer.cellRect(i) })
        }
        return out
    }
    function _ofKind(kind) { return _cells.filter(function(c) { return c.kind === kind }) }

    // The OBD readings' block: their bounding box, usable for the prompt
    // when no other widget stands in it and it has the room
    readonly property var _obdBlock: {
        var obd = _ofKind("obd")
        if (obd.length === 0) return null
        var x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (var i = 0; i < obd.length; i++) {
            var r = obd[i].rect
            x0 = Math.min(x0, r.x); y0 = Math.min(y0, r.y)
            x1 = Math.max(x1, r.x + r.width); y1 = Math.max(y1, r.y + r.height)
        }
        var clean = x1 - x0 >= dp(220) && y1 - y0 >= dp(130)
        for (var k = 0; clean && k < _cells.length; k++) {
            var o = _cells[k]
            if (o.kind === "obd") continue
            if (o.rect.x < x1 - 1 && o.rect.x + o.rect.width > x0 + 1 && o.rect.y < y1 - 1 && o.rect.y + o.rect.height > y0 + 1)
                clean = false
        }
        return { "x": x0, "y": y0, "width": x1 - x0, "height": y1 - y0, "clean": clean }
    }
    readonly property bool _obdPrompt: !obdConnected && _obdBlock !== null

    // No adapter, over the readings' block
    Rectangle {
        objectName: "homeOBDPrompt"
        visible: home._obdPrompt && home._obdBlock.clean
        x: home._obdBlock ? home._obdBlock.x : 0
        y: home._obdBlock ? home._obdBlock.y : 0
        width: home._obdBlock ? home._obdBlock.width : 0
        height: home._obdBlock ? home._obdBlock.height : 0
        radius: dp(8)
        color: App.Style.obdBoxBackground
        border.color: Qt.darker(App.Style.obdBarColor, 1.4)
        border.width: 1
        // A tap beside the button still opens the OBD menu
        TapHandler { onTapped: home.openAction("obd") }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - dp(24), dp(520))
            spacing: dp(12)
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "No vehicle connected"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                font.family: App.Style.fontFamily
            }
            Text {
                width: parent.width
                visible: parent.parent.height >= dp(230)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "Plug in your OBD2 adapter and connect it to see live readings here"
                color: App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 0.9
                font.family: App.Style.fontFamily
            }
            App.OBDConnectButton {
                objectName: "homeOBDConnectButton"
                anchors.horizontalCenter: parent.horizontalCenter
                mainWindow: home.mainWindow
                fontFamily: App.Style.fontFamily
            }
        }
    }

    // No adapter, and the readings are scattered among other widgets: a strip
    // along the bottom, or just above a widget that spans the bottom middle
    // (Home's music card under the phone)
    readonly property real _stripBottom: {
        var bottom = height - dp(10)
        for (var i = 0; i < _cells.length; i++) {
            var r = _cells[i].rect
            if (r.x <= width / 2 && r.x + r.width >= width / 2 && r.y + r.height >= height - dp(20))
                bottom = Math.min(bottom, r.y - dp(24))
        }
        return bottom
    }
    Rectangle {
        objectName: "homeOBDPromptStrip"
        visible: home._obdPrompt && !home._obdBlock.clean
        y: home._stripBottom - height
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - dp(20), stripRow.implicitWidth + dp(28))
        height: stripRow.implicitHeight + dp(12)
        radius: dp(8)
        color: App.Style.obdBoxBackground
        border.color: App.Style.accent
        border.width: 1
        Row {
            id: stripRow
            anchors.centerIn: parent
            spacing: dp(14)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "No vehicle connected"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.bold: true
                font.family: App.Style.fontFamily
            }
            App.OBDConnectButton {
                objectName: "homeOBDConnectButtonStrip"
                anchors.verticalCenter: parent.verticalCenter
                mainWindow: home.mainWindow
                fontFamily: App.Style.fontFamily
            }
        }
    }

    // No Music Loaded, over each Now Playing widget with the room for it
    Repeater {
        model: home.musicEmpty ? home._ofKind("media") : []
        delegate: Rectangle {
            required property var modelData
            // (a low cell keeps the buttons and drops the words: maxHeight)
            visible: modelData.rect.width >= home.dp(200)
                     && modelData.rect.height >= App.Spacing.bottomBarNavButtonHeight + home.dp(16)
            x: modelData.rect.x; y: modelData.rect.y
            width: modelData.rect.width; height: modelData.rect.height
            radius: home.dp(8)
            color: App.Style.obdBoxBackground
            border.color: Qt.darker(App.Style.obdBarColor, 1.4)
            border.width: 1
            App.MusicEmptyPrompt {
                objectName: "homeMusicPrompt"
                anchors.centerIn: parent
                width: parent.width - home.dp(24)
                maxHeight: parent.height - home.dp(16)
                stackView: home.stackView
                mainWindow: home.mainWindow
                fontFamily: App.Style.fontFamily
            }
        }
    }

    // No motion sensor: said on each widget that reads it
    Repeater {
        model: home.imuConnected ? [] : home._ofKind("imu")
        delegate: Text {
            required property var modelData
            // Top left: clear of the gauges' own readouts along the bottom
            x: modelData.rect.x + home.dp(8)
            y: modelData.rect.y + home.dp(5)
            width: modelData.rect.width - home.dp(16)
            elide: Text.ElideRight
            text: "No sensor"
            color: App.Style.secondaryTextColor
            font.pixelSize: Math.max(8, Math.min(App.Spacing.overallText * 0.7, modelData.rect.height * 0.14))
            font.family: App.Style.fontFamily
        }
    }
}
