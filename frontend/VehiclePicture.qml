// VehiclePicture.qml
//
// A vehicle as a picture, for the places that show the car without paying
// for a live Quick3D scene. Give it a size; the picture is fitted inside it.
// Use VehicleView where the car has to move.
//
// A vehicle that ships with the frontend has its picture under assets/ (the
// 3D model rendered once by tools/vehicle/render_preview.py). A model the
// user added gets its picture from the app the first time it is shown
// (Settings > Device > Vehicle). With no vehicle chosen, or no picture yet,
// there is nothing to draw and `hasPicture` is false.
import QtQuick

Image {
    // Which vehicle: vehicleManager's active one, or a row of
    // vehicleManager.vehicles (the chooser's tiles). Without a manager (the
    // tools that load the frontend's files alone), the Jeep.
    property var spec: (typeof vehicleManager !== "undefined" && vehicleManager)
        ? vehicleManager.active : ({ id: "jeep-tj", pictureAsset: "jeep_tj/preview.png" })
    readonly property bool hasPicture: source.toString() !== ""

    // (resolvedUrl: a plain relative string here is resolved against the file that uses this component)
    source: !spec ? "" : spec.pictureAsset ? Qt.resolvedUrl("assets/" + spec.pictureAsset) : (spec.pictureUrl || "")
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    smooth: true
    mipmap: true
    // A re-rendered picture keeps its file name; the URL's query changes
    cache: false
}
