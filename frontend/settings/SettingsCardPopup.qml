import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Rectangle {
    id: popup

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property string title: ""
    property var contentComponent: null

    // ── Hero morph: grows out of the tapped tile, shrinks back into it ──
    // The host sets `open` and the tile's rect (in the parent's coordinates);
    // the popup fills its parent when fully open.
    property bool open: false
    property rect originRect: Qt.rect(0, 0, 0, 0)
    // 0.0 = collapsed onto the originating tile, 1.0 = filling the parent.
    property real openProgress: open ? 1.0 : 0.0
    readonly property bool morphing: open || openProgress > 0.001
    // Header + body fade in over the second half of the open (and out over
    // the first half of the close), once there is room for them.
    readonly property real contentOpacity: Math.max(0.0, (openProgress - 0.45) / 0.55)

    signal backRequested()

    // Title and content are held until the close finishes, so they fade
    // out with the shrink instead of vanishing on its first frame. The
    // content's Loader unloads then, so Component.onDestruction still runs
    // (e.g. the Now Playing live PiP releases its resources).
    property string _title: ""
    property var _component: null
    onTitleChanged: if (title !== "") _title = title
    onContentComponentChanged: if (contentComponent) _component = contentComponent
    onOpenProgressChanged: {
        if (openProgress <= 0.001 && !open) {
            _component = null
            _title = ""
        }
    }

    // Suppress the animation on mount so the popup does not visibly
    // animate at startup.
    property bool _animEnabled: false
    Component.onCompleted: Qt.callLater(function() { _animEnabled = true })

    // Call before the host clears `open` when the tile it would shrink back
    // into is going away (a section switch): the popup closes in one frame.
    function snapShut() {
        _animEnabled = false
        Qt.callLater(function() { popup._animEnabled = true })
    }
    Behavior on openProgress {
        enabled: popup._animEnabled
        // The close is a little quicker: OutCubic's long tail spent the end
        // of a 320 ms close barely moving over the tile.
        NumberAnimation { duration: popup.open ? 320 : 260; easing.type: Easing.OutCubic }
    }

    x: originRect.x * (1.0 - openProgress)
    y: originRect.y * (1.0 - openProgress)
    width: originRect.width + ((parent ? parent.width : 0) - originRect.width) * openProgress
    height: originRect.height + ((parent ? parent.height : 0) - originRect.height) * openProgress

    // The tile stays drawn underneath. The popup's background is clear at
    // the tile end of the morph and fills in over the first 30 %, so the
    // tile cross-fades into the card on open and back out of it on close
    // instead of being covered by a blank block. Tinted like the tile and
    // with its corner radius, so nothing pops at either end.
    color: {
        var c = Qt.tint(App.Style.contentColor,
                        Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g,
                                App.Style.primaryTextColor.b, 0.05 * (1.0 - openProgress)))
        return Qt.rgba(c.r, c.g, c.b, Math.min(1.0, openProgress / 0.3))
    }
    radius: Math.max(2, dp(App.EnvironmentTheme.active.cardRadius)) * (1.0 - openProgress)
    clip: true

    // HUD lines (spacecraft)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: App.Spacing.settingsContentMargin
        anchors.rightMargin: App.Spacing.settingsContentMargin
        height: 1
        z: 1
        opacity: popup.contentOpacity
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
        visible: App.EnvironmentTheme.active.contentHudLines
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: App.Spacing.settingsContentMargin
        anchors.rightMargin: App.Spacing.settingsContentMargin
        height: 1
        z: 1
        opacity: popup.contentOpacity
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3)
        visible: App.EnvironmentTheme.active.contentHudLines
    }

    // Header bar with back button — matches sidebar nav delegate sizing
    Item {
        id: header
        opacity: popup.contentOpacity
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: App.Spacing.settingsButtonHeight
        z: 2

        Row {
            anchors.left: parent.left
            anchors.leftMargin: App.Spacing.settingsContentMargin
            anchors.verticalCenter: parent.verticalCenter
            spacing: App.Spacing.overallSpacing * 0.5

            Text {
                text: "→"
                color: backArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.6
                font.family: App.Style.fontFamily
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 150 } }
            }

            Text {
                text: popup._title
                color: backArea.containsMouse ? App.Style.accent : App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.6
                font.bold: true
                font.family: App.Style.fontFamily
                font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
                font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        MouseArea {
            id: backArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: popup.backRequested()
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: App.Style.accent
        }
    }

    // Content body — scrollable for tall cards
    // Laid out at the fully-open size for the whole morph and clipped by the
    // popup, so the content does not re-wrap on every frame while it grows.
    Flickable {
        id: bodyFlick
        opacity: popup.contentOpacity
        // Same gap under the header and above the bottom bar as the music library
        x: App.Spacing.settingsContentMargin
        y: header.height + App.Spacing.overallMargin
        width: Math.max(0, (popup.parent ? popup.parent.width : popup.width) - 2 * App.Spacing.settingsContentMargin)
        height: Math.max(0, (popup.parent ? popup.parent.height : popup.height) - y - App.Spacing.overallMargin)
        contentWidth: width
        contentHeight: contentLoader.item ? contentLoader.item.implicitHeight : 0
        flickableDirection: Flickable.VerticalFlick
        clip: true
        boundsBehavior: Flickable.DragAndOvershootBounds
        flickDeceleration: 1200
        maximumFlickVelocity: 4000
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Loader {
            id: contentLoader
            width: bodyFlick.width
            height: item ? item.implicitHeight : 0
            sourceComponent: popup._component
        }
    }
}
