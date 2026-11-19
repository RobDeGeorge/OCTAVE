"""Render the vehicle's picture from the real Qt scene: frontend/assets/jeep_tj/preview.png.

The picture is what the app shows where a live 3D scene would cost too much (the Home
screen's vehicle card and its "No vehicle connected" panel, through VehiclePicture.qml):
frontend/VehicleView.qml at its default camera, no level ring, on a transparent background,
cropped to the vehicle. Run it again after the model or the view's lighting changes.

Run from the checkout: QT_QPA_PLATFORM=xcb xvfb-run -a venv/bin/python tools/vehicle/render_preview.py
Requires PySide6 with Quick3D (pip install PySide6; not in requirements.txt), as check_jeep_view.py does.
"""

import sys
import time
from pathlib import Path

from PySide6.QtCore import QObject, QRect, Qt, QUrl, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "frontend/assets/jeep_tj/preview.png"
WIDTH = 900  # pixels, the long side of the saved picture


class AssetStore(QObject):
    """assetStore as JeepHinge uses it: a file:// URL under frontend/assets/."""

    @Slot(str, result=QUrl)
    def localUrl(self, relative_path):
        if not relative_path or ".." in relative_path:
            return QUrl()
        return QUrl.fromLocalFile(str(ROOT / "frontend/assets" / relative_path))


app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
assets = AssetStore()
engine.rootContext().setContextProperty("assetStore", assets)
engine.loadData(
    f'''import QtQuick
import QtQuick.Window
import "{(ROOT / "frontend").as_uri()}" as App
Window {{ width: 1600; height: 1200; visible: true; color: "transparent"
App.VehicleView {{ objectName: "view"; anchors.fill: parent; showLevelRing: false; interactive: false }}
}}'''.encode()
)
assert engine.rootObjects(), "Component failed to load"
window = engine.rootObjects()[0]
view = window.findChild(QQuickItem, "view")
deadline = time.monotonic() + 30
while not view.property("ready") and time.monotonic() < deadline:
    QTest.qWait(50)
assert view.property("ready"), view.property("error")
QTest.qWait(500)

result = view.grabToImage()
done = []
result.ready.connect(lambda: done.append(True))
while not done and time.monotonic() < deadline:
    QTest.qWait(20)
image = result.image()
assert not image.isNull(), "Nothing was rendered"

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
