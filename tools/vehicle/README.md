# Photo-referenced 2003 Jeep TJ Sahara

`frontend/assets/jeep_tj_2003.glb` is an original model reconstructed manually
from photographs of one 2003 TJ Sahara: Shale Green paint, Dark Khaki hardtop,
full doors with lowered windows, Canyon wheels, 31-inch tires, and Khaki cabin.
The app's model uses silver metallic paint, as requested; the reference vehicle
remains Shale Green.

The [reference guide](../../docs/vehicle/references.md) records the selected
angles, the corrections they informed, and a manifest of all 164 downloaded
reference photos from the same vehicle. This is a photo-based reconstruction;
**dimensional 1:1 accuracy is not established**. The 2.372 m wheelbase is anchored
to published dimensions; surfaces and small details are interpreted from images.

All geometry and the studio light probe are generated locally from source under
the repository license. No third-party meshes or photographic textures are
bundled. Reference photography remains in the ignored `dev/jeep-tj-reference/`
working cache, separate from the app's assets.

## Rebuild / edit

Use Blender 4.2 (the app itself does not require Blender):

```sh
blender --background --python tools/vehicle/build_jeep_tj.py
blender --background --python tools/vehicle/build_jeep_tj.py -- \
  --blend /tmp/jeep-tj.blend --preview /tmp/jeep-tj.png
python3 tools/vehicle/build_studio_probe.py
```

Alternatively, run the model builder with Python 3.11 and `bpy==4.2.0` installed
in an isolated environment. `--output` overrides the GLB path. The optional
`.blend` preserves individually named editable parts before material batching.
`--preview` renders front-quarter, side, front, and rear-quarter PNGs.

Edit the `paint` and `roof` materials near the top of the builder for color
changes. The model includes its cabin, so the lowered door windows expose real
interior geometry. Windshield and rear glass use glTF alpha blending. This is
supported in the tested desktop Qt runtime; inspect transparency on target GPUs.

The exported GLB groups geometry by material and embeds its data. The separate
128 KiB `vehicle_studio.hdr` provides environment reflections so the enamel,
glass and metal remain readable as the vehicle rotates. It is generated with
standard-library Python and contains no external imagery.

## Coordinate contract

- GLB units: metres. +Y up, +Z forward, +X vehicle right.
- Origin: midway between the axles, 0.9 m above ground.
- QML scales metres by 30 and applies sensor rotation to the parent node.
- Pitch is around X, yaw around Y, roll around Z, matching the existing sensor
  quaternion mapping. Geometry contains no attitude correction transforms.
- No ground plane, camera, or lights are exported. Studio render fixtures are
  created after the GLB export.

## Validation

- GLB: 211,264 triangles, 13 material groups, 9,227,560 bytes.
- Structural checks: valid GLB header, finite vertices, valid index ranges,
  expected dimensions, no external geometry dependencies.
- Measured rotation envelope: 64.26 QML units; camera framing reserves 70.
- Actual `CarMenu.qml` loaded under Qt 6.11.1 / Xvfb with the GLB and HDR probe.
- Injected pitch, roll, and yaw quaternions matched the existing sensor mapping.
- Normal/narrow app views and four studio angles were visually inspected.
- Physical IMU and Android/Raspberry Pi performance have not been tested.

[Studio preview](../../docs/vehicle/jeep-tj-preview.png) ·
[In-app preview](../../docs/vehicle/jeep-tj-in-app.png) ·
[Side](../../docs/vehicle/jeep-tj-side.png) ·
[Front](../../docs/vehicle/jeep-tj-front.png) ·
[Rear](../../docs/vehicle/jeep-tj-rear.png)

[Reference photo board](../../docs/vehicle/reference-board.html)
