import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    id: tile

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property string cardId: ""
    property string title: ""
    // One short line of current state under the title ("Connected",
    // "7:57 AM · 12-hour"). Empty: the tile shows its title alone.
    property string stateText: ""
    // On / connected. Tints the tile with the accent, the way Quick
    // Settings and Home tiles show an active accessory; display options
    // that are not on or off leave this false.
    property bool active: false
    property color statusColor: "transparent"
    property bool statusVisible: false

    signal tileClicked(string cardId)

    color: tileMouse.pressed
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, tile.active ? 0.30 : 0.18)
        : tile.active
            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, tileMouse.containsMouse ? 0.24 : 0.16)
        : tileMouse.containsMouse
            ? Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.10)
            : Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.05)
    radius: dpMin(App.EnvironmentTheme.active.cardRadius, 2)

    border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
    border.color: App.EnvironmentTheme.active.accentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                  App.EnvironmentTheme.active.accentBorderOpacity) : "transparent"

    scale: tileMouse.pressed ? 0.97 : 1.0

    Behavior on color { ColorAnimation { duration: 150 } }
    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } }

    App.CornerBrackets {
        bracketLength: dp(10)
        visible: App.EnvironmentTheme.active.cornerBrackets
    }

    readonly property int _pad: dpMin(16, 8)

    // Name first, state second (Android Quick Settings label /
    // secondaryLabel, Apple Home name / status), grouped and vertically
    // centred so a tall tile has no empty middle. Sizes follow the tile
    // width, not the text, so every tile in a grid matches.
    ColumnLayout {
        anchors {
            left: parent.left; right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: tile._pad; rightMargin: tile._pad
        }
        spacing: tile.dp(4)

        Text {
            Layout.fillWidth: true
            text: tile.title
            color: App.Style.primaryTextColor
            // About fourteen characters ("Gesture Sensor") fit.
            font.pixelSize: Math.max(App.Spacing.overallText * 0.8,
                Math.min(App.Spacing.overallText * 1.5, (tile.width - 2 * tile._pad) / 9))
            font.family: App.Style.fontFamily
            font.weight: Font.Medium
            font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
            font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        RowLayout {
            Layout.fillWidth: true
            visible: tile.stateText !== ""
            spacing: tile.dp(8)

            Rectangle {
                visible: tile.statusVisible
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: tile.dpMin(10, 6)
                Layout.preferredHeight: Layout.preferredWidth
                radius: width / 2
                color: tile.statusColor
            }

            Text {
                Layout.fillWidth: true
                text: tile.stateText
                color: tile.active ? App.Style.primaryTextColor : App.Style.secondaryTextColor
                font.pixelSize: Math.max(App.Spacing.overallText * 0.85,
                    Math.min(App.Spacing.overallText * 1.05, (tile.width - 2 * tile._pad) / 11))
                font.family: App.Style.fontFamily
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    MouseArea {
        id: tileMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.tileClicked(tile.cardId)
    }
}
