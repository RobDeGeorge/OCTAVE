import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    id: tile

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property string cardId: ""
    property string title: ""
    // Unicode glyph fallback. Kept so a tile that has no SVG yet still renders
    // something rather than a blank square.
    property string icon: ""
    // Preferred: a theme-tinted SVG from frontend/assets/, e.g.
    // App.Style.assetBase + "tile_layout.svg".
    property url iconSource: ""
    property color statusColor: "transparent"
    property bool statusVisible: false

    readonly property bool _hasSvg: String(iconSource) !== ""

    signal tileClicked(string cardId)

    color: tileMouse.pressed
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
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

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - tile.dp(24))
        spacing: tile.dp(12)

        Item {
            id: iconHolder
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(tile.dp(24),
                Math.min(tile.dp(64), tile.height - tileLabel.implicitHeight - tile.dp(40)))

            // Keep the icon balanced on wide and tall tiles alike. Rounded
            // SVG strokes stay clean without an offset shadow doubling them.
            App.ThemedIcon {
                anchors.centerIn: parent
                visible: tile._hasSvg
                width: Math.min(parent.width * 0.6, parent.height)
                height: width
                source: tile.iconSource
                color: App.Style.accent
                shadow: false
            }

            // Static drop-shadow glyph behind the main icon — gives the
            // symbol weight without any animation or graphical effect.
            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: tile.dp(1)
                anchors.verticalCenterOffset: tile.dp(2)
                visible: !tile._hasSvg
                text: tile.icon
                color: Qt.rgba(0, 0, 0, 0.40)
                font.pixelSize: Math.max(tile.dp(20), parent.height * 0.55)
                font.family: App.Style.fontFamily
            }

            Text {
                anchors.centerIn: parent
                visible: !tile._hasSvg
                text: tile.icon
                color: App.Style.accent
                font.pixelSize: Math.max(tile.dp(20), parent.height * 0.55)
                font.family: App.Style.fontFamily
            }
        }

        Text {
            id: tileLabel
            Layout.fillWidth: true
            text: tile.title
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText * 1.1
            font.family: App.Style.fontFamily
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
            font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
        }
    }

    // Status dot
    Rectangle {
        visible: tile.statusVisible
        width: tile.dp(10)
        height: tile.dp(10)
        radius: width / 2
        color: tile.statusColor
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: tile.dp(8)
        anchors.rightMargin: tile.dp(8)
    }

    MouseArea {
        id: tileMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.tileClicked(tile.cardId)
    }
}
