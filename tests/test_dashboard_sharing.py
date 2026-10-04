"""Dashboard sharing (export / import) — Python DashboardManager.

The C++ peer (src/managers/dashboardmanager.cpp, sanitizeImportedSpec) applies
the same rules; keep both in step with these expectations.
"""
import json
import os

import pytest

from backend.dashboard_manager import DashboardManager, sanitize_imported_spec

PRESETS = os.path.join(os.path.dirname(__file__), "..", "frontend", "dashboards", "presets")


@pytest.fixture
def dm(qapp, tmp_path):
    m = DashboardManager()
    m.setPresetsDir(os.path.abspath(PRESETS))
    m.setUserDir(str(tmp_path / "dashboards"))
    return m


def test_sanitize_keeps_known_keys_and_clamps():
    raw = {
        "schema": 1, "id": "../../evil", "label": "  My   Dash  ", "source": "Evil.qml",
        "gridColumns": 4, "gridRows": 2, "extra": {"x": 1},
        "cells": [
            {"type": "BarGauge", "paramId": "RPM", "col": 9, "row": -3, "colSpan": 50, "rowSpan": 1,
             "props": {"warnAbove": 6000, "title": "Revs", "nested": {"a": 1}, "list": [1]},
             "onClicked": "Qt.quit()"},
            "not-a-cell",
            {"type": "", "col": 0},
            {"type": "DigitalReadout", "col": "2", "row": True},
        ],
    }
    spec, err = sanitize_imported_spec(raw)
    assert err == ""
    assert set(spec) == {"schema", "label", "gridColumns", "gridRows", "cells"}
    assert spec["label"] == "My Dash"
    bar, digital = spec["cells"]
    assert set(bar) == {"type", "paramId", "col", "row", "colSpan", "rowSpan", "props"}
    assert (bar["col"], bar["row"], bar["colSpan"]) == (3, 0, 1)      # clamped into a 4x2 grid
    assert bar["props"] == {"warnAbove": 6000, "title": "Revs"}        # scalars only
    assert (digital["col"], digital["row"]) == (0, 0)                  # strings / bools aren't numbers


@pytest.mark.parametrize("raw, reason", [
    ({"schema": 2, "cells": []}, "newer version"),
    ({"schema": 0, "cells": []}, "bad schema"),
    ({"label": "x"}, "no cells"),
    ({"cells": [{}] * 257}, "too many"),
    ([1, 2], "invalid JSON"),
])
def test_sanitize_rejects(raw, reason):
    spec, err = sanitize_imported_spec(raw)
    assert spec == {}
    assert reason in err


def test_export_import_round_trip(dm, tmp_path, monkeypatch):
    # Keep the export out of the real ~/Downloads (Qt reads user-dirs.dirs,
    # not the environment, so stub the lookup itself).
    import backend.dashboard_manager as mod

    class _Paths:
        StandardLocation = mod.QStandardPaths.StandardLocation

        @staticmethod
        def writableLocation(_loc):
            return str(tmp_path / "dl")

    monkeypatch.setattr(mod, "QStandardPaths", _Paths)
    path = dm.exportDashboard("minimal")
    assert path == str(tmp_path / "dl" / "OCTAVE-dashboards" / "minimal.json")
    original = json.load(open(path, encoding="utf-8"))

    new_id = dm.importDashboard(path)
    assert new_id and new_id != "minimal"           # never overwrites; built-in id regenerated
    assert dm.isBuiltIn(new_id) is False
    imported = dm.loadDashboard(new_id)
    assert imported["cells"] == sanitize_imported_spec(original)[0]["cells"]

    # Importing the same file again gets yet another id.
    again = dm.importDashboard(path)
    assert again not in ("", new_id)
    assert dm.lastShareError == ""


def test_import_failure_sets_error(dm, tmp_path):
    assert dm.importDashboardFromText("{not json") == ""
    assert "invalid JSON" in dm.lastShareError
    assert dm.importDashboard(str(tmp_path / "missing.json")) == ""
    assert dm.lastShareError == "Could not open the file"
