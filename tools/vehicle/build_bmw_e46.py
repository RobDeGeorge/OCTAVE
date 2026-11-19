"""Build a BMW E46 330i ZHP sedan (2003-2005) as a rigged model for OCTAVE's vehicle view.

    blender -b -P tools/vehicle/build_bmw_e46.py                      # writes the rig
    blender -b -P tools/vehicle/build_bmw_e46.py -- --render DIR      # and check pictures

Output: frontend/assets/vehicles/bmw-e46-330i-zhp/ (a .glb per moving part and rig.json),
one of the vehicles the app ships with (kBuiltIns in src/managers/vehiclemanager.cpp),
assembled by frontend/RigVehicle.qml. Render its picture afterwards:
    xvfb-run -a venv/bin/python tools/vehicle/render_preview.py bmw-e46-330i-zhp

How it is made (the machinery is carkit.py, shared with any other car built this way):
  - The body is one shell lofted through cross-sections along the car (STATIONS), each
    built by section() from four curves of the side view: the underside, the shoulder
    line, the top line down the middle, and the half-width seen from above.
  - The shell is then cut along outlines drawn in the side / front / top view into the
    four doors, the hood and the trunk lid (each with a shut line round it), and into
    glass. Panels and the cabin get an inner skin, so a door has a back and an edge and
    the opening it leaves has a jamb.
  - Inside: floor, seats, dashboard, steering wheel, console, parcel shelf, an engine bay
    under the hood and a boot under the lid, from boxes and lathed shapes.
  - Lamps, grilles and badges are flat shapes laid onto the panel they belong to.
  - Dimensions are the E46 sedan's: 4471 long, 1739 wide, 2725 wheelbase, 1481 / 1488
    track, about 1400 high on the ZHP's springs, 225/40 and 255/35 R18 tyres.

Coordinates: s = metres back from the front bumper, lat = metres to the car's left,
z = metres up from the road (see carkit.py).
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import carkit as kit  # noqa: E402

LENGTH = 4.471
HALF_WIDTH = 0.8695
FRONT_AXLE = 0.752
REAR_AXLE = FRONT_AXLE + 2.725
TRACK_FRONT = 1.481 / 2
TRACK_REAR = 1.488 / 2
TYRE_RADIUS = 0.318
OUT = Path(__file__).resolve().parents[2] / "frontend/assets/vehicles/bmw-e46-330i-zhp"
F = kit.Frame(LENGTH)

# ── Curves of the side and top views ────────────────────────────────────────

# The top of the car down its middle: bumper, hood, windshield, roof, rear glass, deck
top_line = kit.curve([(0, 0.665), (0.04, 0.72), (0.14, 0.765), (0.5, 0.855), (1.0, 0.93), (1.36, 0.965),
                      (1.46, 1.01), (1.75, 1.20), (2.02, 1.335), (2.25, 1.39), (2.62, 1.405), (3.02, 1.385),
                      (3.24, 1.335), (3.55, 1.185), (3.80, 1.07), (3.90, 1.05), (4.30, 1.05), (4.40, 1.035),
                      (4.45, 0.97), (LENGTH, 0.80)])
# Where the side turns into the hood, the window sill and the deck
shoulder = kit.curve([(0, 0.62), (0.1, 0.70), (0.5, 0.80), (1.0, 0.87), (1.4, 0.90), (2.5, 0.93),
                      (3.6, 0.955), (4.0, 0.975), (4.35, 0.975), (4.44, 0.93), (LENGTH, 0.78)])
underside = kit.curve([(0, 0.21), (0.25, 0.175), (3.85, 0.175), (4.25, 0.26), (LENGTH, 0.33)])
# Half the width seen from above: the nose and tail taper, the middle is barrelled
half_width = kit.curve([(0, 0.67), (0.06, 0.745), (0.25, 0.83), (0.6, 0.860), (1.4, HALF_WIDTH), (3.0, HALF_WIDTH),
                        (3.8, 0.857), (4.2, 0.815), (4.40, 0.735), (LENGTH, 0.64)])

ROOF_HALF_WIDTH = 0.615


def cabin(s):
    """0 over the hood and the deck, 1 under the roof: how far the cabin has risen here."""
    return kit.smoothstep((top_line(s) - shoulder(s) - 0.06) / 0.38)


def section(s):
    """Half a cross-section at s, from the middle of the underside round to the middle
    of the top: (lat, z) points. The other half is its mirror."""
    w, zb, zs, zt, f = half_width(s), underside(s), shoulder(s), top_line(s), cabin(s)
    rise = zt - zs
    edge = (1 - f) * 0.86 * w + f * ROOF_HALF_WIDTH   # roof edge / hood edge
    return [
        (0.0, zb),
        (w * 0.80, zb),
        (w * 0.975, zb + 0.05),
        (w, zb + 0.32 * (zs - zb)),
        (w, zb + 0.66 * (zs - zb)),
        (w * 0.985, zs - 0.045),
        (w * 0.955, zs),
        (w * (0.925 - 0.01 * f), zs + 0.014),      # window sill
        (edge, zs + 0.87 * rise),
        (edge * 0.88, zs + 0.958 * rise),          # a tight roof rail, then a nearly flat roof
        (edge * 0.50, zs + 0.992 * rise),
        (0.0, zt),
    ]


STATIONS = [0.0, 0.03, 0.12, 0.3, 0.5, FRONT_AXLE, 1.0, 1.2, 1.36, 1.46, 1.6, 1.8, 2.0, 2.15, 2.3, 2.6,
            2.9, 3.1, 3.25, 3.4, 3.6, 3.78, 3.88, 4.0, 4.2, 4.35, 4.43, LENGTH]

# ── Materials ───────────────────────────────────────────────────────────────

# Slots every part of the shell shares, in this order
PAINT, LINER, GLASS, SHUT, TRIM, CARD, HEADLINER, UNDER = range(8)


def make_materials():
    m = {
        "paint": kit.material("paint_imola_red", (0.50, 0.012, 0.022), 0.22),
        "liner": kit.material("arch_liner", (0.012, 0.012, 0.014), 0.8),
        "glass": kit.material("glass", (0.02, 0.03, 0.035), 0.05, alpha=0.38),
        "shut": kit.material("shut_line", (0.008, 0.006, 0.006), 0.7),
        "black": kit.material("black_trim", (0.012, 0.012, 0.014), 0.55),
        "card": kit.material("door_card", (0.035, 0.035, 0.038), 0.75),
        "headliner": kit.material("headliner", (0.30, 0.30, 0.31), 0.9),
        "under": kit.material("panel_underside", (0.16, 0.008, 0.012), 0.5),
        "rubber": kit.material("tyre", (0.02, 0.02, 0.022), 0.85),
        "rim": kit.material("rim", (0.62, 0.63, 0.65), 0.28, 0.9),
        "disc": kit.material("brake", (0.16, 0.16, 0.17), 0.5, 0.6),
        "chrome": kit.material("chrome", (0.85, 0.85, 0.86), 0.08, 1.0),
        "lens": kit.material("headlight", (0.80, 0.83, 0.86), 0.08, 0.3),
        "projector": kit.material("projector", (0.30, 0.32, 0.35), 0.1, 0.9),
        "red": kit.material("tail_red", (0.20, 0.0, 0.002), 0.10),
        "clear": kit.material("tail_clear", (0.78, 0.76, 0.74), 0.12),
        "amber": kit.material("amber", (0.9, 0.36, 0.02), 0.2),
        "plate": kit.material("plate", (0.86, 0.86, 0.84), 0.5),
        "badge": kit.material("badge", (0.10, 0.25, 0.55), 0.2),
        "leather": kit.material("leather_black", (0.022, 0.022, 0.024), 0.55),
        "carpet": kit.material("carpet", (0.03, 0.03, 0.032), 0.95),
        "dash": kit.material("dashboard", (0.028, 0.028, 0.03), 0.7),
        "alu": kit.material("aluminium", (0.55, 0.56, 0.58), 0.35, 0.9),
        "engine": kit.material("engine_cover", (0.05, 0.05, 0.055), 0.45),
        "glass_dark": kit.material("glass_dark", (0.004, 0.005, 0.006), 0.08),
        "tank": kit.material("tank", (0.72, 0.70, 0.62), 0.45),
    }
    m["shell"] = [m["paint"], m["liner"], m["glass"], m["shut"], m["black"], m["card"], m["headliner"], m["under"]]
    return m


# ── The shell and its parts ─────────────────────────────────────────────────

BODY, HOOD, TRUNK, DOOR_FL, DOOR_FR, DOOR_RL, DOOR_RR = range(7)
PART_NAMES = {BODY: "body", HOOD: "hood", TRUNK: "trunk", DOOR_FL: "door_front_left", DOOR_FR: "door_front_right",
              DOOR_RL: "door_rear_left", DOOR_RR: "door_rear_right"}
PAINTED, IS_GLASS, IS_TRIM = 0, 1, 2

SILL = 0.235
FRONT_DOOR = [(1.50, SILL), (2.505, SILL), (2.505, 1.322), (2.23, 1.325), (2.06, 1.285), (1.84, 1.150),
              (1.60, 0.935), (1.56, 0.90)]
FRONT_DOOR_GLASS = [(1.66, 0.958), (2.465, 0.972), (2.465, 1.300), (2.23, 1.303), (2.07, 1.262), (1.86, 1.125)]
REAR_DOOR = [(2.515, SILL), (3.02, SILL), (3.37, 0.72), (3.385, 0.99), (3.385, 1.262), (3.20, 1.305), (2.515, 1.322)]
REAR_DOOR_GLASS = [(2.555, 0.974), (3.335, 0.990), (3.335, 1.245), (3.20, 1.283), (2.555, 1.300)]
QUARTER_GLASS = [(3.42, 0.994), (3.59, 0.998), (3.645, 1.052), (3.42, 1.210)]
B_PILLAR_FRONT = [(2.465, 0.972), (2.505, 0.972), (2.505, 1.300), (2.465, 1.300)]
B_PILLAR_REAR = [(2.515, 0.974), (2.555, 0.974), (2.555, 1.300), (2.515, 1.300)]
HOOD_TOP = [(-0.60, 0.16), (0.60, 0.16), (0.70, 1.375), (-0.70, 1.375)]
TRUNK_TOP = [(-0.62, 3.925), (0.62, 3.925), (0.60, LENGTH + 0.2), (-0.60, LENGTH + 0.2)]
TRUNK_REAR = [(-0.60, 0.80), (0.60, 0.80), (0.60, 1.25), (-0.60, 1.25)]
WINDSHIELD = [(-0.665, 1.02), (0.665, 1.02), (0.525, 1.338), (-0.525, 1.338)]
REAR_GLASS = [(-0.62, 1.10), (0.62, 1.10), (0.51, 1.335), (-0.51, 1.335)]
COWL = [(-0.70, 1.380), (0.70, 1.380), (0.70, 1.445), (-0.70, 1.445)]
GAP = 0.0045


def build_shell(mats):
    """The lofted shell, cut into the body and its panels: ({part: object}, the uncut
    skin as an object to lay trim on, the same as a kit.Surface)."""
    body = kit.loft(F, STATIONS, section, creases=((2, 0.5), (6, 0.55), (7, 0.6), (8, 0.4)))
    for m in mats["shell"]:
        body.data.materials.append(m)
    skin_object = body.copy()
    skin_object.data = body.data.copy()
    skin_object.name = "skin"
    bpy.context.scene.collection.objects.link(skin_object)
    surface = kit.Surface(skin_object)
    for s, track in ((FRONT_AXLE, TRACK_FRONT), (REAR_AXLE, TRACK_REAR)):
        for side in (1, -1):
            kit.cut_arch(body, F.world(side * (track + 0.13), s, TYRE_RADIUS + 0.012), 0.362, 0.62, LINER)

    bm = bmesh.new()
    bm.from_mesh(body.data)
    region = bm.faces.layers.int.new("region")
    gap = bm.faces.layers.int.new("gap")
    kind = bm.faces.layers.int.new("kind")
    skin = bm.faces.layers.int.new("skin")
    skip = (LINER,)

    def side(sign):
        return lambda c, n: sign * c[0] > 0.3 and sign * n.x > 0.45

    def up(c, n):
        return n.z > 0.25 and c[2] > 0.55

    # Panels, each with its shut line
    for sign, front, rear in ((1, DOOR_FL, DOOR_RL), (-1, DOOR_FR, DOOR_RR)):
        kit.mark(bm, F, "side", FRONT_DOOR, side(sign), region, front, GAP, gap, skip=skip)
        kit.mark(bm, F, "side", REAR_DOOR, side(sign), region, rear, GAP, gap, skip=skip)
    kit.mark(bm, F, "top", HOOD_TOP, up, region, HOOD, GAP, gap, skip=skip)
    kit.mark(bm, F, "top", TRUNK_TOP, up, region, TRUNK, GAP, gap, skip=skip)
    kit.mark(bm, F, "front", TRUNK_REAR, lambda c, n: c[1] > 4.2 and n.y > 0.25 and n.z <= 0.25, region, TRUNK, GAP, gap, skip=skip)

    # Glass and black trim
    for sign in (1, -1):
        kit.mark(bm, F, "side", FRONT_DOOR_GLASS, side(sign), kind, IS_GLASS, skip=skip)
        kit.mark(bm, F, "side", REAR_DOOR_GLASS, side(sign), kind, IS_GLASS, skip=skip)
        kit.mark(bm, F, "side", QUARTER_GLASS, side(sign), kind, IS_GLASS, skip=skip)
        kit.mark(bm, F, "side", B_PILLAR_FRONT, side(sign), kind, IS_TRIM, skip=skip)
        kit.mark(bm, F, "side", B_PILLAR_REAR, side(sign), kind, IS_TRIM, skip=skip)
    kit.mark(bm, F, "front", WINDSHIELD, lambda c, n: 1.38 < c[1] < 2.12 and n.y < -0.2, kind, IS_GLASS, skip=skip)
    kit.mark(bm, F, "front", REAR_GLASS, lambda c, n: 3.0 < c[1] < 3.97 and n.y > 0.2, kind, IS_GLASS, skip=skip)
    kit.mark(bm, F, "top", kit.rounded(-0.38, 2.24, 0.38, 2.72, 0.04), lambda c, n: n.z > 0.8, kind, IS_GLASS, skip=skip)
    kit.mark(bm, F, "top", COWL, up, kind, IS_TRIM, skip=skip)

    # A shut line stays on the body, as the dark edge of the opening
    for f in bm.faces:
        if f[gap]:
            f[region] = BODY
            f.material_index = SHUT
        elif f[kind] == IS_GLASS:
            f.material_index = GLASS
        elif f[kind] == IS_TRIM:
            f.material_index = TRIM

    # Inner skins: door cards, the undersides of the lids, the cabin's lining
    def solid(part):
        return [f for f in bm.faces if f[region] == part and f[kind] != IS_GLASS and f.material_index != LINER]

    for part in (DOOR_FL, DOOR_FR, DOOR_RL, DOOR_RR):
        kit.add_inner_skin(bm, solid(part), 0.045, CARD, PAINT, skin)
    for part in (HOOD, TRUNK):
        kit.add_inner_skin(bm, solid(part), 0.022, UNDER, PAINT, skin)
    cabin_faces = [f for f in solid(BODY)
                   if 1.40 < F.car(f.calc_center_median())[1] < 3.93 and f.calc_center_median().z > 0.33]
    kit.add_inner_skin(bm, cabin_faces, 0.022, HEADLINER, PAINT, skin)

    parts = {}
    for part, name in PART_NAMES.items():
        obj = kit.new_object(name, kit.extract(bm, [f for f in bm.faces if f[region] == part], layers=("skin",)),
                             mats["shell"], sharp_angle=38)
        kit.shade_from(obj, surface, (PAINT, GLASS, SHUT, TRIM))
        parts[name] = obj
    bm.free()
    bpy.data.objects.remove(body)
    return parts, skin_object, surface


# ── What is inside ──────────────────────────────────────────────────────────

def solid(name, bm, mat, sharp=40):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return kit.new_object(name, bm, mat, sharp_angle=sharp)


def seat(name, lat, s, mats, width=0.50):
    """A front seat: cushion, a back that leans, a headrest. `s` is the cushion's front."""
    half = width / 2
    back = kit.box(F, lat - half, lat + half, s + 0.44, s + 0.56, 0.44, 1.07, bevel=0.035)
    kit.turned(back, F, (lat, s + 0.50, 0.46), "lat", -17)
    bolsters = []
    for side in (-1, 1):       # the side bolsters of a sport seat
        b = kit.box(F, lat + side * half - 0.03, lat + side * half + 0.03, s + 0.04, s + 0.48, 0.42, 0.545, bevel=0.025)
        bolsters.append(solid(name + "_bolster", b, mats["leather"]))
    return [solid(name + "_cushion", kit.box(F, lat - half, lat + half, s, s + 0.50, 0.33, 0.51, bevel=0.035), mats["leather"]),
            solid(name + "_back", back, mats["leather"]),
            solid(name + "_headrest", kit.box(F, lat - 0.12, lat + 0.12, s + 0.665, s + 0.76, 1.09, 1.25, bevel=0.03), mats["leather"]),
            *bolsters]


