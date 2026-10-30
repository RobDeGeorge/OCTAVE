// TemplateGallery.qml
//
// "+ New" in the dashboard chooser: the starting points from
// dashboardManager.templates (frontend/dashboards/templates/*.json), each a
// live miniature of the dashboard it makes. The slots are filled from the
// PIDs the connected car reports (dashboardManager.fromTemplate), or with
// common ones when no car has been scanned, so what the card shows is what
// the editor opens with. The miniatures run on the editor's demo data while
// the gallery is open (the modal popup covers the real dashboard behind it).
//
// templatePicked(spec) hands the filled spec to the caller, which opens the
// editor on it.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as Dashboards
import "../.." as App

Popup {
    id: gallery
    objectName: "templateGallery"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    // The page the dashboards render on, so the miniatures are laid out at
    // full size and scaled down (as the chooser's are)
    property real stageWidth: parent ? parent.width : 1280
    property real stageHeight: parent ? parent.height : 720

    signal templatePicked(var spec)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - dp(24), dp(1100))
    height: parent.height - dp(24)
    modal: true
    focus: true
    padding: dp(18)

    readonly property bool carKnown: App.OBDParameterModel.vehicleKnown
    readonly property var _supported: carKnown ? App.OBDParameterModel.vehicleSupportedParameters : []
    readonly property var templates: (typeof dashboardManager !== "undefined" && dashboardManager)
                                     ? dashboardManager.templates : []

    // The filled spec of every template, worked out when the gallery opens
    property var _specs: ({})
    property bool _demoWas: false
    // True from aboutToShow until a moment after closing: the miniatures
    // render only then. Not Popup.visible, which changes more than once
    // while opening (each change rebuilt every miniature); released late so
    // a quick pick doesn't tear down widgets still loading.
    property bool _showing: false
    Timer { id: releaseTimer; interval: 1500; onTriggered: gallery._showing = false }

    onAboutToShow: {
        var specs = {}
        for (var i = 0; i < templates.length; i++)
            specs[templates[i].id] = dashboardManager.fromTemplate(templates[i].id, _supported)
        _specs = specs
        _demoWas = App.OBDParameterModel.simulationActive
        App.OBDParameterModel.simulationActive = true
        releaseTimer.stop()
        _showing = true
    }
    onClosed: {
        releaseTimer.restart()
        App.OBDParameterModel.simulationActive = _demoWas
    }

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: gallery.dpMin(14, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: gallery.dp(12)

        RowLayout {
            Layout.fillWidth: true
            spacing: gallery.dp(12)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: gallery.dp(2)
                Text {
                    text: "Start a new dashboard"
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 1.25
                    font.bold: true
                }
                Text {
                    objectName: "templateGalleryNote"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: gallery.carKnown
                          ? "Filled in with what your car reports. You can change any of it."
                          : "No car connected: filled in with common values. You can change any of it."
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.85
                }
            }
            Rectangle {
                objectName: "templateGalleryClose"
                Layout.preferredWidth: Math.max(gallery.dp(44), closeLabel.implicitWidth + gallery.dp(24))
                Layout.preferredHeight: gallery.dp(44)
                radius: height / 2
                color: closeMouse.pressed ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                                          : Qt.darker(App.Style.obdBoxBackground, 1.25)
                border.color: Qt.darker(App.Style.obdBarColor, 1.5)
                border.width: 1
                Text {
                    id: closeLabel
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.95
                }
                MouseArea { id: closeMouse; anchors.fill: parent; onClicked: gallery.close() }
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            GridLayout {
                width: scroll.availableWidth
                columns: Math.max(1, Math.min(3, Math.floor(width / gallery.dp(260))))
                rowSpacing: gallery.dp(14)
                columnSpacing: gallery.dp(14)

                Repeater {
                    model: gallery.templates
                    delegate: Rectangle {
                        id: card
                        objectName: "templateCard_" + modelData.id
                        readonly property var spec: gallery._specs[modelData.id] || null
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 0.62 + cardText.implicitHeight + gallery.dp(20)
                        radius: gallery.dpMin(12, 4)
                        color: cardMouse.pressed ? Qt.lighter(App.Style.obdBoxBackground, 1.15)
                                                 : Qt.darker(App.Style.obdBoxBackground, 1.08)
                        border.color: cardMouse.containsMouse ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.5)
                        border.width: 1

                        Rectangle {
                            id: preview
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: gallery.dp(8)
                            height: card.width * 0.62 - gallery.dp(8)
                            radius: gallery.dpMin(8, 2)
                            color: App.Style.backgroundColor
                            clip: true

                            Item {
                                width: Math.max(1, gallery.stageWidth)
                                height: Math.max(1, gallery.stageHeight)
                                anchors.centerIn: parent
                                scale: Math.min(preview.width / width, preview.height / height)
                                // Rendered only while the gallery shows
                                Dashboards.DashboardRenderer {
                                    anchors.fill: parent
                                    spec: gallery._showing ? card.spec : null
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: card.spec !== null && card.spec.cells && card.spec.cells.length === 0
                                text: "+"
                                color: App.Style.obdLabelColor
                                font.pixelSize: preview.height * 0.4
                                opacity: 0.5
                            }
                        }

                        ColumnLayout {
                            id: cardText
                            anchors.top: preview.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: gallery.dp(10)
                            spacing: gallery.dp(2)
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                elide: Text.ElideRight
                                color: App.Style.primaryTextColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText
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
                                font.pixelSize: App.Spacing.overallText * 0.8
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var spec = card.spec
                                if (spec) gallery.templatePicked(spec)
                                gallery.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
