"""Exercise the real Qt vehicle component, hinges, lights, and IMU mapping.

Run from the checkout: QT_QPA_PLATFORM=xcb xvfb-run -a venv/bin/python tools/vehicle/check_jeep_view.py
Screenshots are written to /tmp/jeep-rig-*.png. Requires PySide6 with Quick3D.
"""

import sys
import time
from pathlib import Path

from PySide6.QtCore import QObject, QPointF, Property, QMetaObject, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QGuiApplication, QVector3D
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickItem, QQuickWindow
from PySide6.QtTest import QTest


class IMU(QObject):
    orientationChanged = Signal(float, float, float, float)
    pitchChanged = Signal(float)
    rollChanged = Signal(float)
    headingChanged = Signal(float)
    altitudeChanged = Signal(float)
    accelMagnitudeChanged = Signal(float)
    baroTempChanged = Signal(float)
    connectionStatusChanged = Signal(str)

    @Property(bool, constant=True)
    def connected(self):
        return False


class OBD(QObject):
    """Just enough obdManager for CarMenu's live drive and polling demand."""

    connectionStatusChanged = Signal(str)
    rpmChanged = Signal(float)
    speedMPHChanged = Signal(float)

    def __init__(self):
        super().__init__()
        self._connected = False
        self.demand = {}

    def _get_connected(self):
        return self._connected

    connected = Property(bool, _get_connected, notify=connectionStatusChanged)

    def set_connected(self, value):
        self._connected = value
        self.connectionStatusChanged.emit("Connected" if value else "Disconnected")

    @Slot(str, "QVariantList")
    def setParameterDemand(self, consumer, ids):
        self.demand[consumer] = list(ids)


app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
imu = IMU()
obd = OBD()
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from backend.asset_store import AssetStore  # noqa: E402  (JeepHinge loads parts through it)

assets = AssetStore(str(Path(__file__).resolve().parents[2] / "frontend/assets"))
engine.rootContext().setContextProperty("berryIMU", imu)
engine.rootContext().setContextProperty("obdManager", obd)
engine.rootContext().setContextProperty("assetStore", assets)
engine.rootContext().setContextProperty("width", 1280)
engine.rootContext().setContextProperty("height", 800)
frontend = Path(__file__).resolve().parents[2] / "frontend"
engine.loadData(
    f'''import QtQuick
import QtQuick.Window
import "{frontend.as_uri()}" as App
Window {{ width: 1280; height: 800; visible: true
App.CarMenu {{ anchors.fill: parent; stackView: null }}
}}'''.encode()
)
assert engine.rootObjects(), "Component failed to load"
window = engine.rootObjects()[0]
assert isinstance(window, QQuickWindow)
vehicle = window.findChild(QObject, "jeepVehicle")
menu = window.findChild(QObject, "carMenu")
failures = []


def settle(condition, timeout_ms=1500):
    """Poll a condition while the event loop runs. Under Xvfb's software
    renderer, timers and scene updates can lag a fixed wait by a frame or two."""
    deadline = time.monotonic() + timeout_ms / 1000
    while not condition() and time.monotonic() < deadline:
        QTest.qWait(20)
    return condition()


def check(fn):
    def run():
        try:
            fn()
        except Exception as exc:
            failures.append(exc)
            app.exit(1)

    return run


def hinge(name):
    return window.findChild(QObject, name + "Hinge")


def visual_item(item, name):
    if item.objectName() == name:
        return item
    for child in item.childItems():
        found = visual_item(child, name)
        if found is not None:
            return found
    return None


def capture(label):
    assert window.grabWindow().save(f"/tmp/jeep-rig-{label}.png")


