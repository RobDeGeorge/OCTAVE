// PalettePopup.qml
//
// Widget palette for the dashboard editor (Phase 3 Milestone B). Opened by
// tapping an empty grid cell; shows every primitive from the WidgetCatalog
// singleton. Tapping a card emits widgetPicked(type) and closes — the editor
// places the widget into the cell that was tapped.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../.." as App

Popup {
    id: palette

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    signal widgetPicked(string type)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width * 0.85, dp(640))
    modal: true
    focus: true
    padding: dp(20)

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: palette.dpMin(12, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: palette.dp(16)

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Add a widget"
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 1.2
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            rowSpacing: palette.dp(12)
            columnSpacing: palette.dp(12)

            Repeater {
                model: App.WidgetCatalog.widgets
                delegate: Rectangle {
                    objectName: "paletteCard_" + modelData.type
                    Layout.fillWidth: true
                    Layout.preferredHeight: palette.dp(110)
                    radius: palette.dpMin(8, 2)
                    color: cardMouse.pressed
                           ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                           : Qt.darker(App.Style.obdBoxBackground, 1.12)
                    border.color: cardMouse.containsMouse
                                  ? App.Style.accent
                                  : Qt.darker(App.Style.obdBarColor, 1.6)
                    border.width: 1

                    Column {
                        anchors.centerIn: parent
                        spacing: palette.dp(8)

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.glyph
                            color: App.Style.accent
                            font.pixelSize: palette.dp(34)
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.displayName
                            color: App.Style.obdValueColor
                            font.family: App.Style.fontFamily
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.bold: true
                        }
                    }

                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            palette.widgetPicked(modelData.type)
                            palette.close()
                        }
                    }
                }
            }
        }
    }
}
