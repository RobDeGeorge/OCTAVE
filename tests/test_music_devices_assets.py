"""Budget checks for the generated music device assets.

The GLBs come from tools/music_devices/generate.py. These tests keep a
regeneration from quietly reintroducing the costs that were optimised away:
oversized triangle counts, 32-bit index buffers, per-label textures instead of
one atlas, images that no material references, and a light probe whose RLE
never compresses. They only parse the files, so they need neither Qt nor the
modelling dependencies.
"""
import json
import struct
from pathlib import Path

import pytest

ASSETS = Path(__file__).resolve().parent.parent / "frontend/assets/music_devices"
GLBS = sorted(ASSETS.glob("*.glb"))
# Generous ceilings (roughly 1.5x the generated counts) so honest modelling
# changes pass while a regression to the old geometry does not.
TRIANGLE_BUDGET = {
    "record-player": 9000, "record": 2000, "tonearm": 1000,
    "cassette-player": 4000, "cassette": 1200, "cassette-door": 1000, "reel": 600, "tape-pack": 800,
    "cd-player": 4000, "cd": 1800, "cd-lid": 1200,
    "mp3-player": 5000, "ipod": 3500,
}
FILE_BUDGET_KIB = 200
COMPONENT_BYTES = {5121: 1, 5123: 2, 5125: 4, 5126: 4}
COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def load_glb(path):
    data = path.read_bytes()
    magic, version, length = struct.unpack("<III", data[:12])
    assert magic == 0x46546C67 and version == 2 and length == len(data), path.name
    offset, chunks = 12, {}
    while offset < length:
        chunk_length, chunk_type = struct.unpack("<II", data[offset:offset + 8])
        chunks[chunk_type] = data[offset + 8:offset + 8 + chunk_length]
        offset += 8 + chunk_length
    return json.loads(chunks[0x4E4F534A]), chunks[0x004E4942]


def primitives(doc):
    for mesh in doc["meshes"]:
        yield from mesh["primitives"]


@pytest.mark.parametrize("path", GLBS, ids=[p.stem for p in GLBS])
def test_geometry_budget(path):
    doc, blob = load_glb(path)
    assert path.stem in TRIANGLE_BUDGET, f"add a budget for {path.name}"
    triangles = 0
    for primitive in primitives(doc):
        indices = doc["accessors"][primitive["indices"]]
        vertices = doc["accessors"][primitive["attributes"]["POSITION"]]["count"]
        assert indices["count"] % 3 == 0
        triangles += indices["count"] // 3
        # 16-bit indices unless the part genuinely needs more vertices.
        assert indices["componentType"] == (5125 if vertices > 65535 else 5123), path.name
        for accessor_index in (primitive["indices"], *primitive["attributes"].values()):
            accessor = doc["accessors"][accessor_index]
            view = doc["bufferViews"][accessor["bufferView"]]
            needed = accessor["count"] * COMPONENT_BYTES[accessor["componentType"]] * COMPONENTS[accessor["type"]]
            assert view["byteOffset"] + needed <= len(blob), f"{path.name}: accessor past buffer end"
    assert triangles <= TRIANGLE_BUDGET[path.stem], f"{path.name}: {triangles} triangles"
    assert path.stat().st_size <= FILE_BUDGET_KIB * 1024, f"{path.name}: {path.stat().st_size // 1024} KiB"


@pytest.mark.parametrize("path", GLBS, ids=[p.stem for p in GLBS])
def test_materials_and_textures_are_all_used(path):
    doc, _ = load_glb(path)
    used_materials = {primitive["material"] for primitive in primitives(doc)}
    assert used_materials == set(range(len(doc["materials"]))), f"{path.name}: unused materials"
    used_textures = {
        material["pbrMetallicRoughness"]["baseColorTexture"]["index"]
        for material in doc["materials"] if "baseColorTexture" in material["pbrMetallicRoughness"]
    }
    assert used_textures == set(range(len(doc.get("textures", [])))), f"{path.name}: unused textures"
    used_images = {doc["textures"][t]["source"] for t in used_textures}
    assert used_images == set(range(len(doc.get("images", [])))), f"{path.name}: unused images"
    # Every printed decal in a part shares one atlas, so at most one blended,
    # textured material exists per file and every material owns one draw call.
    decals = [m for m in doc["materials"] if m.get("alphaMode") == "BLEND" and "baseColorTexture" in m["pbrMetallicRoughness"]]
    assert len(decals) <= 1, f"{path.name}: {len(decals)} separate decal materials"
    assert len(doc["meshes"]) == len(doc["materials"]), f"{path.name}: meshes not batched by material"


def test_light_probe_and_masks():
    probe = (ASSETS / "studio.hdr").read_bytes()
    assert probe.startswith(b"#?RADIANCE")
    assert b"-Y 128 +X 256\n" in probe, "probe resolution changed; update MusicDeviceScene expectations"
    assert len(probe) < 64 * 1024, f"studio.hdr is {len(probe) // 1024} KiB; RLE is not compressing"
    for name in ("mask-record.png", "mask-cd.png"):
        mask = (ASSETS / name).read_bytes()
        assert mask.startswith(b"\x89PNG"), name
        width, height = struct.unpack(">II", mask[16:24])
        assert (width, height) == (512, 512), name
