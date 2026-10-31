// ImportReportPopup.qml
//
// What an import brought in, shown after every successful one: a live
// miniature of the new dashboard and a line for each thing that didn't come
// across as it was (dashboardManager.lastImportReport), checked against the
// car that is connected now: values it doesn't report show "--". "Show it"
// makes it the active dashboard, "Edit" opens the editor on it.
//
// lines(report) is plain JS over the report, so the dashboard tests check
// the wording without a car.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as Dashboards
import ".." as App

Popup {
    id: popup
    objectName: "importReportPopup"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var report: ({})
    property var spec: null
    property real stageWidth: parent ? parent.width : 1280
    property real stageHeight: parent ? parent.height : 720

    signal showRequested(string id)
    signal editRequested(string id)

    function plural(n, one, many) { return n + " " + (n === 1 ? one : many) }

    function _titleOf(id) {
        var info = App.OBDParameterModel.getParamInfo(id)
        return info && info.title ? info.title : id
    }

    // [{text, warn}] for the report, most important first
    function lines(r) {
        var out = []
        if (!r || !r.id) return out
        var ids = r.paramIds || []
        var unknownIds = ids.filter(function(id) { return !App.OBDParameterModel.hasParameter(id) })
        var known = ids.filter(function(id) { return App.OBDParameterModel.hasParameter(id) })
        if (App.OBDParameterModel.vehicleKnown) {
            var missing = known.filter(function(id) { return !App.OBDParameterModel.isSupported(id) })
            if (missing.length > 0)
                out.push({ warn: true, text: plural(missing.length, "value your car doesn't report", "values your car doesn't report")
                                             + " (shown as --): " + missing.map(_titleOf).join(", ") })
            else if (known.length > 0)
                out.push({ warn: false, text: "Your car reports every value it uses." })
        } else if (known.length > 0) {
            out.push({ warn: false, text: "No car connected, so it can't be checked against your car yet. "
                                          + "Values your car doesn't report will show --." })
        }
        if (unknownIds.length > 0)
            out.push({ warn: true, text: plural(unknownIds.length, "value", "values") + " this version of OCTAVE doesn't know (shown as --): "
                                         + unknownIds.join(", ") })
        var types = r.unknownTypes || []
        if (types.length > 0)
            out.push({ warn: true, text: plural(types.length, "widget", "widgets") + " this version doesn't have (shown as a placeholder): "
                                         + types.join(", ") })
        var dropped = r.droppedCells || []
        if (dropped.length > 0)
            out.push({ warn: true, text: plural(dropped.length, "widget was", "widgets were") + " left out: "
                                         + (dropped.length === 1 ? "it overlapped" : "they overlapped") + " another" })
        if (r.droppedProps > 0)
            out.push({ warn: false, text: plural(r.droppedProps, "setting", "settings") + " had a value this version can't use: "
                                          + (r.droppedProps === 1 ? "it is" : "they are") + " back to the default" })
        if (r.movedCells > 0)
            out.push({ warn: false, text: plural(r.movedCells, "widget was", "widgets were") + " moved back onto the grid" })
        if (r.portraitDropped)
            out.push({ warn: false, text: "Its portrait layout didn't fit; OCTAVE works one out instead" })
        return out
    }

    function openFor(r) {
        report = r || ({})
        spec = report.id ? dashboardManager.loadDashboard(report.id) : null
        open()
    }

    readonly property var _lines: lines(report)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - dp(24), dp(720))
    height: Math.min(parent.height - dp(24), implicitHeight)
    modal: true
    focus: true
    padding: dp(18)

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: popup.dpMin(12, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    component PillButton: Rectangle {
        id: pill
        property string text: ""
        property bool primary: false
        signal clicked()
        Layout.preferredHeight: popup.dp(48)
        Layout.preferredWidth: pillText.implicitWidth + popup.dp(32)
        radius: height / 2
        color: primary ? App.Style.accent
               : pillMouse.pressed ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
               : Qt.darker(App.Style.obdBoxBackground, 1.25)
        border.width: primary ? 0 : 1
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.text
            color: pill.primary ? App.Style.obdBoxBackground : App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.bold: pill.primary
            font.pixelSize: App.Spacing.overallText * 0.95
        }
        MouseArea { id: pillMouse; anchors.fill: parent; onClicked: pill.clicked() }
    }

    contentItem: ColumnLayout {
        spacing: popup.dp(12)

        Text {
            objectName: "importReportTitle"
            Layout.fillWidth: true
            text: "Imported \"" + (popup.report.label || "") + "\""
            elide: Text.ElideRight
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 1.2
            font.bold: true
        }

        Rectangle {
            id: preview
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            radius: popup.dpMin(8, 2)
            color: App.Style.backgroundColor
            clip: true
            Item {
                width: Math.max(1, popup.stageWidth)
                height: Math.max(1, popup.stageHeight)
                anchors.centerIn: parent
                scale: Math.min(preview.width / width, preview.height / height)
                Dashboards.DashboardRenderer {
                    anchors.fill: parent
                    spec: popup.visible ? popup.spec : null
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: popup.plural(popup.report.cells || 0, "widget", "widgets")
                  + (popup._lines.length === 0 ? ". Everything came across." : ".")
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.9
        }

        Repeater {
            model: popup._lines
            delegate: RowLayout {
                objectName: "importReportLine_" + index
                Layout.fillWidth: true
                spacing: popup.dp(8)
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: popup.dp(6)
                    width: popup.dp(8); height: width; radius: width / 2
                    color: modelData.warn ? App.Style.statusWarning : App.Style.obdLabelColor
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.text
                    wrapMode: Text.WordWrap
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.9
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: popup.dp(4)
            spacing: popup.dp(10)
            Item { Layout.fillWidth: true }
            PillButton {
                objectName: "importReportEdit"
                text: "Edit"
                onClicked: { var id = popup.report.id; popup.close(); popup.editRequested(id) }
            }
            PillButton {
                objectName: "importReportDone"
                text: "Done"
                onClicked: popup.close()
            }
            PillButton {
                objectName: "importReportShow"
                text: "Show it"
                primary: true
                onClicked: { var id = popup.report.id; popup.close(); popup.showRequested(id) }
            }
        }
    }
}
