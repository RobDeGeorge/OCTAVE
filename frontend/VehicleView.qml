// VehicleView.qml
//
// The 3D vehicle as a component: the scene, its lights and camera, the orbit /
// zoom / tap gestures and the vehicle itself. The host gives it a size, says
// when it is on screen (`active`) and feeds it an `attitude`; everything a
// page adds around the car (readouts, menus) stays in the page.
//
//   VehicleView { anchors.fill: parent; active: page.visible; attitude: q }
//
// One at a time: a Quick3D scene is the most expensive thing the app draws.
import QtQuick
import QtQuick3D
import "." as App

Item {
    id: root

    // Camera orbit, in degrees, and zoom (1 = the vehicle fitted to the view)
    property real cameraYaw: -35
    property real cameraPitch: -25
    property real cameraDistance: 200
    property real cameraZoom: 1
    readonly property real defaultYaw: -35
    readonly property real defaultPitch: -25

    // Orientation of the vehicle in the scene's frame (+Y up, +Z forward)
    property quaternion attitude
    // Whether the view is on screen (stops the rig's spin timer when not)
    property bool active: true
    // Drag to orbit, pinch / wheel to zoom, tap a panel to open it,
    // double-tap beside the vehicle to restore the view
    property bool interactive: true
    // A level ring on the ground, the reference the vehicle tilts against
    property bool showLevelRing: true

    // The radius the camera frames, in the rig's metres: the vehicle closed,
    // or with its panels swung out. It eases between the two in step with the
    // hinges (same duration and curve as JeepHinge), starting with the tap in
    // both directions, so the camera glides with the panel and does not jump
    // when the last hinge comes to rest.
    property real fitRadius: vehicle && vehicle.anyOpenRequested ? vehicle.sweptRadius : closedRadius
    Behavior on fitRadius { NumberAnimation { duration: 450; easing.type: Easing.InOutCubic } }

    // Which vehicle: vehicleManager's active one ({id, rigged, asset,
    // modelUrl, yaw, tilt, ...}), or the built-in rig where there is no
    // manager (the tools that load this file alone)
    property var spec: (typeof vehicleManager !== "undefined" && vehicleManager) ? vehicleManager.active : ({ rigged: true })
    readonly property bool rigged: !spec || spec.rigged !== false
    // False while no vehicle is chosen (id ""): nothing is loaded then
    readonly property bool hasVehicle: !spec || spec.id !== ""
    // The model file of an unrigged vehicle: one that ships with the
    // frontend comes through assetStore (a real file, extracted from the APK
    // on Android), the user's own is already a file: URL
    readonly property url modelSource: !spec || rigged ? ""
        : spec.asset ? ((typeof assetStore !== "undefined" && assetStore) ? assetStore.localUrl(spec.asset) : "")
        : (spec.modelUrl || "")

    // The JeepVehicle (the Jeep's own rig), a RigVehicle (a rig from a
    // rig.json) or GenericVehicle (a model of the user's): all offer the
    // members used here. Null for a moment while
    // the vehicle is being swapped, and while none is chosen.
    readonly property var vehicle: vehicleLoader.item
    readonly property bool ready: !!vehicle && vehicle.ready
    readonly property string error: vehicle ? vehicle.error : ""
    readonly property real closedRadius: vehicle ? vehicle.closedRadius : 2.13
    readonly property real groundY: vehicle ? vehicle.groundY : -1

    Component { id: rigComponent; JeepVehicle { active: root.active } }
    // A rigged vehicle described by a rig.json (spec.rig: its folder under assets/)
    Component { id: dataRigComponent; RigVehicle { active: root.active; folder: root.spec.rig } }
    Component {
        id: modelComponent
        GenericVehicle {
            active: root.active
            source: root.modelSource
            yaw: root.spec ? Number(root.spec.yaw) || 0 : 0
            tilt: root.spec ? Number(root.spec.tilt) || 0 : 0
        }
    }

    function zoomCamera(factor) {
        if (isFinite(factor) && factor > 0)
            cameraZoom = Math.max(0.4, Math.min(3, cameraZoom * factor))
    }
    function resetView() {
        // The short way round, whatever the yaw has wound up to
        var yaw = ((cameraYaw - defaultYaw) % 360 + 540) % 360 - 180 + defaultYaw
        cameraYaw = yaw
        resetAnimation.restart()
    }
    ParallelAnimation {
        id: resetAnimation
        NumberAnimation { target: root; property: "cameraYaw"; to: root.defaultYaw; duration: 300; easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "cameraPitch"; to: root.defaultPitch; duration: 300; easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "cameraZoom"; to: 1; duration: 300; easing.type: Easing.OutCubic }
    }

    // The JeepVehicle property a tap on this scene object toggles, or "".
    function _tapKeyOf(node) {
        while (node) {
            if (node.tapKey !== undefined)
                return node.tapKey
            node = node.parent
        }
        return ""
    }

    View3D {
        id: view3d
        objectName: "jeepScene"
        anchors.fill: parent

        environment: SceneEnvironment {
            // The page shows through, so the car sits on the theme's background
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
            aoEnabled: false
            lightProbe: Texture { source: "./assets/vehicle_studio.hdr" }
            probeExposure: 0.8
        }

        Node {
            id: cameraOrbit
            eulerRotation: Qt.vector3d(root.cameraPitch, root.cameraYaw, 0)

            PerspectiveCamera {
                id: camera
                objectName: "jeepCamera"
                // Start fitted even in narrow views; zoom operates on
                // that distance while keeping the camera outside the rig.
                readonly property real fittedDistance: Math.max(root.cameraDistance,
                    (root.fitRadius * 30 + 3) / Math.sin(Math.atan(Math.tan(Math.PI / 8)
                        * Math.min(1, view3d.width / Math.max(1, view3d.height)))))
                position: Qt.vector3d(0, 0, Math.max(root.fitRadius * 30 + clipNear + 2,
                    fittedDistance / root.cameraZoom))
                fieldOfView: 45
                clipNear: 10
                clipFar: Math.max(1000, position.z + root.fitRadius * 30 + 100)
            }
        }

        DirectionalLight {
            eulerRotation.x: -45
            eulerRotation.y: 45
            brightness: 1.3
            ambientColor: "#777777"
            castsShadow: false
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-25, -135, 0)
            brightness: 0.7
            color: "#C3D9FF"
        }

        // Stays level while the vehicle tilts: a ring on the ground under the
        // tyres. A flat quad carrying a 2D ring, as Quick3D has no ring mesh.
        Model {
            objectName: "levelRing"
            visible: root.showLevelRing && root.ready
            source: "#Rectangle"
            position: Qt.vector3d(0, root.groundY * 30, 0)
            eulerRotation.x: -90
            // "#Rectangle" is 100 units across
            scale: Qt.vector3d(root.closedRadius * 0.6, root.closedRadius * 0.6, 1)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                alphaMode: PrincipledMaterial.Blend
                cullMode: Material.NoCulling
                baseColorMap: Texture {
                    sourceItem: Rectangle {
                        width: 512; height: 512; radius: 256
                        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.07)
                        border.width: 3
                        border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.7)
                        antialiasing: true
                    }
                }
            }
        }

        // Metre-scale vehicle, +Z forward / +Y up, pivot at its centre.
        Node {
            id: carModel
            scale: Qt.vector3d(30, 30, 30)
            rotation: root.attitude

            Loader3D {
                id: vehicleLoader
                sourceComponent: !root.hasVehicle ? null
                    : !root.rigged ? modelComponent
                    : (root.spec && root.spec.rig) ? dataRigComponent : rigComponent
            }
        }
    }

    // Separate one-point orbit and two-point zoom gestures so a pinch
    // does not also rotate the vehicle.
    Item {
        anchors.fill: view3d
        enabled: root.interactive
        // Dragging turns the vehicle: no page swipe here (Main.qml pageSwipe)
        readonly property bool octaveNoPageSwipe: true
        PinchHandler {
            id: scenePinch
            target: null
            minimumPointCount: 2
            maximumPointCount: 2
            onScaleChanged: (delta) => root.zoomCamera(delta)
        }
        DragHandler {
            target: null
            maximumPointCount: 1
            property vector2d previousTranslation: Qt.vector2d(0, 0)
            onActiveChanged: previousTranslation = Qt.vector2d(0, 0)
            onActiveTranslationChanged: {
                if (active && !scenePinch.active) {
                    var delta = activeTranslation.minus(previousTranslation)
                    root.cameraYaw -= delta.x * 0.5
                    root.cameraPitch = Math.max(-89, Math.min(89, root.cameraPitch + delta.y * 0.3))
                }
                previousTranslation = activeTranslation
            }
        }
        WheelHandler {
            target: null
            // Hyprland's Qt Wayland labels mouse wheels as touchpads.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => root.zoomCamera(Math.pow(1.0015, event.angleDelta.y))
        }
        TapHandler {
            onTapped: (eventPoint) => {
                if (!root.ready)
                    return
                var hit = view3d.pick(eventPoint.position.x, eventPoint.position.y)
                var key = root._tapKeyOf(hit.objectHit)
                if (key !== "")
                    root.vehicle.toggle(key)
                else if (tapCount === 2)
                    root.resetView()
            }
        }
    }
}
