"""Validate exported geometry, hinge placement and the camera's swept envelope."""

import json
import math
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2] / "frontend/assets"


def load(path):
    data = path.read_bytes()
    assert struct.unpack_from("<4sII", data) == (b"glTF", 2, len(data))
    size = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20 : 20 + size])
    binary = data[28 + size :]

    def accessor(index):
        a = doc["accessors"][index]
        view = doc["bufferViews"][a["bufferView"]]
        dtype = {5126: "<f4", 5125: "<u4", 5123: "<u2"}[a["componentType"]]
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a["type"]]
        return np.frombuffer(
            binary, dtype=dtype, count=a["count"] * width, offset=view.get("byteOffset", 0) + a.get("byteOffset", 0)
        ).reshape(-1, width)

    points = []

    def visit(index, parent):
        node = doc["nodes"][index]
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        rotation = np.array(
            [
                [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
            ]
        )
        matrix = np.eye(4)
        matrix[:3, :3] = rotation @ np.diag(node.get("scale", [1, 1, 1]))
        matrix[:3, 3] = node.get("translation", [0, 0, 0])
        if "matrix" in node:
            matrix = np.array(node["matrix"]).reshape(4, 4).T
        matrix = parent @ matrix
        if "mesh" in node:
            for primitive in doc["meshes"][node["mesh"]]["primitives"]:
                pos = accessor(primitive["attributes"]["POSITION"])
                assert np.isfinite(pos).all()
                assert accessor(primitive["indices"]).max() < len(pos)
                points.append(pos @ matrix[:3, :3].T + matrix[:3, 3])
        for child in node.get("children", []):
            visit(child, matrix)

    for index in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(index, np.eye(4))
    return doc, np.concatenate(points)


rig = json.loads((ROOT / "jeep_tj/rig.json").read_text())
doc, full = load(ROOT / "jeep_tj_2003.glb")
assert {a["name"] for a in doc["animations"]} == {
    "Open_" + k for k in rig["parts"] if rig["parts"][k]["openAngle"] != 0
}
assembled = []
for name, part in rig["parts"].items():
    _, points = load(ROOT / "jeep_tj" / part["file"])
    pivot = np.array(part["pivot"])
    assembled.append(points + pivot)
    for angle in np.linspace(0, 360 if name.endswith("_spin") else part["openAngle"], 101):
        a = math.radians(angle)
        c, s = math.cos(a), math.sin(a)
        rotation = (
            np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
            if part["axis"] == "Y"
            else np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
        )
        assert np.linalg.norm(points @ rotation.T + pivot, axis=1).max() <= rig["bounds"]["sweptRadius"]
    names = part["components"]
    if name.endswith("_spin"):
        assert part["axis"] == "X"
        assert any(n.startswith("31 inch tire carcass") for n in names)
        assert any(n.startswith("16 inch black rim barrel") for n in names)
        assert not any(n.startswith(("Front brake caliper", "Steering knuckle", "Steering arm")) for n in names)
        if "front" in name:
            assert any(n.startswith("Front brake rotor") for n in names)
    if name in ("driver_wheel", "passenger_wheel"):
        assert any(n.startswith("Front brake caliper") for n in names)
        assert not any(n.startswith("31 inch tire carcass") for n in names)
    if name.endswith("door"):
        assert any(n.startswith("Door interior card") for n in names)
        assert any(n.startswith("Rounded mirror shell") for n in names)
    if name == "tailgate":
        assert any(n.startswith("Spare carrier") for n in names)
        assert any(n.startswith("Third brake light") for n in names)
joined = np.concatenate(assembled)
# Separate Qt assets must reconstruct the same closed model, without baked pivots twice.
assert len(full) == len(joined)
for axis in range(3):
    np.testing.assert_allclose(np.sort(full[:, axis]), np.sort(joined[:, axis]), atol=2e-6)
assert np.linalg.norm(joined, axis=1).max() <= rig["bounds"]["closedRadius"] + 1e-6
assert 3.8 < np.ptp(joined[:, 2]) < 4.2
assert rig["parts"]["driver_door"]["pivot"][0] > 0 > rig["parts"]["passenger_door"]["pivot"][0]
assert sum(n.startswith("Shock absorber body") for n in rig["parts"]["body"]["components"]) == 4
print(
    "PASS: five opening clips and matching assembled parts, four shocks, finite geometry, indices and full hinge sweep bounds"
)
