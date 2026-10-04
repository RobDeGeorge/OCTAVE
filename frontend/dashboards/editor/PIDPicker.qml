// PIDPicker.qml
//
// Searchable PID list for the dashboard editor (Phase 3 Milestone B).
// Lists OBDParameterModel.allParameters grouped by semantic `kind`, filtered
// to the kinds the selected widget declares it supports (WidgetCatalog
// `supportedKinds`, mirroring each gauge's `octaveSupportedKinds`). Tapping a
// row emits pidPicked(paramId) and closes.
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

    onOpened: {
        searchField.text = ""
        searchField.forceActiveFocus()
    }

    function _acceptsKind(kind) {
        for (var i = 0; i < supportedKinds.length; i++) {
            if (supportedKinds[i] === "*" || supportedKinds[i] === kind)
                return true
        }
        return false
    }

    function _kindLabel(kind) {
        switch (kind) {
        case "percentage":    return "Percentage"
        case "temperature":   return "Temperature"
        case "voltage":       return "Voltage"
        case "pressure":      return "Pressure"
        case "bidirectional": return "Bidirectional (±)"
        default:              return "General"
        }
    }

    // Flat row list: { header: true, label } separators followed by
    // { header: false, pid, title, unit } entries. Built as a plain array so
    // search filtering is a simple binding re-evaluation.
    readonly property var _rows: {
        var kindOrder = ["numeric", "percentage", "temperature", "pressure", "voltage", "bidirectional"]
        var q = searchField.text.trim().toLowerCase()
        var gated = App.OBDParameterModel.vehicleKnown
        var groups = {}
        var all = App.OBDParameterModel.allParameters
        for (var i = 0; i < all.length; i++) {
            var p = all[i]
            if (!_acceptsKind(p.kind)) continue
            if (q.length > 0 &&
                p.title.toLowerCase().indexOf(q) < 0 &&
                p.id.toLowerCase().indexOf(q) < 0) continue
            if (!groups[p.kind]) groups[p.kind] = []
            groups[p.kind].push(p)
        }
        var rows = []
        for (var k = 0; k < kindOrder.length; k++) {
            var kind = kindOrder[k]
            if (!groups[kind]) continue
            if (gated) {
                // Stable partition: this vehicle's PIDs first.
                groups[kind] = groups[kind].filter(function(x) { return App.OBDParameterModel.isSupported(x.id) })
                    .concat(groups[kind].filter(function(x) { return !App.OBDParameterModel.isSupported(x.id) }))
            }
            rows.push({ "header": true, "label": _kindLabel(kind) })
            for (var n = 0; n < groups[kind].length; n++) {
                var pp = groups[kind][n]
                rows.push({ "header": false, "pid": pp.id, "title": pp.title, "unit": pp.unit,
                            "supported": !gated || App.OBDParameterModel.isSupported(pp.id) })
            }
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
            text: "Choose a PID"
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
            placeholderText: "Search…"
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
                        onClicked: {
                            picker.pidPicked(modelData.pid)
                            picker.close()
                        }
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
