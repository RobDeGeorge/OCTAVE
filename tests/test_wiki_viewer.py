"""Exercise the real About button and offline browser in a separate Qt process."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parent.parent


@pytest.mark.parametrize("available", [True, False])
def test_offline_wiki_navigation_and_lifecycle(available):
    runner = r'''
import sys
from PySide6.QtCore import QUrl, QObject, Slot, Qt, QPointF
from PySide6.QtTest import QTest
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickWindow
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtWebEngineQuick import QtWebEngineQuick
QtWebEngineQuick.initialize()
app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
class Probe(QObject):
    @Slot()
    def passTest(self):
        # Qt's system message handler may send console.log to the journal.
        print("WIKI_SMOKE_PASS", flush=True)

    @Slot(str, result=QObject)
    def find(self, name):
        return engine.rootObjects()[0].findChild(QObject, name)

    @Slot(float, float)
    def click(self, x, y):
        position = QPointF(x, y).toPoint()
        QTest.mouseClick(engine.rootObjects()[0], Qt.LeftButton, Qt.NoModifier, position)
probe = Probe()
context = engine.rootContext()
context.setContextProperty("testProbe", probe)
context.setContextProperty("width", 1100)
context.setContextProperty("height", 760)
context.setContextProperty("settingsManager", None)
context.setContextProperty("networkManager", None)
context.setContextProperty("wikiViewerAvailable", sys.argv[3] == "True")
context.setContextProperty("wikiHomeUrl", QUrl.fromLocalFile(sys.argv[1]))
engine.exit.connect(app.exit)
engine.load(QUrl.fromLocalFile(sys.argv[2]))
if not engine.rootObjects():
    sys.exit(3)
sys.exit(app.exec())
'''
    env = dict(os.environ, QT_QUICK_CONTROLS_STYLE="Basic")
    # This standalone host must use the same browser rendering policy as
    # the native and Python entry points, including on a real Wayland desktop.
    if sys.platform.startswith("linux"):
        env["QTWEBENGINE_CHROMIUM_FLAGS"] = (
            env.get("QTWEBENGINE_CHROMIUM_FLAGS", "") + " --disable-gpu"
        ).strip()
    result = subprocess.run(
        [sys.executable, "-c", runner, str(ROOT / "wiki/index.html"), str(ROOT / "tests/fixtures/WikiSmoke.qml"), str(available)],
        env=env,
        capture_output=True, text=True, timeout=45,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "WIKI_SMOKE_PASS" in output, output
