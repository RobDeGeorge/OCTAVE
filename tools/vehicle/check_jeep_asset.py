"""Validate exported geometry, hinge placement and the camera's swept envelope."""

import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

# Optional argument: an alternative assets folder (e.g. a scratch build).
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "frontend/assets"
ENGINE_SPINNERS = ("crank_pulley", "engine_fan", "front_driveshaft", "rear_driveshaft")


def quaternion_matrix(w, x, y, z):
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


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
    orient = quaternion_matrix(*part.get("orient", [1, 0, 0, 0]))
    assembled.append(points @ orient.T + pivot)
    spins = name.endswith("_spin") or name in ENGINE_SPINNERS
    for angle in np.linspace(0, 360 if spins else part["openAngle"], 101):
        a = math.radians(angle)
        c, s = math.cos(a), math.sin(a)
        rotation = {
            "Y": np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]),
            "X": np.array([[1, 0, 0], [0, c, -s], [0, s, c]]),
            "Z": np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]]),
        }[part["axis"]]
        assert np.linalg.norm(points @ (orient @ rotation).T + pivot, axis=1).max() <= rig["bounds"]["sweptRadius"]
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
    if name in ENGINE_SPINNERS:
        # Spin axes are tilted shaft axes, all pointing forward (+Z).
        assert part["axis"] == "Z" and "orient" in part, name
        assert (orient @ [0, 0, 1])[2] > 0.85, name
    if name == "crank_pulley":
        assert any(n.startswith("Crank damper timing mark") for n in names)
    if name == "engine_fan":
        assert sum(n.startswith("Engine fan blade") for n in names) == 7
        assert any(n.startswith("Water pump pulley") for n in names)
    if name.endswith("driveshaft"):
        assert any(n.startswith("Driveshaft tube") for n in names)
        assert sum(n.startswith("Universal joint cross") for n in names) == 4
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
body = rig["parts"]["body"]["components"]
if "bodyLift" in rig:
    # Lifted soft-top twin (export_jeep_twin.py): its own shocks, no hardtop.
    assert sum(n.startswith("Fox style shock body") for n in body) == 4
    assert any(n.startswith("Soft top") for n in body)
    assert not any(n.startswith("Hardtop ") for n in body) and "rear_glass" not in rig["parts"]
else:
    assert sum(n.startswith("Shock absorber body") for n in body) == 4
for required in ("4.0 inline six cylinder block", "Cylinder head", "Valve cover", "Clutch bellhousing",
                 "NV3550 five speed case", "NP231 chain case", "Serpentine belt", "Exhaust downpipe"):
    assert any(n.startswith(required) for n in body), required
assert not any(n.startswith(("Engine block silhouette", "Transmission casing", "Engine oil pan")) for n in body)
print(
    "PASS: five opening clips and matching assembled parts, four shocks, engine and driveline spinners, "
    "finite geometry, indices and full hinge sweep bounds"
)
