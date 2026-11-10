"""3D models of the J1962 (OBD-II) male plug and of the plug on its adapter board, in lib/3dmodels/:

  J1962_Male_Straight_Jiatel_CR-PG001.step   the plug alone (footprint J1962_Plug_Adapter, the
                                             adapter board obd-adapter-plug)
  J1962_Edge_Joint_Assembly.step             plug + adapter board, placed for the main board's
                                             edge footprint J1962_Edge_Joint

Run with FreeCAD (it is not part of build.py, the STEPs are committed). The GUI build, offscreen, so
the colours reach the STEP; freecadcmd writes them uncoloured:

    QT_QPA_PLATFORM=offscreen freecad gen/mkplug3d.py

Source: Shenzhen Jiatel drawing OBD-II-16M-12V, part no. CR-PG001 rev A (jiatelcn.com, "OBD-II 16 Pin
Straight Male Connector", OBDII-Male-16pin.pdf). From it:
  - contacts 0.8 x 1.8 mm brass blades, 2 x 8, 4.00 mm pitch, rows 8.60 mm apart, 19.25 mm long;
    tails 4.0 mm out of the back; mating side 8.5 mm out of the insert face, pins 4 and 5 10.0 mm
  - back flange 37.60 x 17.20 x 2.0, then a 33.60 x 2.0 step, then the mating shroud: 40.26 x 17.80
    trapezoid, 16.0 deep (20.0 overall), insert face 6.85 mm above the back
  - centre key slot 1.40 wide in the insert face, latch on the pins 9-16 side
Read off the drawing (not dimensioned): the narrow side of the trapezoid (31.8), the step's width
(16.0), the shroud wall (1.5), the slot length (27.5) and the latch (12.6 long, 1.9 out).

The plug model is in KiCad 3D-model coordinates for its footprint (x right, y UP = -footprint y,
z out of the board face it sits on): pin k (1-8) at x = (4.5 - k) * 4, y = +4.3 (footprint y -4.3,
the wide side), pin k + 8 under it at y = -4.3. Seen from the mating face pin 1 is top right.
"""

import os

import FreeCAD as App
import Import
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.join(HERE, "..", "lib", "3dmodels")
OUT = os.path.join(MODELS, "J1962_Male_Straight_Jiatel_CR-PG001.step")
OUT_ASM = os.path.join(MODELS, "J1962_Edge_Joint_Assembly.step")
# adapter board (see build.py plug): 40 x 18 x 1.6, slot 36 x 2.0 for the main board's tab
ADAPTER = (40.0, 18.0, 1.6)
SLOT = (36.0, 2.0)
PIN_SLOT = (2.6, 1.3)                # the blades' holes (mklib.j1962_plug_adapter has the same)
BOARD_T = 1.6

PITCH, ROW = 4.0, 8.6
BLADE_W, BLADE_T = 1.8, 0.8          # along the row, across it
TAIL = 4.0                           # below z = 0
INSERT = 6.85                        # insert face above the back
MATE, MATE_GND = 8.5, 10.0           # pin length out of the insert face (4 and 5 longer)
TOP = 20.0

V = App.Vector


def rrect(w, h, r, z0, z1):
    """Rounded rectangle prism centred on the origin."""
    box = Part.makeBox(w, h, z1 - z0, V(-w / 2, -h / 2, z0))
    return box.makeFillet(r, [e for e in box.Edges if abs(e.tangentAt(0).z) > 0.99]) if r else box


def trapezoid(inset, z0, z1):
    """The mating shroud's outline: 40.26 long on the pins 1-8 side (y < 0), 31.8 on the 9-16 side,
    17.8 across; the long side keeps 4.2 mm of straight end before the ends slope in."""
    hw, hn, hy, ys = 40.26 / 2 - inset, 31.8 / 2 - inset, 17.8 / 2 - inset, -4.7
    pts = [V(-hw, -hy, z0), V(hw, -hy, z0), V(hw, ys, z0), V(hn, hy, z0), V(-hn, hy, z0), V(-hw, ys, z0)]
    face = Part.Face(Part.makePolygon(pts + [pts[0]]))
    solid = face.extrude(V(0, 0, z1 - z0))
    vert = [e for e in solid.Edges if abs(e.tangentAt(0).z) > 0.99]
    return solid.makeFillet(1.2 if inset == 0 else 0.6, vert)


def housing():
    body = rrect(37.6, 17.2, 1.5, 0.0, 2.0)                          # back flange, sits on the board
    body = body.fuse(rrect(33.6, 16.0, 1.0, 2.0, 4.0))               # step
    body = body.fuse(trapezoid(0.0, 4.0, TOP))                       # mating shroud
    body = body.cut(trapezoid(1.5, INSERT, TOP + 1))                 # its cavity, down to the insert
    slot = Part.makeBox(27.5 - 1.4, 1.4, 3.0, V(-(27.5 - 1.4) / 2, -0.7, INSERT - 2.0))
    for x in (-(27.5 - 1.4) / 2, (27.5 - 1.4) / 2):
        slot = slot.fuse(Part.makeCylinder(0.7, 3.0, V(x, 0, INSERT - 2.0)))
    body = body.cut(slot)                                            # centre key slot
    # latch on the 9-16 side: a block with a ramp to the mating end
    latch = Part.makeBox(12.6, 1.9, 13.0, V(-6.3, 8.9 - 0.2, 5.5))
    ramp = Part.Face(Part.makePolygon([V(-6.3, 8.7, 18.5), V(-6.3, 10.6, 18.5), V(-6.3, 8.7, TOP), V(-6.3, 8.7, 18.5)]))
    body = body.fuse(latch).fuse(ramp.extrude(V(12.6, 0, 0)))
    return body.removeSplitter()


