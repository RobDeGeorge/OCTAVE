import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Slider {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: control
    Layout.fillWidth: true

    implicitHeight: App.Spacing.overallSliderHeight * 2.5

    property color activeColor: App.Style.settingsSliderColor
    property double visualValue: value
    property string valueDisplay: ""

    handle: Item {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: App.Spacing.overallSliderWidth
        height: App.Spacing.overallSliderHeight

        // Glow rectangle behind handle (spacecraft only)
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 6
            height: parent.height + 6
            radius: App.EnvironmentTheme.active.sliderHandleRadius === -1
                ? (width / 2) : dpMin(App.EnvironmentTheme.active.sliderHandleRadius + 3, 2)
            color: Qt.rgba(control.activeColor.r, control.activeColor.g, control.activeColor.b, 0.25)
            visible: App.EnvironmentTheme.active.sliderHandleGlow
        }

        // Actual handle
        Rectangle {
            anchors.fill: parent
            radius: App.EnvironmentTheme.active.sliderHandleRadius === -1
                ? App.Spacing.overallSliderRadius : dpMin(App.EnvironmentTheme.active.sliderHandleRadius, 2)
            color: control.pressed ? Qt.darker(control.activeColor, 1.1) : control.activeColor

            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: App.Spacing.overallSliderHeight / 2

        // Track background
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: App.Style.secondaryTextColor

            // Active track fill
            Rectangle {
                // Ends at the handle's center so the rounded end stays under it
                width: control.visualPosition * (parent.width - control.handle.width) + control.handle.width / 2
                height: parent.height
                color: control.activeColor
                radius: parent.radius
            }
        }

        // Tick marks below the track (spacecraft only)
        Row {
            anchors.top: parent.bottom
            anchors.topMargin: dp(4)
            anchors.left: parent.left
            anchors.right: parent.right
            visible: App.EnvironmentTheme.active.sliderTickMarks

            Repeater {
                model: 11
                Item {
                    width: parent.width / 10
                    height: dp(6)
                    Rectangle {
                        anchors.horizontalCenter: parent.left
                        width: 1
                        height: index % 5 === 0 ? dp(6) : dp(3)
                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
                    }
                }
            }
        }
    }

    // Block track clicks — only allow dragging the handle.
    // Sits as a direct child of the Slider so it intercepts before the
    // Slider's own input handling. Passes through clicks on the handle area.
    MouseArea {
        anchors.fill: parent
        z: 10
        onPressed: function(mouse) {
            var hx = control.leftPadding + control.visualPosition * (control.availableWidth - control.handle.width)
            var hy = control.topPadding + control.availableHeight / 2 - control.handle.height / 2
            if (mouse.x >= hx && mouse.x <= hx + control.handle.width &&
                mouse.y >= hy && mouse.y <= hy + control.handle.height) {
                mouse.accepted = false  // On handle — let Slider process it
            } else {
                mouse.accepted = true   // On track — absorb it
            }
        }
    }
}
