// VehiclePicture.qml
//
// The vehicle as a picture, for the places that show the car without paying
// for a live Quick3D scene. Give it a size; the picture is fitted inside it.
// Use VehicleView where the car has to move.
//
// The built-in Jeep's picture ships with the frontend (the 3D model rendered
// once by tools/vehicle/render_preview.py). A model the user added gets its
// picture from the app the first time it is shown (Settings > Device >
// Vehicle); until then there is nothing to draw and `hasPicture` is false.
import QtQuick

Image {
    // vehicleManager's active vehicle; the built-in one where there is no manager
    readonly property var spec: (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.active : null
    readonly property bool hasPicture: source.toString() !== ""

    // (resolvedUrl: a plain relative string here is resolved against the file that uses this component)
    source: !spec || spec.builtIn ? Qt.resolvedUrl("assets/jeep_tj/preview.png") : (spec.pictureUrl || "")
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    smooth: true
    mipmap: true
    // A re-rendered picture keeps its file name; the URL's query changes
    cache: false
}
