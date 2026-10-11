import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

// Vertical mini-slider tile used in the Now Playing left/right rails.
Item {
    id: root
    function dp(s) { return Math.round(s * (App.Spacing.effectiveScale || 1.0)) }

    property string label: ""
    property string suffix: ""
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value: 0
    property bool dimmed: false
    signal moved(real value)

    // Width is fixed (rail dictates it); height grows to fill available rail
    // space so 2 sliders in a tall rail use it all instead of clumping at top.
    Layout.preferredWidth: root.dp(70)
    Layout.preferredHeight: root.dp(180)
    Layout.minimumHeight: root.dp(140)
    Layout.fillHeight: true
    width: Layout.preferredWidth

    opacity: dimmed ? 0.4 : 1.0
    Behavior on opacity { NumberAnimation { duration: 150 } }

    Text {
        id: labelText
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: App.Style.primaryTextColor
        font.pixelSize: App.Spacing.overallText - 1
        font.bold: true
        font.family: App.Style.fontFamily
    }

    Text {
        id: valueText
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: {
            var v = root.stepSize >= 1
                ? slider.value.toFixed(0)
                : Math.round(slider.value * 100).toString()
            return v + root.suffix
        }
        color: App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText - 2
        font.family: App.Style.fontFamily
    }

    Slider {
        id: slider
        orientation: Qt.Vertical
        anchors.top: labelText.bottom
        anchors.bottom: valueText.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: root.dp(6)
        anchors.bottomMargin: root.dp(6)
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        enabled: !root.dimmed

        background: Rectangle {
            x: slider.leftPadding + slider.availableWidth / 2 - width / 2
            y: slider.topPadding
            width: root.dp(8)
            height: slider.availableHeight
            radius: width / 2
            color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.15)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                // Ends at the handle's center so the rounded end stays under it
                height: (1 - slider.visualPosition) * (parent.height - slider.handle.height) + slider.handle.height / 2
                radius: width / 2
                color: App.Style.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.availableWidth / 2 - width / 2
            y: slider.topPadding + slider.visualPosition * (slider.availableHeight - height)
            width: root.dp(20)
            height: root.dp(20)
            radius: width / 2
            color: App.Style.accent
            border.width: 2
            border.color: "white"
        }
        onMoved: root.moved(slider.value)
    }

    onValueChanged: if (Math.abs(slider.value - root.value) > 0.001) slider.value = root.value
}
