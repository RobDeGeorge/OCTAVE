import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

Item {
    Layout.fillWidth: true
    height: App.EnvironmentTheme.active.dividerHeight
    Layout.topMargin: App.Spacing.overallSpacing
    Layout.bottomMargin: App.Spacing.overallSpacing

    Loader {
        anchors.fill: parent
        sourceComponent: {
            if (App.EnvironmentTheme.active.dividerSonarPing) return deepSeaDivider
            if (App.EnvironmentTheme.active.dividerGradient) return spacecraftDivider
            return standardDivider
        }
    }

    // ─── Standard: simple translucent line ───
    Component {
        id: standardDivider
        Rectangle {
            color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.1)
        }
    }

    // ─── Deep Sea: accent gradient + sonar ping ring ───
    Component {
        id: deepSeaDivider
        Item {
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.15; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2) }
                    GradientStop { position: 0.5; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) }
                    GradientStop { position: 0.85; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            Rectangle {
                id: sonarPing
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenter: parent.horizontalCenter
                width: 0; height: 0
                opacity: 0
                radius: height / 2
                color: "transparent"
                border.width: 1
                border.color: App.Style.accent

                SequentialAnimation {
                    running: sonarPing.visible   // an infinite animation keeps the render loop awake; stop it off-screen
                    loops: Animation.Infinite
                    ParallelAnimation {
                        NumberAnimation { target: sonarPing; property: "width"; from: 0; to: 60; duration: 3000; easing.type: Easing.OutCubic }
                        NumberAnimation { target: sonarPing; property: "height"; from: 0; to: 60; duration: 3000; easing.type: Easing.OutCubic }
                        SequentialAnimation {
                            NumberAnimation { target: sonarPing; property: "opacity"; from: 0; to: 0.5; duration: 200; easing.type: Easing.OutQuad }
                            NumberAnimation { target: sonarPing; property: "opacity"; from: 0.5; to: 0; duration: 2800; easing.type: Easing.InQuad }
                        }
                    }
                    PauseAnimation { duration: 5000 }
                }
            }
        }
    }

    // ─── Spacecraft: accent gradient + pulse + diamond ───
    Component {
        id: spacecraftDivider
        Item {
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.3; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.6) }
                    GradientStop { position: 0.5; color: App.Style.accent }
                    GradientStop { position: 0.7; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.6) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            Rectangle {
                id: pulseGlow
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenter: parent.horizontalCenter
                height: parent.height
                width: 0
                opacity: 0
                radius: 1
                visible: App.EnvironmentTheme.active.dividerAnimated
                color: Qt.rgba(1, 1, 1, 0.6)

                SequentialAnimation {
                    running: App.EnvironmentTheme.active.dividerAnimated
                    loops: Animation.Infinite
                    ParallelAnimation {
                        NumberAnimation { target: pulseGlow; property: "width"; from: 0; to: pulseGlow.parent ? pulseGlow.parent.width : 400; duration: 2500; easing.type: Easing.OutCubic }
                        SequentialAnimation {
                            NumberAnimation { target: pulseGlow; property: "opacity"; from: 0; to: 0.8; duration: 400; easing.type: Easing.OutQuad }
                            NumberAnimation { target: pulseGlow; property: "opacity"; from: 0.8; to: 0; duration: 2100; easing.type: Easing.InQuad }
                        }
                    }
                    PauseAnimation { duration: 1500 }
                }
            }

            Rectangle {
                id: centerDiamond
                width: 6; height: 6
                rotation: 45
                anchors.centerIn: parent
                color: App.Style.accent

                SequentialAnimation on rotation {
                    running: App.EnvironmentTheme.active.dividerDiamondRotate
                    loops: Animation.Infinite
                    NumberAnimation { from: 45; to: 135; duration: 3000; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 135; to: 45; duration: 3000; easing.type: Easing.InOutSine }
                }
            }
        }
    }
}
