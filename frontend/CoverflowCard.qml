import QtQuick
import Qt5Compat.GraphicalEffects

// Reusable coverflow card — one album art image with 3D perspective transform,
// rounded-corner or vinyl masking, and animated coverflow properties.
Item {
    id: root

    // ── Public coverflow properties (animated by parent carousel) ──
    property real cfOffset: 0
    property real cfAngle: 0
    property real cfScale: 1.0
    property real cfOpacity: 1.0

    // ── Image configuration ──
    property url artSource: ""
    property real artSize: width
    property bool asynchronous: true

    // ── Masking ──
    property bool roundedArt: false
    property bool vinylMode: false
    property real artRadius: 0
    // External mask items — shared across all cards to avoid duplicating GPU textures
    property Item roundedMask: null
    property Item vinylMask: null

    // ── Optional: vinyl rotation (only used by the center card) ──
    property real vinylAngle: 0

    // ── Animation state (disables smooth filtering for performance) ──
    property bool animating: false

    // ── Optional: drop shadow (only used by the center card) ──
    property bool showShadow: false
    property bool shadowLive: true

    // ── Convenience alias for the inner Image ──
    readonly property alias image: artImage
    readonly property alias imageStatus: artImage.status

    scale: cfScale
    opacity: cfOpacity

    transform: [
        Translate { x: root.cfOffset },
        Rotation {
            axis { x: 0; y: 1; z: 0 }
            angle: root.cfAngle
            origin.x: root.width / 2
            origin.y: root.height / 2
        }
    ]

    Image {
        id: artImage
        anchors.fill: parent
        source: root.artSource
        sourceSize: Qt.size(root.artSize * 2, root.artSize * 2)
        fillMode: Image.PreserveAspectFit
        smooth: !root.animating
        antialiasing: true
        asynchronous: root.asynchronous
        cache: true
        rotation: root.vinylMode ? root.vinylAngle : 0

        layer.enabled: root.roundedArt || root.vinylMode
        layer.effect: OpacityMask {
            maskSource: root.vinylMode ? root.vinylMask : root.roundedMask
        }
    }

    // Drop shadow — only active when showShadow is true
    layer.enabled: showShadow
    layer.live: shadowLive
    layer.effect: DropShadow {
        transparentBorder: true
        horizontalOffset: 8
        verticalOffset: 8
        radius: 16.0
        samples: 33
        color: "#E0000000"
    }
}
