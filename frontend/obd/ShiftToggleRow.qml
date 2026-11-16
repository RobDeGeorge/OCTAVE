import QtQuick 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import ".." as App

// One labelled on / off row of the shift light editor: the whole row, the
// label and the switch all ask for the other state. The row never changes
// `checked` itself; the editor's state does, and `checked` follows it.
Rectangle {
    id: row

    // Local dp wrapper — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property string text: ""
    property bool checked: false
    property string fontFamily: App.Style.fontFamily
    // The switch (or row, or label) was tapped
    signal toggleRequested()

    height: dp(60)
    radius: App.Spacing.overallMargin * 0.5
    color: rowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: row.toggleRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: App.Spacing.overallMargin
        anchors.rightMargin: App.Spacing.overallMargin
        spacing: App.Spacing.overallSpacing * 2

        Text {
            text: row.text
            font.pixelSize: App.Spacing.overallText * 1.1
            font.family: row.fontFamily
            color: App.Style.primaryTextColor
            Layout.fillWidth: true

            MouseArea {
                anchors.fill: parent
                onClicked: row.toggleRequested()
            }
        }

        // Toggle switch
        Item {
            Layout.preferredWidth: row.dp(80)
            Layout.preferredHeight: row.dp(40)

            // Main track
            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: row.checked ?
                    Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                    Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                // Subtle gradient overlay
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                    }
                }

                // Animated highlight
                Rectangle {
                    width: row.checked ? parent.width : 0
                    height: parent.height
                    radius: parent.radius
                    anchors.right: row.checked ? parent.right : undefined
                    anchors.left: !row.checked ? parent.left : undefined
                    color: App.Style.accent
                    opacity: row.checked ? 0.5 : 0

                    Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                    Behavior on opacity { NumberAnimation { duration: 300 } }
                }

                // ON/OFF text
                Text {
                    anchors {
                        left: row.checked ? undefined : parent.left
                        right: row.checked ? parent.right : undefined
                        margins: row.dp(10)
                        verticalCenter: parent.verticalCenter
                    }
                    text: row.checked ? "ON" : "OFF"
                    font.pixelSize: App.Spacing.overallText * 0.8
                    font.bold: true
                    font.family: row.fontFamily
                    color: row.checked ? App.Style.accent :
                        Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                    visible: width < (parent.width - handle.width - row.dp(10))

                    Behavior on color { ColorAnimation { duration: 200 } }
                }
            }

            // Handle
            Rectangle {
                id: handle
                width: row.dp(40)
                height: row.dp(40)
                radius: width / 2
                x: row.checked ? parent.width - width : 0
                y: 0
                color: "white"

                // Inner indicator
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.4
                    height: width
                    radius: width / 2
                    color: App.Style.accent
                    opacity: row.checked ? 1 : 0
                    scale: row.checked ? 1 : 0.5

                    Behavior on opacity { NumberAnimation { duration: 200 } }
                    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                }

                layer.enabled: true
                layer.effect: DropShadow {
                    verticalOffset: 2
                    radius: 6.0
                    samples: 17
                    color: Qt.rgba(0, 0, 0, 0.2)
                }

                scale: toggleMouse.pressed ? 0.95 : 1.0

                Behavior on x {
                    NumberAnimation {
                        duration: 300
                        easing.type: Easing.OutBack
                        easing.overshoot: 0.6
                    }
                }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }

            // Interactive area with pulse animation
            MouseArea {
                id: toggleMouse
                anchors.fill: parent
                onClicked: row.toggleRequested()
                onPressed: pulseAnimation.start()

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
