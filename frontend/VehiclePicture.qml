// VehiclePicture.qml
//
// The vehicle as a picture: the 3D model rendered once (by
// tools/vehicle/render_preview.py), for the places that show the car without
// paying for a live Quick3D scene. Give it a size; the picture is fitted
// inside it. Use VehicleView where the car has to move.
import QtQuick

Image {
    source: "assets/jeep_tj/preview.png"
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    smooth: true
    mipmap: true
}
