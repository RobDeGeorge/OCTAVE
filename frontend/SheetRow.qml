import QtQuick 2.15
import "." as App

// One choice in a ConnectSheet: an outlined accent row with a bold title, a
// line of detail under it and, on the right, an optional word ("Working…")
// and signal bars. Fills the width of the column it sits in.
Rectangle {
    id: sheetRow

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property string title: ""
    property string detail: ""
    property string trailing: ""
    // 0-4 signal bars on the right, -1 for none
    property int bars: -1
    property bool active: true
    // Outlined more strongly (a pairing under way on this row)
    property bool highlighted: false
    signal picked()

    width: parent ? parent.width : 0
    height: Math.max(App.Spacing.bottomBarNavButtonHeight * 1.3, rowText.implicitHeight + dp(20))
    radius: dpMin(8, 2)
    color: rowMouse.pressed ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
                            : highlighted ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.08)
                            : "transparent"
    border.color: App.Style.accent
    border.width: highlighted ? 2 : 1
    opacity: active ? 1.0 : 0.45
    scale: rowMouse.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
    }

    Column {
        id: rowText
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: rightSide.left
        anchors.leftMargin: sheetRow.dp(16)
        anchors.rightMargin: sheetRow.dp(8)
        spacing: sheetRow.dp(2)
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: sheetRow.title
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText
            font.bold: true
            font.family: App.Style.fontFamily
        }
        Text {
            width: parent.width
            visible: sheetRow.detail !== ""
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: sheetRow.detail
            color: App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.overallText * 0.8
            font.family: App.Style.fontFamily
        }
    }

    Row {
        id: rightSide
        anchors.right: parent.right
        anchors.rightMargin: sheetRow.dp(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: sheetRow.dp(10)
        Text {
            visible: sheetRow.trailing !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: sheetRow.trailing
            color: App.Style.accent
            font.pixelSize: App.Spacing.overallText * 0.8
            font.bold: true
            font.family: App.Style.fontFamily
        }
        Row {
            id: barsRow
            visible: sheetRow.bars >= 0
            anchors.verticalCenter: parent.verticalCenter
            spacing: sheetRow.dp(2)
            Repeater {
                model: 4
                Rectangle {
                    width: sheetRow.dp(4)
                    height: sheetRow.dp(6 + index * 4)
                    anchors.bottom: parent.bottom
                    radius: 1
                    color: index < sheetRow.bars ? App.Style.accent
                                            : Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g,
                                                      App.Style.secondaryTextColor.b, 0.3)
                }
            }
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        enabled: sheetRow.active
        cursorShape: Qt.PointingHandCursor
        onClicked: sheetRow.picked()
    }
}
