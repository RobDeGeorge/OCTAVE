import QtQuick 2.15
import QtQuick.Controls 2.15
import ".." as App

// The shift light editor's slider: a thin track filled up to a round white
// handle, in a colour of the caller's (a flag's own colour, mostly).
Slider {
    id: control

    // The filled part of the track, and the handle's ring
    property color fillColor: App.Style.accent
    property color ringColor: fillColor

    implicitHeight: App.Spacing.overallSliderHeight * 2.5

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: App.Spacing.overallSliderHeight * 0.5
        radius: height / 2
        color: Qt.darker(App.Style.contentColor, 1.2)

        Rectangle {
            // Ends at the handle's center so the rounded end stays under it
            width: control.visualPosition * (parent.width - control.handle.width) + control.handle.width / 2
            height: parent.height
            color: control.fillColor
            radius: height / 2
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: App.Spacing.overallSliderHeight * 1.5
        height: width
        radius: width / 2
        color: control.pressed ? Qt.darker("white", 1.1) : "white"
        border.color: control.ringColor
        border.width: 2
    }
}
