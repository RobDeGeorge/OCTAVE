"""
Smoke tests: prove modules import and a QApplication can initialize headless.

These tests do not exercise behaviour — they only catch import-time crashes,
syntax errors, and top-level code that breaks on a clean environment.
Hardware-bound managers (OBD, ESP32, BerryIMU, Gesture, PhoneMirror, AndroidAuto)
are imported but not instantiated, since instantiation would probe for hardware.
"""

import importlib

import pytest
from PySide6.QtCore import QObject


BACKEND_MODULES = [
    "backend.logging_config",
    "backend.clock",
    "backend.settings_manager",
    "backend.media_manager",
    "backend.spotify_manager",
    "backend.audio_analyzer",
    "backend.network_manager",
    "backend.download_manager",
    "backend.obd_manager",
    "backend.esp32_volume_manager",
    "backend.berryimu_manager",
    "backend.gesture_manager",
    "backend.phone_mirror",
    "backend.android_auto",
    "backend.volume_utils",
    "backend.dashboard_manager",
    "backend.asset_store",
]


@pytest.mark.parametrize("module_name", BACKEND_MODULES)
def test_backend_module_imports(module_name):
    module = importlib.import_module(module_name)
    assert module is not None


def test_qapplication_initializes(qapp):
    assert qapp is not None


def test_volume_curve_math():
    """Guard the quadratic curve — if this changes, every audio output drifts."""
    from backend.volume_utils import to_linear

    assert to_linear(0) == pytest.approx(0.0)
    assert to_linear(100) == pytest.approx(1.0)
    assert to_linear(50) == pytest.approx(0.25)  # quadratic: (0.5)^2
    assert to_linear(10) == pytest.approx(0.01)
    # Out-of-range clamping
    assert to_linear(-5) == pytest.approx(0.0)
    assert to_linear(150) == pytest.approx(1.0)


def test_safe_managers_instantiate(qapp, tmp_path, monkeypatch):
    """
    Instantiate managers that don't touch hardware or audio devices.
    Redirect config dirs to a tmp path so the test doesn't write to the
    developer's real settings. MediaManager is excluded because QAudioOutput
    can hang on headless runners without a real audio backend.
    """
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "appdata"))

    from backend.settings_manager import SettingsManager
    from backend.audio_analyzer import AudioAnalyzer
    from backend.network_manager import NetworkManager

    settings = SettingsManager()
    assert isinstance(settings, QObject)

    analyzer = AudioAnalyzer()
    assert isinstance(analyzer, QObject)

    network = NetworkManager()
    assert isinstance(network, QObject)


def test_dashboard_manager_discovers_presets(qapp, tmp_path):
    """
    The Python DashboardManager must discover the same built-in presets that
    the C++ peer does (frontend/dashboards/presets/*.json). If a preset is
    added on the C++ side without landing a JSON file, this breaks.
    """
    import os

    from backend.dashboard_manager import DashboardManager

    repo_root = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..")
    )
    presets_dir = os.path.join(repo_root, "frontend", "dashboards", "presets")

    dm = DashboardManager()
    dm.setPresetsDir(presets_dir)
    dm.setUserDir(str(tmp_path / "dashboards"))

    ids = {entry["id"] for entry in dm.dashboards}
    assert "minimal" in ids
    assert all(entry["builtIn"] for entry in dm.dashboards)

    # Duplicate → save → delete round-trip.
    new_id = dm.duplicateDashboard("minimal", "Copy For Test")
    assert new_id == "copy-for-test"
    assert dm.deleteDashboard(new_id) is True
    assert dm.deleteDashboard("minimal") is False  # built-in is protected
