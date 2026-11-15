import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: control

    property string text: ""
    property string tooltipText: ""
    property color buttonColor: App.Style.accent
    property bool bold: true
    signal clicked()

    // No height of its own (a layout would then ignore every caller's
    // `height:`): each use sets `height` or `Layout.preferredHeight`.
    Layout.preferredWidth: buttonLabel.implicitWidth + App.Spacing.overallSpacing * 1.5
    Layout.minimumWidth: dp(70)

    // Spacecraft: semi-transparent fill with glow border; Standard: solid fill
    color: App.EnvironmentTheme.active.buttonSolidFill
        ? (buttonArea.pressed ? Qt.darker(control.buttonColor, 1.4) :
           buttonArea.containsMouse ? Qt.darker(control.buttonColor, 1.2) : control.buttonColor)
        : (buttonArea.pressed ? Qt.rgba(control.buttonColor.r, control.buttonColor.g, control.buttonColor.b, 0.35) :
           buttonArea.containsMouse ? Qt.rgba(control.buttonColor.r, control.buttonColor.g, control.buttonColor.b, 0.25) :
           Qt.rgba(control.buttonColor.r, control.buttonColor.g, control.buttonColor.b, 0.15))

    radius: dpMin(App.EnvironmentTheme.active.buttonRadius, 2)

    border.width: App.EnvironmentTheme.active.buttonGlowBorder ? 1 : 1
    border.color: App.EnvironmentTheme.active.buttonGlowBorder
        ? (buttonArea.containsMouse
            ? Qt.rgba(control.buttonColor.r, control.buttonColor.g, control.buttonColor.b, 0.9)
            : Qt.rgba(control.buttonColor.r, control.buttonColor.g, control.buttonColor.b, 0.5))
        : Qt.darker(control.buttonColor, 1.3)
    clip: true

    // Top-edge highlight line (spacecraft)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        height: 1
        color: Qt.rgba(1, 1, 1, 0.15)
        visible: App.EnvironmentTheme.active.buttonGlowBorder
    }

    Behavior on border.color { ColorAnimation { duration: 150 } }
    Behavior on color { ColorAnimation { duration: 100 } }

    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: control.text
        color: "white"
        font.pixelSize: App.Spacing.overallText
        font.bold: control.bold
        font.family: App.Style.fontFamily
    }

    MouseArea {
        id: buttonArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: control.clicked()
    }

    ToolTip.visible: buttonArea.containsMouse && control.tooltipText !== ""
    ToolTip.text: control.tooltipText
    ToolTip.delay: 300
}
