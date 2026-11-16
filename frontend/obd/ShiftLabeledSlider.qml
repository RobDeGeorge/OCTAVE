import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

// A title on the left, the value on the right and a ShiftSlider under them,
// with optional captions under the slider's two ends.
Column {
    id: block

    property string title: ""
    property string valueText: ""
    property color valueColor: App.Style.primaryTextColor
    property string fontFamily: App.Style.fontFamily
    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias value: slider.value
    property alias fillColor: slider.fillColor
    property alias ringColor: slider.ringColor
    property string lowCaption: ""
    property string highCaption: ""
    // The handle was dragged or the track tapped; `value` is the new value
    signal moved(real value)

    spacing: App.Spacing.overallSpacing * 0.5

    RowLayout {
        width: parent.width

        Text {
            text: block.title
            font.pixelSize: App.Spacing.overallText * 1.1
            font.family: block.fontFamily
            color: App.Style.primaryTextColor
            Layout.fillWidth: true
            leftPadding: App.Spacing.overallMargin
        }

        Text {
            text: block.valueText
            font.pixelSize: App.Spacing.overallText * 1.1
            font.bold: true
            font.family: block.fontFamily
            color: block.valueColor
            rightPadding: App.Spacing.overallMargin
        }
    }

    ShiftSlider {
        id: slider
        width: parent.width - App.Spacing.overallMargin * 2
        anchors.horizontalCenter: parent.horizontalCenter
        onMoved: block.moved(value)
    }

    RowLayout {
        width: parent.width - App.Spacing.overallMargin * 2
        anchors.horizontalCenter: parent.horizontalCenter
        visible: block.lowCaption !== "" || block.highCaption !== ""

        Text {
            text: block.lowCaption
            font.pixelSize: App.Spacing.overallText * 0.9
            font.family: block.fontFamily
            color: Qt.darker(App.Style.primaryTextColor, 1.3)
        }
        Item { Layout.fillWidth: true }
        Text {
            text: block.highCaption
            font.pixelSize: App.Spacing.overallText * 0.9
            font.family: block.fontFamily
            color: Qt.darker(App.Style.primaryTextColor, 1.3)
        }
    }
}
