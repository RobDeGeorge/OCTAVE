// PIDPicker.qml
//
// Searchable parameter list for the dashboard editor. Grouped by the car's
// systems (ParameterCatalog `group`: Engine, Fuel & air, Temperatures, ...),
// with the last few picks under Recent at the top, and each row's live value
// when there is one (a connected car, or the editor's demo data). Filtered
// to the kinds the widget accepts (WidgetCatalog `supportedKinds`, mirroring
// each gauge's `octaveSupportedKinds`); ["*"] lists everything. The search
// field is focused only when tapped, so a touch screen doesn't open its
// keyboard over the list. Tapping a row emits pidPicked(paramId), records it
// as recent (the dashboardRecentPids setting) and closes.
//
// Vehicle gate: with no vehicle scanned this session every PID is listed
// normally (build for any car). Once a connected vehicle has been scanned,
// its supported PIDs sort first within each group and the rest are greyed
// out — still pickable (a dashboard may be meant for another car, or shared),
// but labelled as not readable on the current vehicle.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../.." as App

Popup {
    id: picker
    objectName: "pidPicker"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    // Kinds the target widget accepts (["*"] = everything).
    property var supportedKinds: ["*"]
    // Currently-bound PID, highlighted in the list.
    property string currentParamId: ""

    signal pidPicked(string paramId)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width * 0.7, dp(520))
    height: parent.height * 0.85
    modal: true
    focus: true
    padding: dp(16)

    // Shown above the list ("Choose data" / "What do you want to see?")
    property string title: "Choose data"

    onAboutToShow: {
        searchField.text = ""
        searchField.focus = false
        _recent = _loadRecent()
    }

    // ── Recent picks ─────────────────────────────────────────────────────
    readonly property int maxRecent: 6
    property var _recent: []
    function _loadRecent() {
        if (typeof settingsManager === "undefined" || !settingsManager || !settingsManager.get_setting_with_default)
            return []
        try {
            var list = JSON.parse(settingsManager.get_setting_with_default("dashboardRecentPids", "[]"))
            return Array.isArray(list) ? list : []
        } catch (e) {
            return []
        }
    }
    function _remember(pid) {
        var list = [pid].concat(_recent.filter(function(x) { return x !== pid })).slice(0, maxRecent)
        _recent = list
        if (typeof settingsManager !== "undefined" && settingsManager && settingsManager.save_setting)
            settingsManager.save_setting("dashboardRecentPids", JSON.stringify(list))
    }
    function _pick(pid) {
        _remember(pid)
        pidPicked(pid)
        close()
    }

    // The live value as the gauges show it, "" with no reading
    function _live(pid, decimals) {
        var v = App.OBDParameterModel.sample(pid)
        return isFinite(v) ? v.toFixed(Math.max(0, Math.min(3, decimals))) : ""
    }

    function _acceptsKind(kind) {
        for (var i = 0; i < supportedKinds.length; i++) {
            if (supportedKinds[i] === "*" || supportedKinds[i] === kind)
                return true
        }
        return false
    }

    // Flat row list: { header: true, label } separators followed by
    // { header: false, pid, title, unit, decimals, supported } entries,
    // Recent first, then each group in ParameterCatalog.groups order. A
    // plain array, so search filtering is a binding re-evaluation.
    readonly property var _rows: {
        var q = searchField.text.trim().toLowerCase()
        var gated = App.OBDParameterModel.vehicleKnown
        var order = (typeof parameterCatalog !== "undefined" && parameterCatalog) ? parameterCatalog.groups : []
        var byGroup = {}
        var byId = {}
        var all = App.OBDParameterModel.displayParameters   // in the gauges' units
        for (var i = 0; i < all.length; i++) {
            var p = all[i]
            if (p.aliasOf) continue
            if (!_acceptsKind(p.kind)) continue
            if (q.length > 0 &&
                p.title.toLowerCase().indexOf(q) < 0 &&
                p.id.toLowerCase().indexOf(q) < 0 &&
                (p.group || "").toLowerCase().indexOf(q) < 0) continue
            var g = p.group || "Other"
            if (!byGroup[g]) byGroup[g] = []
            byGroup[g].push(p)
            byId[p.id] = p
        }
        function row(pp) {
            return { "header": false, "pid": pp.id, "title": pp.title, "unit": pp.unit,
                     "decimals": pp.decimals, "supported": !gated || App.OBDParameterModel.isSupported(pp.id) }
        }
        var rows = []
        var recent = _recent.filter(function(id) { return byId[id] !== undefined })
        if (recent.length > 0 && q.length === 0) {
            rows.push({ "header": true, "label": "Recent" })
            for (var r = 0; r < recent.length; r++) rows.push(row(byId[recent[r]]))
        }
        var groups = order.slice()
        for (var key in byGroup) if (groups.indexOf(key) === -1) groups.push(key)
        for (var k = 0; k < groups.length; k++) {
            var list = byGroup[groups[k]]
            if (!list) continue
            if (gated) {
                // Stable partition: this vehicle's PIDs first.
                list = list.filter(function(x) { return App.OBDParameterModel.isSupported(x.id) })
                    .concat(list.filter(function(x) { return !App.OBDParameterModel.isSupported(x.id) }))
            }
            rows.push({ "header": true, "label": groups[k] })
            for (var n = 0; n < list.length; n++) rows.push(row(list[n]))
        }
        return rows
    }

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: picker.dpMin(12, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: picker.dp(12)

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: picker.title
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 1.2
            font.bold: true
        }

        Text {
            objectName: "pidPickerVehicleNote"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: App.OBDParameterModel.vehicleKnown
                  ? "Your vehicle's PIDs are listed first. Greyed-out PIDs can still be added, but won't read on this vehicle."
                  : "No vehicle connected. Showing every PID."
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.8
            opacity: 0.85
        }

        TextField {
            id: searchField
            objectName: "pidSearchField"
            Layout.fillWidth: true
            Layout.preferredHeight: picker.dp(44)
            placeholderText: "Search…"
            focus: false
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            color: App.Style.primaryTextColor
        }

        ListView {
            id: pidList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: picker._rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                objectName: modelData.header ? "" : "pidRow_" + modelData.pid
                width: pidList.width
                height: modelData.header ? picker.dp(34) : picker.dp(52)
                readonly property bool _supported: modelData.header || modelData.supported !== false

                // Section header
                Text {
                    visible: modelData.header === true
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: picker.dp(4)
                    text: modelData.header ? modelData.label : ""
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.bold: true
                    opacity: 0.8
                }

                // PID row
                Rectangle {
                    visible: modelData.header !== true
                    anchors.fill: parent
                    anchors.topMargin: picker.dp(2)
                    anchors.bottomMargin: picker.dp(2)
                    radius: picker.dpMin(6, 2)
                    color: picker.currentParamId === modelData.pid
                           ? Qt.lighter(App.Style.obdBoxBackground, 1.25)
                           : (rowMouse.pressed
                              ? Qt.lighter(App.Style.obdBoxBackground, 1.18)
                              : Qt.darker(App.Style.obdBoxBackground, 1.08))
                    border.color: picker.currentParamId === modelData.pid
                                  ? App.Style.accent
                                  : "transparent"
                    border.width: 1
                    opacity: parent._supported ? 1.0 : 0.45

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: picker.dp(12)
                        anchors.rightMargin: picker.dp(12)
                        spacing: picker.dp(8)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: modelData.header ? "" : modelData.title
                                elide: Text.ElideRight
                                color: App.Style.obdValueColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText
                            }
                            Text {
                                visible: !modelData.header && modelData.supported === false
                                text: "Not supported by this vehicle"
                                color: App.Style.obdLabelColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText * 0.7
                            }
                        }
                        // The live value, when there is one
                        Text {
                            objectName: modelData.header ? "" : "pidLive_" + modelData.pid
                            text: modelData.header ? "" : picker._live(modelData.pid, modelData.decimals)
                            visible: text.length > 0
                            color: App.Style.accent
                            font.family: App.Style.fontFamily
                            font.bold: true
                            font.pixelSize: App.Spacing.overallText * 0.95
                        }
                        Text {
                            text: modelData.header ? "" : modelData.unit
                            color: App.Style.obdLabelColor
                            font.family: App.Style.fontFamily
                            font.pixelSize: App.Spacing.overallText * 0.85
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: picker._pick(modelData.pid)
                    }
                }
            }

            // Empty-state when the search matches nothing.
            Text {
                anchors.centerIn: parent
                visible: pidList.count === 0
                text: "No matching PIDs"
                color: App.Style.obdLabelColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText
                opacity: 0.7
            }
        }
    }
}