@check
def rear_sequence():
    assert vehicle.property("ready"), vehicle.property("error")
    button = visual_item(window.contentItem(), "jeepControl_tailgateOpen")
    point = button.mapToScene(QPointF(button.width() / 2, button.height() / 2)).toPoint()

    def click():
        QTest.mouseClick(window, Qt.LeftButton, pos=point)

    def wait_for_sequence(opening):
        observed_first_stage = False
        observed_second_stage = False
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            QTest.qWait(20)
            glass_angle = hinge("glass").property("angle")
            gate_angle = hinge("gate").property("angle")
            if opening:
                if glass_angle > 0.01:
                    assert hinge("gate").property("opened"), "Glass rose before gate finished opening"
                    observed_second_stage = True
                elif hinge("gate").property("moving"):
                    observed_first_stage = True
                done = hinge("gate").property("opened") and hinge("glass").property("opened")
            else:
                if gate_angle < 94.99:
                    assert hinge("glass").property("closed"), "Gate closed before glass finished lowering"
                    observed_second_stage = True
                elif hinge("glass").property("moving"):
                    observed_first_stage = True
                done = hinge("gate").property("closed") and hinge("glass").property("closed")
            if done:
                assert observed_first_stage and observed_second_stage
                return
        raise AssertionError("Rear-panel sequence did not finish")

    click()
    wait_for_sequence(True)
    click()
    assert not vehicle.property("rearGlassOpen"), "Closing the gate must also request glass closure"
    wait_for_sequence(False)
    # Reverse a request before the gate finishes, then resume opening.
    click()
    QTest.qWait(100)
    click()
    QTest.qWait(100)
    click()
    wait_for_sequence(True)
    click()
    wait_for_sequence(False)
    initial()


