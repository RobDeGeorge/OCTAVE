import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

// The settings of one shift light flag (an RPM band with a colour and what
// it does while the engine is inside it). Shows `flag` and asks for changes
// with edited(); it holds no state of its own.
Column {
    id: panel

    // The flag shown: { rpmLow, rpmHigh, color, flash, flashSpeed,
    // fullScreenFlash, fullScreenFlashOpacity, auraFlash, auraFlashOpacity,
    // auraSpread }, or null when none is selected
    property var flag: null
    // Its place in the list, counted from 1, for the heading
    property int flagNumber: 0
    // The redline: the upper end of both RPM sliders
    property int maxRpm: 8000
    property string fontFamily: App.Style.fontFamily

    // One property of the flag should take a new value
    signal edited(string name, var value)
    signal deleteRequested()

    readonly property color flagColor: flag ? flag.color : App.Style.accent
    readonly property real overlayOpacity: flag && flag.fullScreenFlashOpacity !== undefined ? flag.fullScreenFlashOpacity : 0.5
    readonly property real auraOpacity: flag && flag.auraFlashOpacity !== undefined ? flag.auraFlashOpacity : 0.6
    readonly property real auraSpread: flag && flag.auraSpread !== undefined ? flag.auraSpread : 0.18

    // The flag's colour at the opacity a slider is choosing
    function faded(alpha) {
        var c = Qt.lighter(panel.flagColor, 1)
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }

    spacing: App.Spacing.overallSpacing

    RowLayout {
        width: parent.width

        Text {
            text: "Flag " + panel.flagNumber + " Settings"
            font.pixelSize: App.Spacing.overallText * 1.6
            font.bold: true
            font.family: panel.fontFamily
            color: panel.flagColor
            leftPadding: App.Spacing.overallMargin
            Layout.fillWidth: true
        }

        // Delete flag button
        Rectangle {
            Layout.preferredWidth: App.Spacing.overallText * 8
            Layout.preferredHeight: App.Spacing.overallText * 2.5
            Layout.rightMargin: App.Spacing.overallMargin
            radius: App.Spacing.overallMargin * 0.5
            color: deleteFlagMouse.pressed ? Qt.darker("#FF4444", 1.2) :
                   deleteFlagMouse.containsMouse ? Qt.lighter("#FF4444", 1.1) : "#FF4444"

            Behavior on color { ColorAnimation { duration: 150 } }

            Text {
                anchors.centerIn: parent
                text: "Delete"
                font.pixelSize: App.Spacing.overallText * 1.1
                font.bold: true
                font.family: panel.fontFamily
                color: "white"
            }

            MouseArea {
                id: deleteFlagMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: panel.deleteRequested()
            }
        }
    }

    ShiftLabeledSlider {
        width: parent.width
        fontFamily: panel.fontFamily
        title: "RPM Low (Start)"
        valueText: panel.flag ? panel.flag.rpmLow + " RPM" : ""
        valueColor: panel.flag ? panel.flag.color : App.Style.primaryTextColor
        from: 0
        to: panel.maxRpm
        stepSize: 100
        value: panel.flag ? panel.flag.rpmLow : 0
        fillColor: panel.flagColor
        onMoved: function(value) { panel.edited("rpmLow", value) }
    }

    ShiftLabeledSlider {
        width: parent.width
        fontFamily: panel.fontFamily
        title: "RPM High (End)"
        valueText: panel.flag ? panel.flag.rpmHigh + " RPM" : ""
        valueColor: panel.flag ? panel.flag.color : App.Style.primaryTextColor
        from: 0
        to: panel.maxRpm
        stepSize: 100
        value: panel.flag ? panel.flag.rpmHigh : panel.maxRpm
        fillColor: panel.flagColor
        onMoved: function(value) { panel.edited("rpmHigh", value) }
    }

    // Color picker
    Column {
        width: parent.width
        spacing: App.Spacing.overallSpacing * 0.5

        Text {
            text: "Color"
            font.pixelSize: App.Spacing.overallText * 1.1
            font.family: panel.fontFamily
            color: App.Style.primaryTextColor
            leftPadding: App.Spacing.overallMargin
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: App.Spacing.overallSpacing

            Repeater {
                model: ["#00FF00", "#FFFF00", "#FF8800", "#FF0000", "#FF00FF", "#00FFFF", "#FFFFFF", "#0088FF"]

                Rectangle {
                    id: swatch
                    required property string modelData
                    readonly property bool chosen: !!panel.flag && panel.flag.color === modelData
                    width: App.Spacing.overallText * 3
                    height: width
                    radius: width / 2
                    color: modelData
                    border.color: chosen ? "white" : Qt.darker(modelData, 1.3)
                    border.width: chosen ? 3 : 2

                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.edited("color", swatch.modelData)
                    }
                }
            }
        }
    }

    ShiftToggleRow {
        width: parent.width
        fontFamily: panel.fontFamily
        text: "Flash when triggered"
        checked: !!panel.flag && !!panel.flag.flash
        onToggleRequested: panel.edited("flash", !checked)
    }

    // Only while flash is on
    ShiftLabeledSlider {
        width: parent.width
        visible: !!panel.flag && !!panel.flag.flash
        fontFamily: panel.fontFamily
        title: "Flash Speed"
        valueText: (panel.flag ? panel.flag.flashSpeed : 100) + " ms"
        from: 50
        to: 500
        stepSize: 25
        value: panel.flag ? panel.flag.flashSpeed : 100
        fillColor: App.Style.accent
        lowCaption: "Fast"
        highCaption: "Slow"
        onMoved: function(value) { panel.edited("flashSpeed", value) }
    }

    ShiftToggleRow {
        width: parent.width
        fontFamily: panel.fontFamily
        text: "Full screen overlay"
        checked: !!panel.flag && !!panel.flag.fullScreenFlash
        onToggleRequested: panel.edited("fullScreenFlash", !checked)
    }

    // Only while the full screen overlay is on; the fill previews the opacity
    ShiftLabeledSlider {
        width: parent.width
        visible: !!panel.flag && !!panel.flag.fullScreenFlash
        fontFamily: panel.fontFamily
        title: "Overlay Opacity"
        valueText: Math.round(panel.overlayOpacity * 100) + "%"
        from: 0.1
        to: 1.0
        stepSize: 0.05
        value: panel.overlayOpacity
        fillColor: panel.faded(panel.overlayOpacity)
        ringColor: panel.flagColor
        onMoved: function(value) { panel.edited("fullScreenFlashOpacity", value) }
    }

    // Perimeter-only fade
    ShiftToggleRow {
        width: parent.width
        fontFamily: panel.fontFamily
        text: "Aura overlay"
        checked: !!panel.flag && !!panel.flag.auraFlash
        onToggleRequested: panel.edited("auraFlash", !checked)
    }

    // Only while the aura overlay is on
    ShiftLabeledSlider {
        width: parent.width
        visible: !!panel.flag && !!panel.flag.auraFlash
        fontFamily: panel.fontFamily
        title: "Aura Opacity"
        valueText: Math.round(panel.auraOpacity * 100) + "%"
        from: 0.1
        to: 1.0
        stepSize: 0.05
        value: panel.auraOpacity
        fillColor: panel.faded(panel.auraOpacity)
        ringColor: panel.flagColor
        onMoved: function(value) { panel.edited("auraFlashOpacity", value) }
    }

    ShiftLabeledSlider {
        width: parent.width
        visible: !!panel.flag && !!panel.flag.auraFlash
        fontFamily: panel.fontFamily
        title: "Aura Spread"
        valueText: Math.round(panel.auraSpread * 100) + "%"
        from: 0.05
        to: 0.5
        stepSize: 0.01
        value: panel.auraSpread
        fillColor: panel.flagColor
        onMoved: function(value) { panel.edited("auraSpread", value) }
    }
}
