import QtQuick 2.15
import QtQuick.Controls 2.15
import "." as App

// The look every "connect something" popup shares (Connect Music in
// MusicEmptyPrompt.qml, Connect Vehicle in OBDAdapterSheet.qml): a card in
// the middle of the window with a centred title and a column of SheetRows
// under it. Tap outside or Esc closes it.
//
//   App.ConnectSheet {
//       title: "Connect Music"
//       App.SheetRow { title: "Local Music"; detail: "..."; onPicked: ... }
//   }
Popup {
    id: sheet

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property string title: ""
    // A spinner beside the title (a scan under way)
    property bool busy: false
    // What goes under the title, in a column as wide as the sheet
    default property alias content: body.data
    readonly property real bodyWidth: body.width

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width * 0.9, dp(440))
    height: Math.min(implicitHeight, parent.height * 0.92)
    modal: true
    focus: true
    padding: dp(20)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.97)
        radius: App.Spacing.overallMargin
        border.color: App.Style.accent
        border.width: 2
    }

    contentItem: Column {
        spacing: sheet.dp(12)
        Item {
            width: parent.width
            height: titleText.implicitHeight
            Text {
                id: titleText
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: sheet.title
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                font.family: App.Style.fontFamily
            }
            BusyIndicator {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: sheet.dp(28)
                height: sheet.dp(28)
                running: sheet.busy
                visible: running
            }
        }
        Column {
            id: body
            width: parent.width
            spacing: sheet.dp(12)
        }
    }
}
