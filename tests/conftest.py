import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

import pytest
from PySide6.QtWidgets import QApplication


@pytest.fixture(scope="session")
def qapp():
    app = QApplication.instance() or QApplication(sys.argv)
    yield app


@pytest.fixture(autouse=True)
def _collect_on_main_thread():
    # Python's cyclic GC runs on whichever thread happens to allocate. A test's
    # leftover QObjects (QML roots, managers with live timers) collected on a
    # worker thread are destroyed off their thread: their timers stay
    # registered with the main event dispatcher and later fire into freed
    # memory (segfault in QTimerInfoList::activateTimers, PySide6 6.12).
    # Collect here, on the main thread, after every test.
    yield
    import gc
    gc.collect()
