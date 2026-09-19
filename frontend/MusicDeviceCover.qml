import QtQuick

// Shared by printed labels and screens. A failed cover never blanks a device.
Image {
    id: cover
    property url artSource: ""
    property bool failed: false
    readonly property url fallbackSource: Qt.resolvedUrl("assets/missing_art.png")
    source: failed || artSource.toString() === "" ? fallbackSource : artSource
    onArtSourceChanged: failed = false
    onStatusChanged: if (status === Image.Error && source.toString() !== fallbackSource.toString()) failed = true
    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    sourceSize: Qt.size(512, 512)
}