def pins():
    out = []
    for k in range(1, 17):
        col = (k - 1) % 8 + 1
        x, y = (4.5 - col) * PITCH, -ROW / 2 if k <= 8 else ROW / 2
        top = INSERT + (MATE_GND if k in (4, 5) else MATE)
        blade = Part.makeBox(BLADE_W, BLADE_T, top + TAIL, V(x - BLADE_W / 2, y - BLADE_T / 2, -TAIL))
        tips = [e for e in blade.Edges if abs(e.tangentAt(0).y) > 0.99 and abs(e.Vertexes[0].Point.z - top) < 1e-6
                or abs(e.tangentAt(0).y) > 0.99 and abs(e.Vertexes[0].Point.z + TAIL) < 1e-6]
        out.append(blade.makeChamfer(0.4, tips))
    return Part.makeCompound(out)


def mirror_y(shape):
    """The parts above are drawn in footprint coordinates (y down); KiCad models have y up."""
    return shape.mirror(V(0, 0, 0), V(0, 1, 0))


def adapter():
    """Adapter board in the plug's frame: its front face at z = 0 (plug side), back at z = -1.6."""
    w, h, t = ADAPTER
    b = Part.makeBox(w, h, t, V(-w / 2, -h / 2, -t))
    sl, sw = SLOT
    slot = Part.makeBox(sl - sw, sw, t + 2, V(-(sl - sw) / 2, -sw / 2, -t - 1))
    for x in (-(sl - sw) / 2, (sl - sw) / 2):
        slot = slot.fuse(Part.makeCylinder(sw / 2, t + 2, V(x, 0, -t - 1)))
    b = b.cut(slot)
    for k in range(1, 9):
        for y in (-ROW / 2, ROW / 2):
            b = b.cut(Part.makeBox(PIN_SLOT[0], PIN_SLOT[1], t + 2,
                                   V((4.5 - k) * PITCH - PIN_SLOT[0] / 2, y - PIN_SLOT[1] / 2, -t - 1)))
    pads = []
    for k in range(1, 9):                       # joint pads on the back, slot edge to pin
        for sgn in (-1, 1):
            y0, y1 = sw / 2 + 0.25, ROW / 2 + 1.0
            y = y0 if sgn > 0 else -y1
            pads.append(Part.makeBox(2.5, y1 - y0, 0.04, V((4.5 - k) * PITCH - 1.25, y, -t - 0.04)))
    return b.removeSplitter(), Part.makeCompound(pads)


# plug frame -> main-board footprint frame (J1962_Edge_Joint, KiCad model coordinates: x right,
# y up, z up from the board's top face). The plug's back sits on the adapter's front face at
# x = -1.6, its mating face points -x; pins 9-16 above the board's mid-plane (z = -0.8), 1-8 below;
# pin 1 / 9 at footprint y -14 (model y +14). Columns: plug x -> +y, plug y -> -z, plug z -> -x.
TO_EDGE = App.Matrix(0, 0, -1, -ADAPTER[2], 1, 0, 0, 0, 0, -1, 0, -BOARD_T / 2, 0, 0, 0, 1)


def export(objs, path):
    if App.GuiUp:
        import ImportGui                         # colours only reach the STEP from the view objects
        for o, rgb in objs:
            o.ViewObject.ShapeColor = rgb
        ImportGui.export([o for o, _ in objs], path)
    else:
        Import.export([o for o, _ in objs], path)


def main():
    os.makedirs(MODELS, exist_ok=True)
    doc = App.newDocument("j1962")
    housing_s, pins_s = mirror_y(housing()), mirror_y(pins())
    board_s, pads_s = (mirror_y(x) for x in adapter())
    black, gold, green = (0.12, 0.12, 0.13), (0.85, 0.72, 0.35), (0.10, 0.36, 0.20)

    def obj(name, shape, matrix=None):
        o = doc.addObject("Part::Feature", name)
        o.Shape = shape.transformed(matrix) if matrix else shape
        return o

    h, p = obj("Housing", housing_s), obj("Pins", pins_s)
    doc.recompute()
    export([(h, black), (p, gold)], OUT)
    ah, ap = obj("AsmHousing", housing_s, TO_EDGE), obj("AsmPins", pins_s, TO_EDGE)
    ab, apd = obj("AsmAdapter", board_s, TO_EDGE), obj("AsmPads", pads_s, TO_EDGE)
    doc.recompute()
    export([(ah, black), (ap, gold), (ab, green), (apd, gold)], OUT_ASM)
    print("wrote", OUT, OUT_ASM)


main()
if App.GuiUp:
    os._exit(0)          # the GUI would otherwise stay up