@check
def initial():
    assert vehicle.property("ready"), vehicle.property("error")
    nodes = [
        o
        for o in window.findChildren(QObject)
        if o.metaObject().indexOfProperty("scale") >= 0 and o.property("scale") == QVector3D(30, 30, 30)
    ]
    assert len(nodes) == 1
    for q in [(0.9659258, 0.258819, 0, 0), (0.9659258, 0, 0.258819, 0), (0.7071068, 0, 0, 0.7071068), (1, 0, 0, 0)]:
        imu.orientationChanged.emit(*q)
        actual = nodes[0].property("rotation")
        assert all(
            abs(a - b) < 1e-5
            for a, b in zip(
                (actual.scalar(), actual.x(), actual.y(), actual.z()), (q[0], q[2], q[3], q[1]), strict=True
            )
        )
    capture("closed")
    steering_slider = visual_item(window.contentItem(), "jeepSteering")
    assert isinstance(steering_slider, QQuickItem)
    target = steering_slider.mapToScene(QPointF(steering_slider.width() * 0.9, steering_slider.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=target)
    assert vehicle.property("steeringInput") < -0.5
    vehicle.setProperty("steeringInput", 0)
    # Exercise the actual toolbar, including its checked binding and hit area.
    for key in ("driverDoorOpen", "headlightsOn", "fogLightsOn", "brakeLightsOn", "reverseLightsOn"):
        button = visual_item(window.contentItem(), "jeepControl_" + key)
        assert isinstance(button, QQuickItem)
        assert button is not None
        center = button.mapToScene(QPointF(button.width() / 2, button.height() / 2)).toPoint()
        for expected in (True, False):
            QTest.mouseClick(window, Qt.LeftButton, pos=center)
            assert vehicle.property(key) == expected
            assert button.property("checked") == expected
            names = {
                "fogLightsOn": [("fogLight", 2)],
                "brakeLightsOn": [("brakeLight", 2), ("thirdBrakeLight", 1)],
                "reverseLightsOn": [("reverseLight", 2)],
            }.get(key, [])
            for name, count in names:
                for suffix in ("Glow", "Beam"):
                    lights = window.findChildren(QObject, name + suffix)
                    assert len(lights) == count, (name, suffix, len(lights))
                    assert all(light.property("visible") == expected for light in lights)
            for other in ("headlightsOn", "fogLightsOn", "brakeLightsOn", "reverseLightsOn"):
                if other != key:
                    assert not vehicle.property(other), "Light controls must be independent"
    global third_light_closed, rear_light_closed
    third_light_closed = window.findChild(QObject, "thirdBrakeLight").property("scenePosition")
    rear_light_closed = window.findChildren(QObject, "brakeLight")[0].property("scenePosition")
    button = visual_item(window.contentItem(), "jeepOpenAll")
    assert isinstance(button, QQuickItem)
    center = button.mapToScene(QPointF(button.width() / 2, button.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=center)
    assert all(
        vehicle.property(key)
        for key in ("driverDoorOpen", "passengerDoorOpen", "hoodOpen", "rearGlassOpen", "tailgateOpen")
    )
    assert not vehicle.property("headlightsOn"), "Open all should only open panels"
    for key in ("headlightsOn", "fogLightsOn", "brakeLightsOn", "reverseLightsOn"):
        vehicle.setProperty(key, True)
    assert vehicle.property("rearGlassOpen")
    assert hinge("glass").property("closed"), "Glass must wait for gate"
    QTimer.singleShot(2200, opened)


@check
def opened():
    for name in ("driver", "passenger", "glass", "gate", "hood"):
        assert hinge(name).property("opened"), name
    assert hinge("driver").property("angle") < 0 < hinge("passenger").property("angle")
    beams = window.findChildren(QObject, "headlightBeam")
    assert len(beams) == 2 and all(o.property("visible") for o in beams)
    third_now = window.findChild(QObject, "thirdBrakeLight").property("scenePosition")
    assert (third_now - third_light_closed).length() > 5, "Third brake lamp must follow the tailgate"
    rear_now = window.findChildren(QObject, "brakeLight")[0].property("scenePosition")
    assert (rear_now - rear_light_closed).length() < 1e-5, "Body brake lights must stay on the tub"
    capture("open")
    menu.setProperty("cameraYaw", 145)
    QTimer.singleShot(500, rear)


@check
def rear():
    capture("rear-open")
    QMetaObject.invokeMethod(vehicle, "closeAll")
    assert not vehicle.property("tailgateOpen")
    assert hinge("gate").property("open"), "Gate must wait for glass on closing"
    vehicle.setProperty("headlightsOn", False)
    QTimer.singleShot(2200, closed)


@check
def closed():
    assert all(hinge(name).property("closed") for name in ("driver", "passenger", "glass", "gate", "hood"))
    assert all(not o.property("visible") for o in window.findChildren(QObject, "headlightBeam"))
    def third_offset():
        return (window.findChild(QObject, "thirdBrakeLight").property("scenePosition") - third_light_closed).length()

    # Open, the lamp is > 5 units away. Closed, a residual of up to ~0.006
    # units (0.2 mm on the vehicle) shows up on some Xvfb runs with the gate
    # at exactly 0 degrees; cause not found, so allow 0.02 units (0.7 mm).
    assert settle(lambda: third_offset() < 0.02), ("Third brake lamp must return with the gate", third_offset())
    capture("rear-lights")
    for key in ("fogLightsOn", "brakeLightsOn", "reverseLightsOn"):
        vehicle.setProperty(key, False)
    for name in ("fogLight", "brakeLight", "thirdBrakeLight", "reverseLight"):
        for suffix in ("Glow", "Beam"):
            assert all(not light.property("visible") for light in window.findChildren(QObject, name + suffix))
    menu.setProperty("cameraYaw", -35)
    menu.setProperty("cameraPitch", 65)
    QTimer.singleShot(500, underside)


@check
def underside():
    capture("underside")
    menu.setProperty("cameraYaw", 25)
    menu.setProperty("cameraPitch", 35)
    vehicle.setProperty("steeringInput", 1)
    QTimer.singleShot(700, steering_left)


@check
def steering_left():
    left = window.findChild(QObject, "driverWheel").property("angle")
    right = window.findChild(QObject, "passengerWheel").property("angle")
    assert left > right > 15
    spin_nodes = [
        window.findChild(QObject, name)
        for name in ("driverFrontSpin", "passengerFrontSpin", "driverRearSpin", "passengerRearSpin")
    ]
    positions = [node.property("scenePosition") for node in spin_nodes]
    vehicle.setProperty("wheelSpinAngle", 0)
    button = visual_item(window.contentItem(), "jeepSpinWheels")
    point = button.mapToScene(QPointF(button.width() / 2, button.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=point)
    assert vehicle.property("wheelSpeedKph") == 10
    QTest.qWait(100)
    phase = vehicle.property("wheelSpinAngle")
    assert 5 < phase < 180, phase
    for node, position in zip(spin_nodes, positions, strict=True):
        assert abs(node.property("angle") - phase) < 1e-5
        assert (node.property("scenePosition") - position).length() < 1e-4, "Wheel center orbited while spinning"
    for wheel, steering in zip(spin_nodes[:2], ("driverWheel", "passengerWheel"), strict=True):
        axis = QVector3D(1, 0, 0)
        spin_axis = wheel.property("sceneRotation").rotatedVector(axis)
        steer_axis = window.findChild(QObject, steering).property("sceneRotation").rotatedVector(axis)
        assert (spin_axis - steer_axis).length() < 1e-5, "Spin axis must follow steering"
    assert abs(hinge("gate").property("angle")) < 0.01, "Spare must remain still"
    capture("wheels-spinning")
    slider = visual_item(window.contentItem(), "jeepWheelSpeed")
    target = slider.mapToScene(QPointF(slider.width() * 0.25, slider.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=target)
    assert vehicle.property("wheelSpeedKph") < -5
    before = vehicle.property("wheelSpinAngle")

    def change():
        return (vehicle.property("wheelSpinAngle") - before + 540) % 360 - 180

    assert settle(lambda: change() < -5), ("Negative speed must reverse rotation", change())
    QTest.mouseClick(window, Qt.LeftButton, pos=point)
    assert vehicle.property("wheelSpeedKph") == 0
    stopped = vehicle.property("wheelSpinAngle")
    QTest.qWait(100)
    assert vehicle.property("wheelSpinAngle") == stopped, "Stopped wheels must hold their phase"
    vehicle.setProperty("wheelSpinAngle", 0)
    capture("steer-left")
    QTimer.singleShot(0, engine_driveline)


def circular(a):
    return (a + 540) % 360 - 180


@check
def engine_driveline():
    nodes = {n: window.findChild(QObject, n) for n in ("crankPulley", "engineFan", "frontDriveshaft", "rearDriveshaft")}
    assert all(nodes.values()), nodes
    # Local rotation is orient * spin-about-Z, so the shaft axis is orient applied to +Z.
    def axis(node):
        return node.property("rotation").rotatedVector(QVector3D(0, 0, 1))

    axes = {n: axis(node) for n, node in nodes.items()}
    positions = {n: node.property("scenePosition") for n, node in nodes.items()}
    assert all(a.z() > 0.85 for a in axes.values()), "Spinner axes must point forward"
    assert obd.demand.get("carMenu") == ["RPM", "SPEED"], obd.demand
    # Driveshafts follow the wheels at the axle ratio, clockwise from the front.
    for name in ("wheelSpinAngle", "driveshaftAngle", "crankAngle"):
        vehicle.setProperty(name, 0)
    vehicle.setProperty("wheelSpeedKph", 10)
    QTest.qWait(150)
    vehicle.setProperty("wheelSpeedKph", 0)
    wheel, shaft = vehicle.property("wheelSpinAngle"), vehicle.property("driveshaftAngle")
    assert 10 < wheel < 180, wheel
    assert abs(circular(shaft + vehicle.property("axleRatio") * wheel)) < 1e-3, (wheel, shaft)
    for name in ("frontDriveshaft", "rearDriveshaft"):
        assert abs(nodes[name].property("angle") - shaft) < 1e-5
    assert vehicle.property("crankAngle") == 0, "Engine must not turn with the wheels"
    # The engine slider spins crank and fan together, clockwise from the front.
    slider = visual_item(window.contentItem(), "jeepEngineRpm")
    target = slider.mapToScene(QPointF(slider.width() * 0.5, slider.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=target)
    assert 2000 < vehicle.property("engineRpm") < 4000, vehicle.property("engineRpm")
    QTest.qWait(150)
    crank = vehicle.property("crankAngle")
    assert circular(crank) < -5, crank
    assert abs(nodes["engineFan"].property("angle") - crank) < 1e-5
    assert abs(nodes["crankPulley"].property("angle") - crank) < 1e-5
    for n, node in nodes.items():
        assert (axis(node) - axes[n]).length() < 1e-5, n + " spin axis moved"
        assert (node.property("scenePosition") - positions[n]).length() < 1e-4, n + " orbited while spinning"
    vehicle.setProperty("engineRpm", 0)
    QTest.qWait(50)
    stopped = vehicle.property("crankAngle")
    QTest.qWait(100)
    assert vehicle.property("crankAngle") == stopped, "Stopped engine must hold its phase"
    # Live OBD takes over RPM and speed (MPH -> km/h) and locks the test sliders.
    obd.set_connected(True)
    obd.rpmChanged.emit(1800.0)
    obd.speedMPHChanged.emit(12.0)
    QTest.qWait(120)
    assert menu.property("liveDrive")
    assert vehicle.property("engineRpm") == 1800
    assert abs(vehicle.property("wheelSpeedKph") - 12 * 1.609344) < 1e-3
    assert not slider.property("enabled")
    assert not visual_item(window.contentItem(), "jeepSpinWheels").property("enabled")
    obd.set_connected(False)
    QTest.qWait(50)
    assert vehicle.property("engineRpm") == 0 and vehicle.property("wheelSpeedKph") == 0
    assert slider.property("enabled")
    for name in ("wheelSpinAngle", "driveshaftAngle", "crankAngle"):
        vehicle.setProperty(name, 0)
    vehicle.setProperty("hoodOpen", True)
    menu.setProperty("cameraYaw", -20)
    menu.setProperty("cameraPitch", -40)
    QTimer.singleShot(700, engine_bay)


@check
def engine_bay():
    capture("engine-bay")
    vehicle.setProperty("hoodOpen", False)
    menu.setProperty("cameraYaw", -35)
    menu.setProperty("cameraPitch", -25)
    vehicle.setProperty("steeringInput", -1)
    QTimer.singleShot(700, steering_right)


@check
def steering_right():
    left = window.findChild(QObject, "driverWheel").property("angle")
    right = window.findChild(QObject, "passengerWheel").property("angle")
    assert right < left < -15
    capture("steer-right")
    button = visual_item(window.contentItem(), "centerWheels")
    point = button.mapToScene(QPointF(button.width() / 2, button.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.LeftButton, pos=point)
    assert vehicle.property("steeringInput") == 0
    QTimer.singleShot(700, steering_centered)


@check
def steering_centered():
    assert abs(window.findChild(QObject, "driverWheel").property("angle")) < 0.01
    assert abs(window.findChild(QObject, "passengerWheel").property("angle")) < 0.01
    window.resize(600, 800)
    menu.setProperty("cameraPitch", -25)
    vehicle.setProperty("driverDoorOpen", True)
    QTimer.singleShot(700, narrow)


@check
def narrow():
    capture("narrow")
    print(
        "PASS: asset loading, IMU quaternion mapping, five hinges, rear interlock, four lighting circuits and tailgate lamp attachment, steering, wheel spin/reverse/stop, engine and driveshaft spin, live OBD drive, and narrow view"
    )
    app.quit()


QTimer.singleShot(5000, rear_sequence)
QTimer.singleShot(45000, lambda: app.exit(2))
result = app.exec()
if failures:
    raise failures[0]
sys.exit(result)