def steering_wheel(mats, lat=0.36):
    """Rim, three spokes and a hub, facing the driver along the column."""
    centre, tilt, radius = (lat, 1.895, 0.905), 24, 0.185
    rim = bmesh.new()
    ring = [(0.016 * math.cos(2 * math.pi * i / 10), radius + 0.016 * math.sin(2 * math.pi * i / 10)) for i in range(11)]
    kit.lathe(rim, ring, steps=40)
    hub = bmesh.new()
    kit.lathe(hub, [(-0.03, 0.0), (-0.03, 0.07), (0.015, 0.075), (0.02, 0.0)], steps=20)
    for angle in (90, 210, 330):
        made = bmesh.ops.create_cube(hub, size=1.0)["verts"]
        bmesh.ops.scale(hub, verts=made, vec=(0.014, 0.045, radius - 0.06))
        bmesh.ops.translate(hub, verts=made, vec=(0, 0, 0.06 + (radius - 0.06) / 2))
        bmesh.ops.rotate(hub, verts=made, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(angle), 3, "X"))
    out = []
    for name, bm, mat in (("steering_rim", rim, mats["leather"]), ("steering_hub", hub, mats["dash"])):
        # The lathe's axis is X: turn it to point back along the car, then up with the column
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, "Z"))
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(tilt), 3, "X"))
        bmesh.ops.translate(bm, verts=bm.verts, vec=F.world(*centre))
        out.append(solid(name, bm, mat))
    column = bmesh.new()
    kit.tube(column, [F.world(lat, 1.66, 0.80), F.world(lat, 1.88, 0.898)], 0.03)
    out.append(solid("steering_column", column, mats["dash"]))
    return out


