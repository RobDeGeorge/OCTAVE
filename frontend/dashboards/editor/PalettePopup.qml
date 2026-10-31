// PalettePopup.qml
//
// The editor's widget gallery. Opened by Add here / + Add; shows every
// widget from the WidgetCatalog singleton as a live miniature (the real
// widget on the catalog's `demo` PID and props, on demo data while the
// gallery is open), with its name and one-line description, filtered by
// category and by a search field (focused only when tapped, so a touch
// screen doesn't throw up its keyboard).
//
// Tapping a card emits widgetPicked(type) and closes; the editor places the
// widget where it was asked to. "What do you want to see?" emits
// dataFirstRequested() instead: the editor asks for a PID and places the
// widget the catalog suggests for it.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../.." as App

Popup {
    id: gallery
    objectName: "widgetPalette"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    signal widgetPicked(string type)
    signal presetPicked(var preset)
    signal dataFirstRequested()

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - dp(24), dp(900))
    height: parent.height * 0.9
    modal: true
    focus: true
    padding: dp(18)

    property string category: ""
    property string query: ""
    property string note: ""
    property bool _demoWas: false

    readonly property var shown: {
        var out = []
        var all = App.WidgetCatalog.presets.concat(App.WidgetCatalog.widgets)
        var q = query.trim().toLowerCase()
        for (var i = 0; i < all.length; i++) {
            var w = all[i]
            if (category !== "" && w.category !== category) continue
            if (q !== "" && (w.displayName + " " + (w.description || "") + " " + (w.category || ""))
                    .toLowerCase().indexOf(q) === -1) continue
            out.push(w)
        }
        return out
    }

    onAboutToShow: {
        category = ""
        note = ""
        searchField.text = ""
        _demoWas = App.OBDParameterModel.simulationActive
        App.OBDParameterModel.simulationActive = true
    }
    onClosed: App.OBDParameterModel.simulationActive = _demoWas

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: gallery.dpMin(12, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    component Chip: Rectangle {
        id: chip
        property string text: ""
        property bool checked: false
        signal clicked()
        implicitHeight: gallery.dp(44)
        implicitWidth: chipText.implicitWidth + gallery.dp(28)
        radius: height / 2
        color: checked ? App.Style.accent
               : chipMouse.pressed ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
               : Qt.darker(App.Style.obdBoxBackground, 1.25)
        border.width: checked ? 0 : 1
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            color: chip.checked ? App.Style.obdBoxBackground : App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.bold: chip.checked
            font.pixelSize: App.Spacing.overallText * 0.9
        }
        MouseArea { id: chipMouse; anchors.fill: parent; onClicked: chip.clicked() }
    }

    contentItem: ColumnLayout {
        spacing: gallery.dp(12)

        RowLayout {
            Layout.fillWidth: true
            spacing: gallery.dp(12)
            Text {
                id: paletteTitle
                Layout.fillWidth: true
                text: "Add a widget"
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
            }
            // A widget someone shared (My widgets)
            Chip {
                objectName: "palettePastePreset"
                text: "Paste a widget"
                onClicked: {
                    var id = dashboardManager.importWidgetPresetFromClipboard()
                    gallery.note = id ? "Added to Mine" : (dashboardManager.lastShareError || "Nothing to paste")
                    if (id) gallery.category = "Mine"
                }
            }
            // Data first: pick what to see, get the widget that suits it
            Chip {
                objectName: "paletteDataFirst"
                text: "What do you want to see?"
                checked: true
                onClicked: {
                    gallery.close()
                    gallery.dataFirstRequested()
                }
            }
        }

        Text {
            objectName: "paletteNote"
            visible: gallery.note !== ""
            Layout.fillWidth: true
            text: gallery.note
            wrapMode: Text.WordWrap
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.85
        }

        TextField {
            id: searchField
            objectName: "paletteSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: gallery.dp(44)
            placeholderText: "Search widgets"
            placeholderTextColor: Qt.darker(App.Style.obdLabelColor, 1.4)
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            focus: false
            onTextChanged: gallery.query = text
            background: Rectangle {
                radius: gallery.dpMin(6, 2)
                color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                border.color: searchField.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                border.width: 1
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: gallery.dp(44)
            contentWidth: chipRow.implicitWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: chipRow
                spacing: gallery.dp(8)
                Chip {
                    objectName: "paletteCategory_all"
                    text: "All"
                    checked: gallery.category === ""
                    onClicked: gallery.category = ""
                }
                Repeater {
                    model: (App.WidgetCatalog.presets.length > 0 ? ["Mine"] : []).concat(App.WidgetCatalog.categories)
                    delegate: Chip {
                        objectName: "paletteCategory_" + modelData
                        text: modelData
                        checked: gallery.category === modelData
                        onClicked: gallery.category = modelData
                    }
                }
            }
        }

        ScrollView {
            id: paletteScroll
            objectName: "widgetPaletteScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            GridLayout {
                id: paletteGrid
                width: paletteScroll.availableWidth
                columns: Math.max(2, Math.min(4, Math.floor(width / gallery.dp(190))))
                rowSpacing: gallery.dp(12)
                columnSpacing: gallery.dp(12)

                Repeater {
                    model: gallery.visible ? gallery.shown : []
                    delegate: Rectangle {
                        id: card
                        objectName: modelData.preset ? "palettePreset_" + modelData.preset.id
                                                     : "paletteCard_" + modelData.type
                        Layout.fillWidth: true
                        Layout.preferredHeight: gallery.dp(108) + cardText.implicitHeight + gallery.dp(16)
                        radius: gallery.dpMin(8, 2)
                        color: cardMouse.pressed
                               ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                               : Qt.darker(App.Style.obdBoxBackground, 1.12)
                        border.color: cardMouse.containsMouse
                                      ? App.Style.accent
                                      : Qt.darker(App.Style.obdBarColor, 1.6)
                        border.width: 1

                        // The real widget, at about its natural shape
                        Item {
                            id: mini
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.topMargin: gallery.dp(8)
                            readonly property real _boxW: card.width - gallery.dp(16)
                            readonly property real _boxH: gallery.dp(100)
                            readonly property real _aspect: modelData.aspect > 0 ? modelData.aspect : 1
                            width: Math.min(_boxW, _boxH * _aspect)
                            height: Math.min(_boxH, _boxW / _aspect)
                            clip: true
                            Loader {
                                anchors.fill: parent
                                source: modelData.url
                                onLoaded: {
                                    var d = modelData.demo || {}
                                    if (d.paramId && item.paramId !== undefined) item.paramId = d.paramId
                                    var props = d.props || {}
                                    for (var k in props)
                                        if (item[k] !== undefined) item[k] = props[k]
                                }
                            }
                        }

                        ColumnLayout {
                            id: cardText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: gallery.dp(8)
                            spacing: gallery.dp(2)
                            Text {
                                Layout.fillWidth: true
                                text: modelData.displayName
                                elide: Text.ElideRight
                                color: App.Style.obdValueColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.description || ""
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                color: App.Style.obdLabelColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText * 0.72
                            }
                        }

                        // Over the miniature, so a tap on a media widget's
                        // buttons picks the card instead of pressing them
                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.preset) gallery.presetPicked(modelData.preset)
                                else gallery.widgetPicked(modelData.type)
                                gallery.close()
                            }
                        }

                        // My widgets: share as text, or remove
                        Row {
                            visible: !!modelData.preset
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: gallery.dp(4)
                            spacing: gallery.dp(4)
                            Chip {
                                objectName: "palettePresetCopy"
                                text: "Copy"
                                implicitHeight: gallery.dp(36)
                                onClicked: {
                                    if (dashboardManager.copyWidgetPresetToClipboard(modelData.preset.id))
                                        gallery.note = "Copied \"" + modelData.preset.label + "\". Paste it anywhere to share."
                                }
                            }
                            Chip {
                                objectName: "palettePresetDelete"
                                text: "×"
                                implicitHeight: gallery.dp(36)
                                implicitWidth: gallery.dp(36)
                                onClicked: dashboardManager.deleteWidgetPreset(modelData.preset.id)
                            }
                        }
                    }
                }

                Text {
                    visible: gallery.shown.length === 0
                    Layout.columnSpan: paletteGrid.columns
                    text: "No widget matches \"" + gallery.query + "\""
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText
                }
            }
        }
    }
}
