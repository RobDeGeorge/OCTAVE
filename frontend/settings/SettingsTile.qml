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

    // Top-edge bevel highlight — static, fakes a lit upper rim.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: dp(App.EnvironmentTheme.active.cardRadius)
        anchors.rightMargin: dp(App.EnvironmentTheme.active.cardRadius)
        height: 1
        color: Qt.rgba(1, 1, 1, 0.10)
    }

    // Bottom-edge inner shadow — static, pairs with the top highlight to
    // give the card a beveled / raised look without any per-frame cost.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: dp(App.EnvironmentTheme.active.cardRadius)
        anchors.rightMargin: dp(App.EnvironmentTheme.active.cardRadius)
        height: 1
        color: Qt.rgba(0, 0, 0, 0.20)
    }

    App.CornerBrackets {
        bracketLength: dp(10)
        visible: App.EnvironmentTheme.active.cornerBrackets
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: tile.dp(10)
        spacing: tile.dp(6)

        Item {
            id: iconHolder
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: tile.dp(28)

            // Preferred path: theme-tinted SVG. ThemedIcon carries the same
            // offset drop shadow the glyphs below fake, so the two render
            // paths have matching weight.
            App.ThemedIcon {
                anchors.centerIn: parent
                visible: tile._hasSvg
                width: Math.max(tile.dp(24), parent.height * 0.62)
                height: width
                source: tile.iconSource
                color: App.Style.accent
                shadowOffsetX: tile.dp(1)
                shadowOffsetY: tile.dp(2)
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
            Layout.fillWidth: true
            text: tile.title
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText * 1.6
            font.family: App.Style.fontFamily
            font.bold: true
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