def build_cabin(mats):
    c, d, leather = mats["carpet"], mats["dash"], mats["leather"]
    parts = [
        solid("floor", kit.box(F, -0.77, 0.77, 1.40, 3.95, 0.19, 0.315), c),
        solid("tunnel", kit.box(F, -0.13, 0.13, 1.42, 3.0, 0.30, 0.40, bevel=0.03), c),
        solid("firewall", kit.box(F, -0.77, 0.77, 1.365, 1.425, 0.19, 0.94), c),
        solid("rear_bulkhead", kit.box(F, -0.71, 0.71, 3.575, 3.625, 0.30, 0.99), c),
        solid("parcel_shelf", kit.box(F, -0.67, 0.67, 3.60, 3.94, 0.985, 1.012, bevel=0.008), c),
        # Dashboard: the main moulding, the binnacle over the dials, the centre stack
        solid("dash", kit.box(F, -0.745, 0.745, 1.43, 1.73, 0.62, 0.955, bevel=0.06, segments=3), d),
        solid("binnacle", kit.box(F, 0.17, 0.55, 1.52, 1.75, 0.92, 1.035, bevel=0.035, segments=3), d),
        solid("dials", kit.box(F, 0.22, 0.50, 1.745, 1.755, 0.935, 1.01), mats["glass_dark"]),
        solid("centre_stack", kit.box(F, -0.15, 0.15, 1.66, 1.80, 0.44, 0.90, bevel=0.03), d),
        solid("screen", kit.box(F, -0.105, 0.105, 1.798, 1.806, 0.72, 0.84), mats["glass_dark"]),
        solid("vents", kit.box(F, -0.11, 0.11, 1.798, 1.806, 0.855, 0.885), mats["alu"]),
        solid("glovebox", kit.box(F, -0.66, -0.22, 1.725, 1.737, 0.64, 0.80), mats["card"]),
        solid("console", kit.box(F, -0.125, 0.125, 1.75, 2.86, 0.30, 0.525, bevel=0.03), d),
        solid("armrest", kit.box(F, -0.11, 0.11, 2.36, 2.80, 0.52, 0.58, bevel=0.025), leather),
        solid("mirror_inside", kit.box(F, -0.11, 0.11, 1.965, 1.985, 1.215, 1.265, bevel=0.008), d),
        solid("mirror_stalk", kit.box(F, -0.012, 0.012, 1.95, 1.975, 1.26, 1.31), d),
    ]
    lever = bmesh.new()
    kit.tube(lever, [F.world(0, 2.10, 0.52), F.world(0, 2.085, 0.61)], 0.011)
    parts.append(solid("gear_lever", lever, mats["alu"]))
    knob = bmesh.new()
    bmesh.ops.create_uvsphere(knob, u_segments=14, v_segments=8, radius=0.026)
    bmesh.ops.translate(knob, verts=knob.verts, vec=F.world(0, 2.083, 0.625))
    parts.append(solid("gear_knob", knob, leather))
    parts += seat("seat_driver", 0.365, 1.98, mats) + seat("seat_passenger", -0.365, 1.98, mats)
    for side in (1, -1):       # the B-pillars, leaning in with the glass
        post = kit.box(F, side * 0.735, side * 0.80, 2.455, 2.565, 0.31, 1.31, bevel=0.012)
        kit.turned(post, F, (side * 0.77, 2.51, 0.93), "s", side * -20,
                   verts=[v for v in post.verts if v.co.z > 0.93])
        parts.append(solid("b_pillar", post, mats["card"]))
    # Rear bench: one cushion, one back, three headrests
    back = kit.box(F, -0.68, 0.68, 3.42, 3.54, 0.44, 0.99, bevel=0.035)
    kit.turned(back, F, (0, 3.47, 0.46), "lat", -20)
    parts += [solid("bench_cushion", kit.box(F, -0.68, 0.68, 2.98, 3.47, 0.33, 0.50, bevel=0.035), leather),
              solid("bench_back", back, leather)]
    for lat in (-0.42, 0, 0.42):
        parts.append(solid("bench_headrest", kit.box(F, lat - 0.11, lat + 0.11, 3.615, 3.70, 0.985, 1.10, bevel=0.03), leather))
    return parts + steering_wheel(mats)


