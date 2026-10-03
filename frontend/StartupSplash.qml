import QtQuick 2.15
import "." as App

// Full-window startup screen shown while Main.qml pre-builds the pages the
// nav buttons open. It covers the app (taps included) so the builds don't
// compete with input, and the heavy startup work (Quick3D pages, the first
// track's audio analysis) runs one step at a time behind it instead of
// stacking up on the first frames. A tap skips it: the rest keeps building
// in the background as before.
//
// Kept cheap to draw on purpose (no blur, no layers): it is on screen while
// the GPU is busiest, on hardware that browned out under startup load.
Rectangle {
    id: splash

    // 0..1, animated
    property real progress: 0
    property string status: "Starting up"
    // Set by finish(); the Loader unloads the splash once the fade is done.
    property bool finished: false

    signal skipRequested()
    signal faded()

    color: App.Style.backgroundColor

    function finish() {
        if (finished)
            return
        finished = true
        progress = 1
        fadeOut.start()
    }

    // Swallow every press: nothing underneath should react while it builds.
    MouseArea {
        anchors.fill: parent
        onClicked: splash.skipRequested()
    }

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -App.Spacing.overallText
        spacing: App.Spacing.overallText * 1.2
        width: Math.min(splash.width * 0.6, App.Spacing.overallText * 30)

        Text {
            id: title
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "OCTAVE"
            font.pixelSize: App.Spacing.overallText * 5
            font.family: App.Style.fontFamily
            font.bold: true
            color: App.Style.accent

            // Slow breathing while it works
            SequentialAnimation on opacity {
                running: splash.visible && !splash.finished
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.7; duration: 1200; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.7; to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
            }
        }

        Rectangle {
            id: track
            width: parent.width
            height: Math.max(3, App.Spacing.overallText * 0.25)
            radius: height / 2
            color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g,
                           App.Style.primaryTextColor.b, 0.12)

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: App.Style.accent
                width: parent.width * Math.max(0, Math.min(1, splash.progress))
                Behavior on width { NumberAnimation { duration: 350; easing.type: Easing.OutCubic } }
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: splash.status
            font.pixelSize: App.Spacing.overallText
            font.family: App.Style.fontFamily
            color: App.Style.secondaryTextColor
            elide: Text.ElideRight
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: App.Spacing.overallText * 1.5
        text: "Tap to skip"
        font.pixelSize: App.Spacing.overallText * 0.8
        font.family: App.Style.fontFamily
        color: App.Style.secondaryTextColor
        opacity: 0.6
    }

    OpacityAnimator {
        id: fadeOut
        target: splash
        from: 1
        to: 0
        duration: 350
        easing.type: Easing.InOutQuad
        onFinished: splash.faded()
    }
}
