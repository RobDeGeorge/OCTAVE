# Photo-referenced 2003 Jeep TJ Sahara

`frontend/assets/jeep_tj_2003.glb` is an original model reconstructed manually
from photographs of one 2003 TJ Sahara: Shale Green paint, Dark Khaki hardtop,
full doors with lowered windows, Canyon wheels, 31-inch tires, and Khaki cabin.
The app model uses silver metallic paint, a black hardtop, black upholstery,
textured black Sport-style flares, left-hand-drive controls, and 31-inch
KO2-inspired tires on 16-inch satin-black eight-spoke rims, as requested.
The spare matches the road wheels. The reference vehicle remains Shale Green.

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

The full GLB includes five opening hinges/clips, two steering-knuckle pivots, four axle-spin pivots,
and a neutral steering-link assembly.
Geometry is batched by material within each part. `assets/jeep_tj/` contains separate
part-local GLBs, plus generated `Rig.js` and `rig.json` pivot/bounds metadata.
`JeepVehicle.qml` assembles these parts using public Qt APIs, avoiding imported
scene internals. Rebuild and distribute the complete generated asset set together.

The view provides controls for both front doors, hood, rear glass, tailgate and
headlights. “Open all” opens all five hinged panels for inspection, preserving
the rear-glass/tailgate sequence; “Close all” shuts them again. Mirrors and interior cards follow their doors; the spare and third
brake light follow the tailgate. The tailgate opens before the rear glass and
closes after it. Headlights combine visible emissive lenses with spotlights.
Door jambs follow only the sides and sill, leaving the entry unobstructed. Front
seats have floor feet, bolted brackets and slide rails; the rear bench has fixed
support legs and rails. The pedals hang beneath the driver dashboard, forward of
the seat. The left rear fuel filler sits clear of the flare, including its bezel. The
hood has a shallow deck crown and narrow shoulder roll; the cowl sides are flat
and flush with the door jambs, with a separate firewall behind the hinged hood.
The tub floor uses thin footwell pans, a raised transmission tunnel, rear wheel
housings, a stepped cargo floor, reinforcing ribs and body mounts. Two recessed
cupholders sit between the shifter and armrest alongside the parking brake.

The fenders have tapered ends and closed, rolled cross-sections. Exterior skins
have thickness at the rear quarters, windshield and hardtop. Curved corner
returns join the rear tub and hardtop panels; recessed door flanges and seals
back the shut lines without closing the door apertures. Windshield side returns
connect the raked frame to the front door pillars. The rear roof sections meet
the hardtop corner and rear-window surround. Front and rear
Dana 44 carriers have angular ten-bolt covers, fill plugs and pinion housings;
the front carrier is offset toward the driver and the rear is centered.

The steering slider drives the pitman arm and solves two fixed-length constraints
for the drag link and bent tie rod in an inverted-Y layout. Each wheel, brake and
steering arm rotates together about its knuckle; the inside wheel turns farther.
The Spin wheels button and signed speed slider drive all four road wheels in
forward or reverse, using the 31-inch tire circumference. Stopping holds the
current rotation. Front tire/rim/hub/rotor parts spin under their steering
pivots; calipers and steering arms only steer. Rear wheels spin about their
axle centers, and the spare stays fixed to its carrier. All four wheels use
the same test speed; differential speed in turns is not simulated. The full
GLB exports separate pivots; Qt composes the front steering/spin hierarchy.
The damper telescopes between its axle bracket and tie-rod clamp. Hardpoints in
`steering_geometry.json` are approximate visual dimensions. Suspension travel,
load-dependent steering and cabin steering-wheel rotation are not animated.
The full GLB contains the links in their neutral pose; Qt replaces that part with
`JeepLink.qml` geometry driven by `JeepSteering.js` for live steering.