def build_engine_bay(mats):
    black, alu, engine = mats["black"], mats["alu"], mats["engine"]
    parts = [
        solid("bay_floor", kit.box(F, -0.70, 0.70, 0.20, 1.365, 0.20, 0.28), black),
        solid("radiator", kit.box(F, -0.36, 0.36, 0.185, 0.25, 0.30, 0.755, bevel=0.01), black),
        solid("slam_panel", kit.box(F, -0.62, 0.62, 0.17, 0.27, 0.745, 0.775, bevel=0.008), black),
        # The straight six, lying along the car and leaning to the right as it does
        solid("block", kit.box(F, -0.19, 0.13, 0.46, 1.25, 0.36, 0.73, bevel=0.02), alu),
        solid("cover_cam", kit.box(F, -0.185, -0.015, 0.47, 1.24, 0.725, 0.80, bevel=0.022), engine),
        solid("cover_injectors", kit.box(F, 0.005, 0.135, 0.50, 1.20, 0.715, 0.785, bevel=0.022), engine),
        solid("cover_badge", kit.box(F, -0.15, -0.05, 0.78, 0.94, 0.798, 0.806), alu),
        solid("oil_cap", kit.box(F, -0.13, -0.07, 0.53, 0.59, 0.795, 0.825, bevel=0.012), black),
        solid("manifold", kit.box(F, 0.15, 0.43, 0.56, 1.14, 0.50, 0.70, bevel=0.06, segments=3), black),
        solid("airbox", kit.box(F, 0.34, 0.65, 0.27, 0.58, 0.46, 0.745, bevel=0.03), black),
        solid("expansion_tank", kit.box(F, -0.45, -0.33, 0.27, 0.56, 0.47, 0.76, bevel=0.03), mats["tank"]),
        solid("fuse_box", kit.box(F, -0.64, -0.36, 1.07, 1.34, 0.62, 0.80, bevel=0.025), black),
        solid("washer_tank", kit.box(F, -0.66, -0.50, 0.30, 0.56, 0.45, 0.70, bevel=0.03), mats["tank"]),
        solid("booster_reservoir", kit.box(F, 0.40, 0.52, 1.13, 1.22, 0.80, 0.86, bevel=0.012), mats["tank"]),
    ]
    intake = bmesh.new()
    kit.tube(intake, [F.world(0.50, 0.56, 0.62), F.world(0.47, 0.72, 0.66), F.world(0.36, 0.84, 0.66)], 0.045)
    parts.append(solid("intake_pipe", intake, black))
    booster = bmesh.new()
    kit.lathe(booster, [(-0.08, 0.0), (-0.08, 0.10), (-0.03, 0.125), (0.03, 0.125), (0.08, 0.09), (0.08, 0.0)], steps=28)
    bmesh.ops.rotate(booster, verts=booster.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, "Z"))
    bmesh.ops.translate(booster, verts=booster.verts, vec=F.world(0.46, 1.27, 0.72))
    parts.append(solid("brake_booster", booster, black))
    for side in (1, -1):       # strut towers, and the nut on top of each
        tower = bmesh.new()
        bmesh.ops.create_cone(tower, cap_ends=True, segments=28, radius1=0.15, radius2=0.115, depth=0.40)
        bmesh.ops.translate(tower, verts=tower.verts, vec=F.world(side * 0.57, FRONT_AXLE + 0.02, 0.645))
        parts.append(solid("strut_tower", tower, mats["paint"]))
        cap = bmesh.new()
        bmesh.ops.create_cone(cap, cap_ends=True, segments=20, radius1=0.055, radius2=0.05, depth=0.03)
        bmesh.ops.translate(cap, verts=cap.verts, vec=F.world(side * 0.57, FRONT_AXLE + 0.02, 0.858))
        parts.append(solid("strut_cap", cap, alu))
    return parts


