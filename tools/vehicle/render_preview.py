"""Render the vehicle's picture from the real Qt scene: frontend/assets/jeep_tj/preview.png.

The picture is what the app shows where a live 3D scene would cost too much (the Home
screen's vehicle card and its "No vehicle connected" panel, through VehiclePicture.qml):
frontend/VehicleView.qml at its default camera, no level ring, on a transparent background,
cropped to the vehicle. Run it again after the model or the view's lighting changes.

Run from the checkout: QT_QPA_PLATFORM=xcb xvfb-run -a venv/bin/python tools/vehicle/render_preview.py
With a bundled vehicle's id (render_preview.py bmw-e46-330i-zhp) it renders that vehicle's picture,
frontend/assets/vehicles/<id>/preview.png: from its rig (rig.json, through RigVehicle.qml) or, without
one, from its model.glb. Check pictures instead of the preview: --out FILE writes there, uncropped
(--open with the panels open, --lights with the lamps on, --steer -1..1 with the front wheels turned,
--yaw / --pitch DEGREES for the camera).
Requires PySide6 with Quick3D (pip install PySide6; not in requirements.txt), as check_jeep_view.py does.
"""

import json
import sys
import time
from pathlib import Path

from PySide6.QtCore import QObject, QRect, Qt, QUrl, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[2]
# With an argument, a bundled model: frontend/assets/vehicles/<id>/model.glb -> its preview.png
def option(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


VALUES = {sys.argv[i + 1] for i, a in enumerate(sys.argv[:-1]) if a in ("--out", "--yaw", "--pitch", "--steer")}
BUNDLED = next((a for a in sys.argv[1:] if not a.startswith("-") and a not in VALUES), "")
FOLDER = ROOT / (f"frontend/assets/vehicles/{BUNDLED}" if BUNDLED else "frontend/assets/jeep_tj")
CHECK = option("--out")
OUT = Path(CHECK) if CHECK else FOLDER / "preview.png"
if not BUNDLED:
    SPEC = ""
elif (FOLDER / "rig.json").exists():
    SPEC = f'spec: ({{ id: "{BUNDLED}", rigged: true, rig: "vehicles/{BUNDLED}" }}); '
else:
    SPEC = 'spec: ({ rigged: false, modelUrl: "' + (FOLDER / "model.glb").as_uri() + '", yaw: 0, tilt: 0 }); '
CAMERA = "".join(f"{prop}: {float(option(flag))}; " for flag, prop in (("--yaw", "cameraYaw"), ("--pitch", "cameraPitch"))
                 if option(flag) is not None)
WIDTH = 900  # pixels, the long side of the saved picture


class AssetStore(QObject):
    """assetStore as JeepHinge uses it: a file:// URL under frontend/assets/."""

    @Slot(str, result=QUrl)
    def localUrl(self, relative_path):
        if not relative_path or ".." in relative_path:
            return QUrl()
        return QUrl.fromLocalFile(str(ROOT / "frontend/assets" / relative_path))

    @Slot(str, result="QVariantMap")
    def readJson(self, relative_path):
        path = ROOT / "frontend/assets" / relative_path
        return json.loads(path.read_text()) if ".." not in relative_path and path.exists() else {}


app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
assets = AssetStore()
engine.rootContext().setContextProperty("assetStore", assets)
engine.loadData(
    f'''import QtQuick
import QtQuick.Window
import "{(ROOT / "frontend").as_uri()}" as App
Window {{ width: 1600; height: 1200; visible: true; color: "transparent"
App.VehicleView {{ objectName: "view"; anchors.fill: parent; {SPEC}{CAMERA}showLevelRing: false; interactive: false }}
}}'''.encode()
)
assert engine.rootObjects(), "Component failed to load"
window = engine.rootObjects()[0]
view = window.findChild(QQuickItem, "view")
deadline = time.monotonic() + 30
while not view.property("ready") and time.monotonic() < deadline:
    QTest.qWait(50)
assert view.property("ready"), view.property("error")
vehicle = view.property("vehicle")
if "--open" in sys.argv:
    vehicle.openAll()
if option("--steer") is not None:
    vehicle.setProperty("steeringInput", float(option("--steer")))
if "--lights" in sys.argv:
    for circuit in ("headlightsOn", "fogLightsOn", "brakeLightsOn", "reverseLightsOn"):
        vehicle.setProperty(circuit, True)
QTest.qWait(1200 if "--open" in sys.argv else 500)

result = view.grabToImage()
done = []
result.ready.connect(lambda: done.append(True))
while not done and time.monotonic() < deadline:
    QTest.qWait(20)
image = result.image()
assert not image.isNull(), "Nothing was rendered"

if CHECK:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    image = image.scaled(1000, 750, Qt.KeepAspectRatio, Qt.SmoothTransformation)
    assert image.save(str(OUT)), OUT
    print(f"Wrote {OUT}")
    sys.exit(0)

# Crop to the pixels the vehicle covers
left, top, right, bottom = image.width(), image.height(), -1, -1
for y in range(image.height()):
    for x in range(image.width()):
        if image.pixelColor(x, y).alpha() > 8:
            left, right = min(left, x), max(right, x)
            top, bottom = min(top, y), max(bottom, y)
assert right >= left, "The render is empty"
image = image.copy(QRect(left, top, right - left + 1, bottom - top + 1))
image = image.scaled(WIDTH, WIDTH, Qt.KeepAspectRatio, Qt.SmoothTransformation)
assert image.save(str(OUT)), OUT
print(f"Wrote {OUT.relative_to(ROOT)} ({image.width()}x{image.height()}, {OUT.stat().st_size // 1024} KiB)")
sys.exit(0)