The 3D-view toolbar independently toggles headlights, fog lights, brake lights
and reverse lights. `JeepLamp.qml` adds emissive lens surfaces and directional
light spill for the fog, brake and reverse circuits. The high-mounted brake
lamp is a child of the tailgate hinge; the two lower brake/reverse assemblies
stay attached to the tub. These are manual test controls; hardware brake/gear
inputs are not connected.

The five tires use a 0.3937 m tread radius (31 inches overall), interlocking
siped blocks, shoulder armor and raised black BFGoodrich / KO2 lettering.
The black rims use a 0.2032 m bead-seat radius (16 inches), with larger outer
protective lips, connected barrels, eight spokes and five lug nuts. This is a
photo-interpreted tread at the requested dimensions, not a retail tire SKU.

The shocks are detailed static geometry, including bodies, chrome pistons, boots,
bushings and mounts; suspension travel is not animated.

The exported GLBs embed their data. The separate
128 KiB `vehicle_studio.hdr` provides environment reflections so the enamel,
glass and metal remain readable as the vehicle rotates. It is generated with
standard-library Python and contains no external imagery.

## Coordinate contract

- GLB units: metres. +Y up, +Z forward, +X vehicle left.
- Origin: midway between the axles, 0.9 m above ground.
- QML scales metres by 30 and applies sensor rotation to the parent node.
- Pitch is around X, yaw around Y, roll around Z, matching the existing sensor
  quaternion mapping. Geometry contains no attitude correction transforms.
- Door hinges rotate around +Y; hood and rear glass rotate around +X.
- No ground plane, camera, or lights are exported. Studio render fixtures are
  created after the GLB export.

## Validation

```sh
node tools/vehicle/check_jeep_steering.cjs
venv/bin/python tools/vehicle/check_jeep_asset.py
QT_QPA_PLATFORM=xcb xvfb-run -a venv/bin/python tools/vehicle/check_jeep_view.py
```

- Panel-fit check: `check_jeep_panel_fit.py` uses the builder’s bpy environment
  and saved `.blend` to check closed return edges and moving-panel clearance.
- Steering checks: 201 input positions, constant rod lengths, monotonic angles,
  inside-wheel ordering and bounded input. Inspected 21 poses for tire/body contact.
- Asset checks: finite vertices, valid indices, matching full/assembled geometry,
  five animation clips, attached door components, four shocks, and swept bounds.
- Camera fitting uses the generated closed/swept radius, including moving panels.
- Real `CarMenu.qml` tested with Qt 6.11.1 / Xvfb: five animated hinges, rear-opening
  interlock in both directions, all four light circuits, moving third brake lamp,
  and sensor quaternion mapping.
- Closed, open, underside, and narrow views captured; studio views regenerated.
- Physical IMU and Android/Raspberry Pi performance have not been tested.

[Studio preview](../../docs/vehicle/jeep-tj-preview.png) ·
[In-app preview](../../docs/vehicle/jeep-tj-in-app.png) ·
[Side](../../docs/vehicle/jeep-tj-side.png) ·
[Front](../../docs/vehicle/jeep-tj-front.png) ·
[Rear](../../docs/vehicle/jeep-tj-rear.png) ·
[Open panels and seat mounts](../../docs/vehicle/jeep-tj-open.png) ·
[Underside](../../docs/vehicle/jeep-tj-underside.png) ·
[Steering](../../docs/vehicle/jeep-tj-steering.png) ·
[Wheel rotation](../../docs/vehicle/jeep-tj-wheel-spin.png) ·
[Brake and reverse lights](../../docs/vehicle/jeep-tj-lights.png) ·
[Console detail — roof and doors hidden for inspection](../../docs/vehicle/jeep-tj-console.png)

[Reference photo board](../../docs/vehicle/reference-board.html)

The Tailgate test button controls the complete rear-opening sequence: the gate
finishes opening before the glass rises; on closing, the glass finishes
lowering before the gate shuts. Completion checks include animation state,
and the app test samples both stages and interrupted/reversed requests.
