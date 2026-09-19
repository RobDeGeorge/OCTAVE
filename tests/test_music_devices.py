"""Exercise actual glTF loading, playback lifecycle, and the shared preference API."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parent.parent


@pytest.mark.parametrize("fixture", ["MusicDevicesSmoke.qml", "MusicDeviceLoadingSmoke.qml", "MusicDeviceArtworkSmoke.qml", "MusicDeviceDetailsSmoke.qml"])
def test_music_device_scene_and_preferences(tmp_path, fixture):
    runner = r'''
import sys
from pathlib import Path
from PySide6.QtCore import QUrl
from PySide6.QtGui import QGuiApplication, QImage, QColor
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow
from backend.settings_manager import SettingsManager
app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
for name, color in (("cover-a", "#ec6354"), ("cover-b", "#43b6cf")):
    image = QImage(64, 64, QImage.Format_RGB32)
    image.fill(QColor(color))
    image.save(str(Path(sys.argv[2]) / (name + ".png")))
    engine.rootContext().setContextProperty(name.replace("-", "_"), QUrl.fromLocalFile(str(Path(sys.argv[2]) / (name + ".png"))))
settings = SettingsManager()
settings.save_setting("musicDeviceModel", "Album art")
engine.rootContext().setContextProperty("settingsManager", settings)
engine.rootContext().setContextProperty("testSettings", settings)
from backend.asset_store import AssetStore
asset_store = AssetStore(str(Path(sys.argv[3]) / "frontend" / "assets"))
engine.rootContext().setContextProperty("assetStore", asset_store)
# Catch import/type errors in both integration surfaces, not just the isolated scene.
for file in ("MediaRoom.qml", "NowPlayingStudio.qml", "settings/MediaSettingsPage.qml"):
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(Path.cwd() / "frontend" / file)))
    if component.isError():
        print(component.errors())
        sys.exit(3)
engine.exit.connect(app.exit)
engine.load(QUrl.fromLocalFile(sys.argv[1]))
if not engine.rootObjects(): sys.exit(4)
code = app.exec()
settings.flushPendingSave()
# Recreate the backend to verify the final selection survives a disk round trip.
settings.save_setting("musicDeviceModel", "Record player")
settings.flushPendingSave()
restored = SettingsManager()
assert restored.get_setting_with_default("musicDeviceModel", "Album art") == "Record player"
sys.exit(code)
'''
    result = subprocess.run(
        [sys.executable, "-c", runner, str(ROOT / "tests/fixtures" / fixture), str(tmp_path), str(ROOT)],
        cwd=ROOT,
        env=dict(os.environ, XDG_CONFIG_HOME=str(tmp_path), QT_QUICK_CONTROLS_STYLE="Basic", QSG_RHI_BACKEND="opengl"),
        capture_output=True, text=True, timeout=35,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "MUSIC_DEVICES_SMOKE_PASS" in output, output
    assert "ReferenceError" not in output and "Unable to assign" not in output, output
