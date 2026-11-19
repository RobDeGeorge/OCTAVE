import QtQuick 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import ".." as App

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: control
    height: compact ? implicitHeight : dp(46)
    Layout.preferredHeight: compact ? implicitHeight : dp(46)
    // Compact toggles hug their indicator so they right-align cleanly inside
    // SettingCategory's inline slot. Full toggles still fill to host the wide label.
    Layout.fillWidth: !compact
    implicitHeight: compact ? compactSwitch.implicitHeight : dp(46)
    implicitWidth: compact ? compactSwitch.implicitWidth : 200

    property bool checked: false
    property bool compact: false
    property string text: ""
    property color activeColor: App.Style.accent
    property color inactiveColor: App.Style.hoverColor
    signal toggled(bool checked)

    // A tap asks for the other state and lets the caller's `checked` binding
    // follow its source. Assigning `checked` here (as this used to) cut that
    // binding on the first tap, and every page then needed a Connections
    // block to copy the setting back. Only a caller whose `checked` did not
    // follow (no binding, or one on a value that does not notify) has the
    // state set here.
    function flip() {
        var want = !control.checked
        control.toggled(want)
        if (control.checked !== want)
            control.checked = want
    }

    // ─── Compact mode (inline switch) ───
    Item {
        id: compactSwitch
        anchors.fill: parent
        visible: control.compact
        implicitWidth: compactIndicator.width + compactLabel.implicitWidth + App.Spacing.overallSpacing * 0.5
        implicitHeight: dp(36)

        Item {
            id: compactIndicator
            width: dp(68)
            height: dp(36)
            anchors.verticalCenter: parent.verticalCenter

            // Glow behind track (spacecraft, when checked)
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 6
                height: parent.height + 6
                radius: dpMin(App.EnvironmentTheme.active.switchRadius + 3, 2)
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
                visible: App.EnvironmentTheme.active.accentBorder && control.checked
            }

            Rectangle {
                anchors.fill: parent
                radius: dpMin(App.EnvironmentTheme.active.switchRadius, 2)
                color: control.checked ? App.Style.accent : App.Style.secondaryTextColor
                border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
                border.color: control.checked
                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                    : Qt.rgba(App.Style.secondaryTextColor.r, App.Style.secondaryTextColor.g, App.Style.secondaryTextColor.b, 0.5)

                Behavior on color { ColorAnimation { duration: 150 } }

                Rectangle {
                    x: control.checked ? parent.width - width - dp(4) : dp(4)
                    width: dp(28)
                    height: dp(28)
                    radius: dpMin(App.EnvironmentTheme.active.switchKnobRadius, 2)
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"

                    Behavior on x { NumberAnimation { duration: 150 } }
                }
            }
        }

        Text {
            id: compactLabel
            anchors.left: compactIndicator.right
            anchors.leftMargin: App.Spacing.overallSpacing * 0.6
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 1.3
            font.bold: true
        }

        MouseArea {
            // The switch is drawn dp(36) tall; the touch target is never
            // under dp(44), reaching past it above and below
            anchors.fill: parent
            anchors.topMargin: -Math.max(0, (dp(44) - parent.height) / 2)
            anchors.bottomMargin: anchors.topMargin
            onClicked: control.flip()
        }
    }

    // ─── Full mode (wide toggle with label) ───
    RowLayout {
        anchors.fill: parent
        spacing: App.Spacing.overallSpacing * 2
        visible: !control.compact

        Text {
            text: control.text
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText * 1.1
            font.family: App.Style.fontFamily
            Layout.preferredWidth: control.width * 0.55

            MouseArea {
                anchors.fill: parent
                onClicked: control.flip()
            }
        }

        Item {
            width: dp(72)
            height: dp(36)

            // Glow behind toggle (spacecraft, when checked)
            Rectangle {
                anchors.centerIn: track
                width: track.width + 6
                height: track.height + 6
                radius: dpMin((App.EnvironmentTheme.active.toggleTrackRadius === -1
                    ? track.height / 2 : App.EnvironmentTheme.active.toggleTrackRadius) + 3, 2)
                color: Qt.rgba(control.activeColor.r, control.activeColor.g, control.activeColor.b, 0.2)
                visible: App.EnvironmentTheme.active.toggleRectShadow && control.checked
            }

            Rectangle {
                id: track
                anchors.fill: parent
                radius: App.EnvironmentTheme.active.toggleTrackRadius === -1
                    ? height / 2 : dpMin(App.EnvironmentTheme.active.toggleTrackRadius, 2)
                color: control.checked ? Qt.rgba(control.activeColor.r, control.activeColor.g, control.activeColor.b, 0.3) :
                                    Qt.rgba(control.inactiveColor.r, control.inactiveColor.g, control.inactiveColor.b, 0.3)

                // Accent border (spacecraft)
                border.width: App.EnvironmentTheme.active.toggleRectShadow ? 1 : 0
                border.color: control.checked
                    ? Qt.rgba(control.activeColor.r, control.activeColor.g, control.activeColor.b, 0.6)
                    : Qt.rgba(control.inactiveColor.r, control.inactiveColor.g, control.inactiveColor.b, 0.3)

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                    }
                }

                Rectangle {
                    id: highlightTrack
                    width: control.checked ? parent.width : 0
                    height: parent.height
                    radius: parent.radius
                    anchors.right: control.checked ? parent.right : undefined
                    anchors.left: !control.checked ? parent.left : undefined
                    color: control.activeColor
                    opacity: control.checked ? 0.5 : 0

                    Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                    Behavior on opacity { NumberAnimation { duration: 300 } }
                }
            }

            // Rectangle shadow behind handle (spacecraft) — replaces DropShadow
            Rectangle {
                visible: App.EnvironmentTheme.active.toggleRectShadow
                x: handle.x + 2
                y: handle.y + 3
                width: handle.width
                height: handle.height
                radius: handle.radius
                color: Qt.rgba(0, 0, 0, 0.2)
            }

            Rectangle {
                id: handle
                width: dp(36)
                height: dp(36)
                radius: App.EnvironmentTheme.active.toggleHandleRadius === -1
                    ? width / 2 : dpMin(App.EnvironmentTheme.active.toggleHandleRadius, 2)
                x: control.checked ? parent.width - width : 0
                y: 0
                color: "white"

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.4
                    height: width
                    radius: App.EnvironmentTheme.active.toggleHandleRadius === -1
                        ? width / 2 : dpMin(App.EnvironmentTheme.active.toggleHandleRadius, 2)
                    color: control.activeColor
                    opacity: control.checked ? 1 : 0
                    scale: control.checked ? 1 : 0.5

                    Behavior on opacity { NumberAnimation { duration: 200 } }
                    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                }

                // Standard DropShadow (hidden in spacecraft mode)
                layer.enabled: !App.EnvironmentTheme.active.toggleRectShadow
                layer.effect: DropShadow {
                    verticalOffset: 2
                    radius: 6.0
                    samples: 17
                    color: Qt.rgba(0, 0, 0, 0.2)
                }

                scale: controlMouseArea.pressed ? 0.95 : 1.0

                Behavior on x {
                    NumberAnimation {
                        duration: 300
                        easing.type: Easing.OutBack
                        easing.overshoot: 0.6
                    }
                }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }

            MouseArea {
                id: controlMouseArea
                anchors.fill: parent
                onClicked: control.flip()

                onPressed: {
                    pulseAnimation.start()
                }

                SequentialAnimation {
                    id: pulseAnimation
                    PropertyAnimation {
                        target: handle
                        property: "scale"
                        to: 0.9
                        duration: 100
                    }
                    PropertyAnimation {
                        target: handle
                        property: "scale"
                        to: 1.0
                        duration: 100
                    }
                }
            }
        }
    }
}
