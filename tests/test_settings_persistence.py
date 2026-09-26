"""Settings must survive a power cut: a truncated settings file is restored
from the last known-good backup instead of being replaced with defaults."""

import json
import os


def _isolate(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "appdata"))


def test_corrupt_settings_restored_from_backup(qapp, tmp_path, monkeypatch):
    _isolate(tmp_path, monkeypatch)
    from backend.settings_manager import SettingsManager

    first = SettingsManager()
    settings_file = first.settings_file
    settings = first.load_settings()
    settings["startUpVolume"] = 0.37
    first.save_settings(settings)
    first.flushPendingSave()
    del first

    # Second boot reads the good file and backs it up
    SettingsManager()
    assert os.path.exists(settings_file + ".bak")

    # Power cut mid-write: file truncated
    with open(settings_file, "w") as f:
        f.write('{"startUpVol')

    third = SettingsManager()
    assert third.load_settings()["startUpVolume"] == 0.37
    assert os.path.exists(settings_file + ".corrupt")
    with open(settings_file) as f:
        assert json.load(f)["startUpVolume"] == 0.37


def test_failed_write_leaves_existing_file_intact(qapp, tmp_path, monkeypatch):
    _isolate(tmp_path, monkeypatch)
    from backend.settings_manager import SettingsManager

    manager = SettingsManager()
    with open(manager.settings_file) as f:
        before = f.read()

    # Unserialisable value: the write fails partway, as on a full disk
    manager._write_settings_to_disk({"bad": object()})

    with open(manager.settings_file) as f:
        assert f.read() == before
    leftovers = [n for n in os.listdir(os.path.dirname(manager.settings_file))
                 if n.startswith("tmp") and n.endswith(".json")]
    assert leftovers == []
