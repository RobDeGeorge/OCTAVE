import QtQuick 2.15
import Qt5Compat.GraphicalEffects

// Theme-tinted SVG icon.
//
// Wraps the Image + ColorOverlay pair that the rest of the app hand-rolls at
// every call site (BottomBar.qml alone repeats it ~20 times). The assets in
// frontend/assets/ are authored as flat white fills precisely so they can be
// recoloured this way — see docs in TODO/settings-icons-svg.md history.
//
// The optional drop shadow reproduces the offset-glyph trick SettingsTile used
// back when tile icons were Unicode characters, so SVG tiles keep the same
// sense of weight.
Item {
    id: icon

    // Path to a single-fill SVG, e.g. "assets/obd_button.svg". Relative paths
    // resolve against the *importing* file, so callers in subdirectories should
    // pass something like "../assets/foo.svg" or use Qt.resolvedUrl.
    property url source: ""

    // Tint applied to every non-transparent pixel.
    property color color: "#FFFFFF"

    // Static drop shadow behind the icon. Cheap — it is a second ColorOverlay,
    // not a blur, so there is no per-frame cost.
    property bool shadow: true
    property color shadowColor: Qt.rgba(0, 0, 0, 0.40)
    property real shadowOffsetX: 1
    property real shadowOffsetY: 2

    // Multiplier applied to width/height when sizing the rendered image inside
    // this Item. Matches the 0.7 the bottom bar buttons use.
    property real fillRatio: 1.0

    implicitWidth: 24
    implicitHeight: 24

    Image {
        id: sourceImage
        anchors.centerIn: parent
        width: Math.round(parent.width * icon.fillRatio)
        height: Math.round(parent.height * icon.fillRatio)
        source: icon.source
        // 2x supersample keeps SVG edges crisp when the tile scales up.
        sourceSize: Qt.size(Math.max(1, width * 2), Math.max(1, height * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        antialiasing: true
        mipmap: true
        visible: false
    }

    ColorOverlay {
        visible: icon.shadow && sourceImage.status === Image.Ready
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: icon.shadowOffsetX
        anchors.verticalCenterOffset: icon.shadowOffsetY
        width: sourceImage.width
        height: sourceImage.height
        source: sourceImage
        color: icon.shadowColor
    }

    ColorOverlay {
        visible: sourceImage.status === Image.Ready
        anchors.fill: sourceImage
        source: sourceImage
        color: icon.color
    }
}