def build_boot(mats):
    c = mats["carpet"]
    return [
        solid("boot_floor", kit.box(F, -0.58, 0.58, 3.955, 4.41, 0.47, 0.50), c),
        solid("boot_front", kit.box(F, -0.58, 0.58, 3.955, 3.975, 0.47, 0.985), c),
        solid("boot_left", kit.box(F, 0.565, 0.585, 3.955, 4.41, 0.47, 0.985), c),
        solid("boot_right", kit.box(F, -0.585, -0.565, 3.955, 4.41, 0.47, 0.985), c),
        solid("boot_rear", kit.box(F, -0.58, 0.58, 4.395, 4.415, 0.47, 0.79), c),
        solid("boot_battery_cover", kit.box(F, -0.56, -0.30, 4.05, 4.36, 0.50, 0.62, bevel=0.02), c),
    ]


# ── Trim on the skin ────────────────────────────────────────────────────────

def build_trim(skin, surface, mats):
    """Lamps, grilles, handles, mirrors and the rest: {part: [objects]}, each on the part
    it moves with."""
    out = {name: [] for name in PART_NAMES.values()}

    def lay(part, name, view, outlines, mat, **kw):
        out[part].append(kit.decal(F, name, skin, view, outlines, mat, **kw))

    def skin_lat(side, s, z):
        hit = surface.hit(F.world(side * 2.0, s, z), (-side, 0, 0))
        return hit.x if hit else side * HALF_WIDTH

    def skin_z(lat, s):
        hit = surface.hit(F.world(lat, s, 3.0), (0, 0, -1))
        return hit.z if hit else 1.0

    for view, side, front, rear in (("left", 1, "door_front_left", "door_rear_left"),
                                    ("right", -1, "door_front_right", "door_rear_right")):
        # Door handles: a grip standing off the skin, in a dark recess
        for part, s0, z0 in ((front, 2.27, 0.868), (rear, 3.13, 0.882)):
            lay(part, "handle_recess", view, [kit.rounded(s0 - 0.012, z0 - 0.012, s0 + 0.172, z0 + 0.040)], mats["shut"], cell=0.05)
            x = skin_lat(side, s0 + 0.08, z0 + 0.014)
            grip = kit.box(F, x + side * 0.004, x + side * 0.024, s0, s0 + 0.16, z0, z0 + 0.028, bevel=0.008)
            out[part].append(solid("handle", grip, mats["paint"]))
        # The rub strip, in three lengths: fender, front door, rear door
        for part, a, b in (("body", 1.10, 1.495), (front, 1.51, 2.50), (rear, 2.52, 3.03)):
            za, zb = 0.555 + (a - 1.10) * 0.01, 0.555 + (b - 1.10) * 0.01
            lay(part, "rub_strip", view, [[(a, za), (b, zb), (b, zb + 0.016), (a, za + 0.016)]], mats["shut"], offset=0.004, cell=0.06)
        lay("body", "side_repeater", view, [kit.rounded(1.18, 0.735, 1.27, 0.757, 0.008)], mats["amber"], cell=0.05)
        lay("body", "tail_lamp_side", view, [[(4.26, 0.825), (4.41, 0.805), (4.41, 0.965), (4.29, 0.975)]], mats["red"], cell=0.025)
        lay("body", "headlamp_side", view, [[(0.10, 0.672), (0.24, 0.705), (0.24, 0.770), (0.11, 0.748)]], mats["lens"], offset=0.005, cell=0.012)
        # Mirror: a housing on a foot at the front corner of the door glass
        cap = kit.box(F, side * 0.825, side * 0.995, 1.665, 1.752, 0.928, 1.040, bevel=0.034, segments=4)
        out[front].append(solid("mirror_cap", cap, mats["paint"], sharp=60))
        glass = kit.box(F, side * 0.848, side * 0.972, 1.750, 1.755, 0.948, 1.020)
        out[front].append(solid("mirror_glass", glass, mats["chrome"]))
        foot = kit.box(F, side * 0.80, side * 0.86, 1.68, 1.75, 0.945, 0.985, bevel=0.01)
        out[front].append(solid("mirror_foot", foot, mats["black"]))

    # Front: headlamps, the two grilles, the M bumper's intakes
    lamp = [(0.305, 0.662), (0.60, 0.644), (0.785, 0.680), (0.80, 0.740), (0.62, 0.766), (0.305, 0.752)]
    lay("body", "headlamps", "front", kit.mirrored([lamp]), mats["lens"], offset=0.005, cell=0.012)
    lay("body", "projectors", "front", kit.mirrored([kit.ellipse(0.41, 0.706, 0.050, 0.036), kit.ellipse(0.575, 0.706, 0.056, 0.040)]),
        mats["projector"], offset=0.009, cell=0.012)
    lay("body", "front_signals", "front", kit.mirrored([[(0.70, 0.694), (0.785, 0.699), (0.79, 0.738), (0.70, 0.752)]]),
        mats["amber"], offset=0.009, cell=0.012)
    lay("body", "grille_surround", "front", kit.mirrored([kit.squircle(0.016, 0.668, 0.258, 0.752)]), mats["chrome"], cell=0.025)
    lay("body", "grilles", "front", kit.mirrored([kit.squircle(0.028, 0.679, 0.246, 0.741)]), mats["black"], offset=0.006, cell=0.025)
    lay("body", "grille_slats", "front", kit.mirrored([[(0.045 + i * 0.026, 0.684), (0.053 + i * 0.026, 0.684),
                                                         (0.053 + i * 0.026, 0.736), (0.045 + i * 0.026, 0.736)] for i in range(8)]),
        mats["projector"], offset=0.009, cell=0.03)
    lay("body", "badge_front", "front", [kit.ellipse(0, 0.786, 0.038, 0.020)], mats["badge"], cell=0.02)
    lay("body", "intakes", "front", [kit.rounded(-0.33, 0.255, 0.33, 0.415, 0.03)]
        + kit.mirrored([[(0.40, 0.265), (0.70, 0.285), (0.735, 0.40), (0.40, 0.42)]]), mats["black"], cell=0.03)
    lay("body", "fog_lamps", "front", kit.mirrored([kit.ellipse(0.565, 0.345, 0.050, 0.038)]), mats["lens"], offset=0.006, cell=0.02)
    lay("body", "plate_front", "front", [kit.rounded(-0.152, 0.435, 0.152, 0.545, 0.008)], mats["plate"], offset=0.006, cell=0.03)

    # Rear: the lamps are split by the lid's edge, as on the car
    lay("body", "tail_lamps", "rear", kit.mirrored([[(0.61, 0.790), (0.74, 0.785), (0.785, 0.86), (0.765, 0.972), (0.61, 0.977)]]),
        mats["red"], cell=0.025)
    lay("trunk", "tail_lamps_lid", "rear", kit.mirrored([[(0.33, 0.800), (0.59, 0.791), (0.59, 0.978), (0.33, 0.985)]]), mats["red"], cell=0.025)
    lay("body", "tail_clear", "rear", kit.mirrored([[(0.61, 0.910), (0.75, 0.908), (0.745, 0.948), (0.61, 0.950)]]),
        mats["clear"], offset=0.006, cell=0.025)
    lay("trunk", "tail_clear_lid", "rear", kit.mirrored([[(0.36, 0.915), (0.59, 0.911), (0.59, 0.951), (0.36, 0.955)]]),
        mats["clear"], offset=0.006, cell=0.025)
    lay("trunk", "plate_recess", "rear", [kit.rounded(-0.29, 0.815, 0.29, 0.965, 0.02)], mats["shut"], cell=0.03)
    lay("trunk", "plate", "rear", [kit.rounded(-0.152, 0.825, 0.152, 0.955, 0.008)], mats["plate"], offset=0.006, cell=0.03)
    lay("trunk", "badge_rear", "rear", [kit.ellipse(0, 1.005, 0.038, 0.026)], mats["badge"], cell=0.02)
    lay("body", "valance", "rear", [[(-0.63, 0.345), (0.63, 0.345), (0.56, 0.455), (-0.56, 0.455)]], mats["black"], cell=0.03)
    lay("body", "brake_lamp_high", "rear", [kit.rounded(-0.25, 1.118, 0.25, 1.136, 0.004)], mats["red"], offset=0.009, cell=0.03)

    # Wipers, parked along the foot of the windshield
    for pivot, tip in ((0.52, -0.06), (-0.08, -0.60)):
        blade = bmesh.new()
        points = []
        for i in range(6):
            lat = pivot + (tip - pivot) * i / 5
            s = 1.455 + 0.035 * math.sin(math.pi * i / 5)
            points.append(F.world(lat, s, skin_z(lat, s) + 0.014))
        kit.tube(blade, points, 0.008, segments=6)
        out["body"].append(solid("wiper", blade, mats["black"]))

    for lat in (0.345, 0.435):   # two exhaust tips, on the left
        tip = bmesh.new()
        bmesh.ops.create_cone(tip, cap_ends=False, segments=24, radius1=0.036, radius2=0.036, depth=0.10)
        bmesh.ops.rotate(tip, verts=tip.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"))
        bmesh.ops.translate(tip, verts=tip.verts, vec=F.world(lat, LENGTH - 0.06, 0.30))
        out["body"].append(kit.new_object("exhaust_tip", tip, mats["chrome"]))
    return out


# ── Wheels ──────────────────────────────────────────────────────────────────

def build_wheel(name, mats, width):
    """One wheel, axle along X, outer face towards +X, origin at its centre."""
    h = width / 2
    tyre = bmesh.new()
    # The tread has three grooves round it
    tread = []
    for x in (-0.42 * h, 0.0, 0.42 * h):
        tread += [(x - 0.008, TYRE_RADIUS), (x - 0.006, TYRE_RADIUS - 0.007), (x + 0.006, TYRE_RADIUS - 0.007), (x + 0.008, TYRE_RADIUS)]
    kit.lathe(tyre, [(-h + 0.01, 0.232), (-h, 0.262), (-h + 0.004, 0.296), (-h + 0.028, 0.314), (-h + 0.045, TYRE_RADIUS), *tread,
                     (h - 0.045, TYRE_RADIUS), (h - 0.028, 0.314), (h - 0.004, 0.296), (h, 0.262), (h - 0.01, 0.232)])
    bmesh.ops.recalc_face_normals(tyre, faces=tyre.faces)
    parts = [kit.new_object(name + "_tyre", tyre, mats["rubber"], sharp_angle=30)]

    rim = bmesh.new()
    kit.lathe(rim, [(-h + 0.01, 0.234), (-h + 0.012, 0.218), (h - 0.05, 0.205), (h - 0.03, 0.212), (h - 0.012, 0.222),
                    (h - 0.004, 0.236), (h - 0.012, 0.240)])
    kit.lathe(rim, [(h - 0.035, 0.0), (h - 0.022, 0.03), (h - 0.022, 0.062), (h - 0.03, 0.072)], steps=32)   # hub
    # Ten spokes in five pairs, from the hub to the rim
    for i in range(5):
        for half in (-1, 1):
            angle = 2 * math.pi * i / 5 + half * math.radians(9)
            made = bmesh.ops.create_cube(rim, size=1.0)["verts"]
            bmesh.ops.scale(rim, verts=made, vec=(0.016, 0.020, 0.160))
            bmesh.ops.translate(rim, verts=made, vec=(h - 0.034, 0, 0.134))
            bmesh.ops.rotate(rim, verts=made, cent=(0, 0, 0), matrix=Matrix.Rotation(angle, 3, "X"))
    bmesh.ops.recalc_face_normals(rim, faces=rim.faces)
    parts.append(kit.new_object(name + "_rim", rim, mats["rim"], sharp_angle=35))

    disc = bmesh.new()
    kit.lathe(disc, [(h - 0.075, 0.0), (h - 0.075, 0.165), (h - 0.095, 0.165), (h - 0.095, 0.203), (-h + 0.02, 0.203)], steps=32)
    bmesh.ops.recalc_face_normals(disc, faces=disc.faces)
    parts.append(kit.new_object(name + "_disc", disc, mats["disc"], sharp_angle=35))
    return kit.join(name, parts)


WHEELS = (("wheel_front_left", FRONT_AXLE, TRACK_FRONT, 0.225), ("wheel_front_right", FRONT_AXLE, -TRACK_FRONT, 0.225),
          ("wheel_rear_left", REAR_AXLE, TRACK_REAR, 0.255), ("wheel_rear_right", REAR_AXLE, -TRACK_REAR, 0.255))


def build_wheels(mats):
    wheels = {}
    for name, s, lat, width in WHEELS:
        wheel = build_wheel(name, mats, width)
        wheel.location = F.world(lat, s, TYRE_RADIUS)
        if lat < 0:
            wheel.rotation_euler = (0, 0, math.pi)
        wheels[name] = wheel
    return wheels


# ── The rig ─────────────────────────────────────────────────────────────────

# part -> (pivot in car coordinates, the app's axis, degrees open)
HINGES = {
    "door_front_left": ((0.862, 1.535, 0.60), "Y", -62),
    "door_front_right": ((-0.862, 1.535, 0.60), "Y", 62),
    "door_rear_left": ((0.868, 2.53, 0.60), "Y", -60),
    "door_rear_right": ((-0.868, 2.53, 0.60), "Y", 60),
    "hood": ((0.0, 1.385, 0.955), "X", -52),
    "trunk": ((0.0, 3.915, 1.055), "X", 78),
}


def pose(objects, opened):
    """Swing the hinged parts open (for the check pictures) or back."""
    for name, (pivot, axis, angle) in HINGES.items():
        p = F.world(*pivot)
        blender_axis, sign = {"X": ("X", 1), "Y": ("Z", 1)}[axis]
        turn = Matrix.Rotation(math.radians(sign * angle if opened else 0), 4, blender_axis)
        objects[name].matrix_world = Matrix.Translation(p) @ turn @ Matrix.Translation(-p)


# The rig's origin: the middle of the car, as the view turns it about that point
ORIGIN = F.world(0, LENGTH / 2, 0.70)

# Lamps the app lights: circuit, where (car coordinates), size (width, height), facing
LAMPS = [
    {"circuit": "head", "at": (0.575, 0.06, 0.706), "size": (0.11, 0.08), "round": True, "facing": "front"},
    {"circuit": "head", "at": (0.41, 0.03, 0.706), "size": (0.10, 0.07), "round": True, "facing": "front"},
    {"circuit": "fog", "at": (0.565, 0.02, 0.345), "size": (0.10, 0.075), "round": True, "facing": "front"},
    # (on the body's part of the tail lamp, outboard of the lid, so they stay put when it opens)
    {"circuit": "brake", "at": (0.69, LENGTH - 0.045, 0.85), "size": (0.14, 0.08), "facing": "rear"},
    {"circuit": "brake", "at": (0.0, 3.77, 1.127), "size": (0.50, 0.018), "facing": "rear"},    # high lamp, in the rear glass
    {"circuit": "reverse", "at": (0.685, LENGTH - 0.045, 0.928), "size": (0.12, 0.035), "facing": "rear"},
]


def export(objects):
    parts = {"body": {"objects": [objects["body"]], "pivot": ORIGIN, "axis": "Y", "openAngle": 0, "role": "body"}}
    taps = {"door_front_left": "driverDoorOpen", "door_front_right": "passengerDoorOpen",
            "door_rear_left": "rearLeftDoorOpen", "door_rear_right": "rearRightDoorOpen",
            "hood": "hoodOpen", "trunk": "tailgateOpen"}
    for name, (pivot, axis, angle) in HINGES.items():
        parts[name] = {"objects": [objects[name]], "pivot": F.world(*pivot), "axis": axis, "openAngle": angle,
                       "role": "panel", "tap": taps[name]}
    for name, s, lat, _width in WHEELS:
        parts[name] = {"objects": [objects[name]], "pivot": F.world(lat, s, TYRE_RADIUS), "axis": "X", "openAngle": 0,
                       "role": "steered_wheel" if s == FRONT_AXLE else "wheel", "radius": TYRE_RADIUS}
    lamps = []
    for lamp in LAMPS:
        for side in ((1, -1) if lamp["at"][0] else (1,)):
            lat, s, z = lamp["at"]
            lamps.append({"circuit": lamp["circuit"], "position": kit.to_app(F.world(side * lat, s, z) - ORIGIN),
                          "size": list(lamp["size"]), "round": bool(lamp.get("round")), "facing": lamp["facing"]})
    kit.export_rig(OUT, parts, ORIGIN, {
        "name": "BMW 330i ZHP", "groundY": round(-ORIGIN.z, 4), "maxSteer": 34,
        "lamps": lamps,
    })


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_materials()
    shell, skin, surface = build_shell(mats)
    trim = build_trim(skin, surface, mats)
    bpy.data.objects.remove(skin)
    trim["body"] += build_cabin(mats) + build_engine_bay(mats) + build_boot(mats)
    wheels = build_wheels(mats)
    # Every part as one object: the cut skin and what rides on it
    objects = {name: kit.join(name, [obj, *trim.get(name, [])]) for name, obj in shell.items()}
    objects.update(wheels)
    tris = sum(len(p.vertices) - 2 for o in bpy.data.objects if o.type == "MESH" for p in o.data.polygons)
    print(f"Built: {tris} triangles")
    if "--render" in args:
        folder = args[args.index("--render") + 1]
        only = args[args.index("--shots") + 1].split(",") if "--shots" in args else None
        closed = {"front34": (4.6, -6.8, 2.0), "rear34": (-4.6, 6.8, 2.0), "side": (9.0, 0, 0.9),
                  "front": (0, -9.0, 0.9), "rear": (0, 9.0, 0.9), "top": (0.01, 0, 11.0)}
        opened = {"open-front34": (5.2, -7.4, 2.6), "open-rear34": (-5.2, 7.4, 2.6),
                  "open-cabin": ((2.6, -0.3, 1.5), (0.0, -0.1, 0.8), 28)}
        pick = lambda shots: {k: v for k, v in shots.items() if not only or k in only}  # noqa: E731
        if pick(closed):
            kit.render(folder, "bmw", pick(closed))
        if pick(opened):
            pose(objects, True)
            kit.render(folder, "bmw", pick(opened))
            pose(objects, False)
    if "--no-export" not in args:
        export(objects)


main()
