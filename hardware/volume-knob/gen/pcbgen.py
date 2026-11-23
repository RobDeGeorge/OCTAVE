"""Build a KiCad board from a Design with pcbnew's Python API.

Footprints are linked to their schematic symbols (same UUID path), so
`kicad-cli pcb drc --schematic-parity` checks the board against the sheet.
"""

import pcbnew

from design import stable_uuid
from kilib import footprint_path

MM = pcbnew.FromMM


# 3D models the stock KiCad library names but does not ship (or ships under another
# name). The STEP files in lib/3dmodels/ are the LCSC / EasyEDA models of the exact
# JLCPCB parts, fetched with easyeda2kicad; offset (mm, KiCad's y-up) and z rotation
# put them on the KiCad footprint's pads (solved by matching pad positions).
MODELS = {
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12":
        ("${KIPRJMOD}/lib/3dmodels/HRO_TYPE-C-31-M-12_LCSC_C165948.step", (0, -1.025, 0), 180),
    # plain shapes from gen/mk3d.py, for the 3D view
    "Battery_Nichicon_SLB12400_Horizontal": ("${KIPRJMOD}/lib/3dmodels/Nichicon_SLB12400.wrl", (0, 0, 0), 0),
    "ESP32-C3-MINI-1": ("${KIPRJMOD}/lib/3dmodels/ESP32-C3-MINI-1.wrl", (0, 0, 0), 0),
    "RotaryEncoder_Alps_EC11J15_SMD": ("${KIPRJMOD}/lib/3dmodels/Alps_EC11J.wrl", (0, 0, 0), 0),
    "LED_WS2812B-2020_PLCC4_2.0x2.0mm": ("${KIPRJMOD}/lib/3dmodels/WS2812B-2020.wrl", (0, 0, 0), 0),
    "Texas_DRC0010J_ThermalVias":
        ("${KICAD10_3DMODEL_DIR}/Package_SON.3dshapes/VSON-10-1EP_3x3mm_P0.5mm_EP1.65x2.4mm.step", (0, 0, 0), 0),
    "TI_YBH0006_WCSP-6_0.8x1.2mm":
        ("${KICAD10_3DMODEL_DIR}/Package_BGA.3dshapes/Texas_DSBGA-6_0.757x1.01mm_Layout2x3_P0.35mm.step", (0, 0, 0), 0),
}


def set_model(fp, name):
    if name not in MODELS:
        return
    path, off, rot = MODELS[name]
    fp.Models().clear()
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = path
    m.m_Offset = pcbnew.VECTOR3D(*off)
    m.m_Rotation = pcbnew.VECTOR3D(0, 0, rot)
    m.m_Scale = pcbnew.VECTOR3D(1, 1, 1)
    fp.Models().push_back(m)


def new_board(layers=4):
    b = pcbnew.NewBoard("/dev/null") if False else pcbnew.BOARD()
    b.SetCopperLayerCount(layers)
    return b


def add_footprints(board, design, sch_name=None, sheets=None):
    """sheets: {ref: (sheet uuid, sheet name, sheet file)} when the schematic is a hierarchy."""
    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    fps = {}
    for part in design.parts:
        if not part.footprint or part.lib_id.startswith("power:"):
            continue
        lib, name = footprint_path(part.footprint)
        fp = pcbnew.FootprintLoad(lib, name)
        fp.SetFPIDAsString(part.footprint)
        set_model(fp, name)
        fp.SetReference(part.ref)
        fp.SetValue(part.value)
        units = sorted(part.sym.units)
        sym_uuid = stable_uuid(design.name, part.ref, units[0])
        if sheets and part.ref in sheets:
            su, sname, sfile = sheets[part.ref]
            fp.SetPath(pcbnew.KIID_PATH(f"/{su}/{sym_uuid}"))
            fp.SetSheetname(sname)
            fp.SetSheetfile(sfile)
        else:
            fp.SetPath(pcbnew.KIID_PATH("/" + sym_uuid))
            fp.SetSheetname("Root")
            fp.SetSheetfile((sch_name or design.name) + ".kicad_sch")
        for key, val in (("LCSC", part.lcsc), ("MPN", part.mpn), ("Note", part.note)):
            if val:
                f = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER, key) if hasattr(pcbnew, "FIELD_T_USER") else None
                if f is None:
                    fp.SetField(key, val)
                else:
                    f.SetText(val)
                    f.SetVisible(False)
                    f.SetLayer(pcbnew.F_Fab)
                    fp.Add(f)
        if part.dnp:
            fp.SetDNP(True)
        pin_net, pin_name = {}, {}
        for num, net_name in part.pin_net().items():
            for n in expand_pin_number(num):
                pin_net[n] = net_name
        for p in part.sym.pins:
            for n in expand_pin_number(p.number):
                pin_name[n] = p.name
        for pad in fp.Pads():
            num = pad.GetNumber()
            if not num or num not in pin_net:
                continue  # mechanical pad with no symbol pin
            if pin_net[num]:
                pad.SetNet(net(pin_net[num]))
            else:
                nm = pin_name.get(num, "")
                nm = nm.replace("/", "{slash}")
                label = f"{part.ref}-{nm}-Pad{num}" if nm and nm != "~" else f"{part.ref}-Pad{num}"
                pad.SetNet(net(f"unconnected-({label})"))
        board.Add(fp)
        fps[part.ref] = fp
    return fps, nets


def add_holes(board, centres, keep, footprint="MountingHole:MountingHole_3.2mm_M3"):
    """Mounting holes H1 ...: board-only footprints (not in the schematic, the BOM or the placement file),
    each with a rule area of radius `keep` on F.Cu and B.Cu that takes no tracks and no vias (pours may
    run through it: a screw head or a standoff sits on solder mask over plain copper, never on a track).
    Returns {ref: footprint}."""
    import math
    lib, name = footprint_path(footprint)
    out = {}
    for i, (x, y) in enumerate(centres, 1):
        fp = pcbnew.FootprintLoad(lib, name)
        fp.SetFPIDAsString(footprint)
        fp.SetReference(f"H{i}")
        fp.SetValue(footprint.split("_")[-1])
        fp.Reference().SetVisible(False)
        fp.SetAttributes(pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        place(fp, x, y)
        board.Add(fp)
        out[fp.GetReference()] = fp
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetZoneName(f"H{i} screw head")
        z.SetLayerSet(_lset(pcbnew.F_Cu, pcbnew.B_Cu))
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowZoneFills(True)      # no pour under the screw head either: it must not be ground by chance
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        o = z.Outline()
        o.NewOutline()
        n = 48
        for k in range(n):
            a = 2 * math.pi * k / n
            o.Append(MM(x + keep * math.cos(a)), MM(y + keep * math.sin(a)))
        board.Add(z)
    return out


def _lset(*layers):
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    return ls


def expand_pin_number(num):
    """KiCad 10 stacked pins: "[1,15,38-39]" -> ["1", "15", "38", "39"]."""
    if not (num.startswith("[") and num.endswith("]")):
        return [num]
    out = []
    for item in num[1:-1].split(","):
        if "-" in item:
            a, b = item.split("-")
            out.extend(str(i) for i in range(int(a), int(b) + 1))
        else:
            out.append(item.strip())
    return out


def place(fp, x, y, rot=0, back=False):
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    if back and not fp.IsFlipped():
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM if hasattr(pcbnew, "FLIP_DIRECTION_TOP_BOTTOM") else False)
    fp.SetOrientationDegrees(rot)


def circle_outline(board, cx, cy, r):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    s.SetEnd(pcbnew.VECTOR2I(MM(cx + r), MM(cy)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)


def notched_outline(board, cx, cy, r, nx, ny):
    """A disc with a rectangular notch cut into its top (-y): the notch is 2 * nx wide and its floor is at
    cy + ny (ny negative)."""
    import math
    yc = -math.sqrt(r * r - nx * nx)

    def P(x, y):
        return pcbnew.VECTOR2I(MM(cx + x), MM(cy + y))

    def add(s):
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)

    a = pcbnew.PCB_SHAPE(board)
    a.SetShape(pcbnew.SHAPE_T_ARC)
    a.SetArcGeometry(P(nx, yc), P(0, r), P(-nx, yc))       # the long way round, through the bottom
    add(a)
    for p, q in (((-nx, yc), (-nx, ny)), ((-nx, ny), (nx, ny)), ((nx, ny), (nx, yc))):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(*p))
        s.SetEnd(P(*q))
        add(s)


def circle_zone(board, netinfo, layer, cx, cy, r, clearance=0.25, min_w=0.2, priority=0, solid=False):
    import math
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(netinfo)
    o = z.Outline()
    o.NewOutline()
    for k in range(96):
        a = 2 * math.pi * k / 96
        o.Append(MM(cx + r * math.cos(a)), MM(cy + r * math.sin(a)))
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(min_w))
    z.SetAssignedPriority(priority)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid else pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(MM(0.25))
    z.SetThermalReliefSpokeWidth(MM(0.3))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)
    return z


def keepout_poly(board, pts, name, layers, tracks=True, vias=True, fills=True):
    """A rule area with the outline `pts`; each of tracks / vias / fills True means it is kept out."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    z.SetLayerSet(_lset(*layers))
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowZoneFills(fills)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    o = z.Outline()
    o.NewOutline()
    for x, y in pts:
        o.Append(MM(x), MM(y))
    board.Add(z)
    return z


def keepout_rect(board, x0, y0, x1, y1, name, layers, tracks=True, vias=True, fills=True):
    """A rectangular rule area; each of tracks / vias / fills True means it is kept out."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    z.SetLayerSet(_lset(*layers))
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowZoneFills(fills)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(MM(x), MM(y))
    board.Add(z)
    return z


def keepout_circle(board, x, y, r, name, layers=(pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)):
    """A rule area that takes no tracks and no vias (pours pull back from the hole by themselves)."""
    import math
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    z.SetLayerSet(_lset(*layers))
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowZoneFills(False)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    o = z.Outline()
    o.NewOutline()
    for k in range(32):
        a = 2 * math.pi * k / 32
        o.Append(MM(x + r * math.cos(a)), MM(y + r * math.sin(a)))
    board.Add(z)
    return z


def outline(board, x0, y0, x1, y1, r=1.0, tab=None):
    """Rectangular board outline; tab = (ya, yb, depth) adds a tab sticking out of the left edge."""
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    if tab:
        ya, yb, depth = tab
        pts += [(x0, yb), (x0 - depth, yb), (x0 - depth, ya), (x0, ya)]
    for i in range(len(pts)):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        a, b = pts[i], pts[(i + 1) % len(pts)]
        s.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])))
        s.SetEnd(pcbnew.VECTOR2I(MM(b[0]), MM(b[1])))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)


def zone(board, netinfo, layer, x0, y0, x1, y1, clearance=0.3, min_w=0.25, priority=0, solid=False):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(netinfo)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(MM(x), MM(y))
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(min_w))
    z.SetAssignedPriority(priority)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid else pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(MM(0.3))
    z.SetThermalReliefSpokeWidth(MM(0.4))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)
    return z


def fanout(board, netname, layer_plane, keepouts=(), via_d=0.6, drill=0.3, width=0.3, clearance=0.2,
           hv=(), hv_clearance=0.0, refs=None, allow=None):
    """Give every SMD pad on `netname` a short track + via down to its plane layer (only the pads
    of the footprints in `refs`, if given).

    Tries spots around each pad (nearest first) and keeps the first one whose via
    and track clear every other-net copper item by `clearance` (`hv_clearance` for the
    nets in `hv`). Returns the pads it
    could not fan out.
    """
    import math
    net = board.FindNet(netname)
    clr = MM(clearance)
    items = [t for t in board.GetTracks()] + [p for fp in board.GetFootprints() for p in fp.Pads()]
    failed = []
    for fp in board.GetFootprints():
        if refs is not None and fp.GetReference() not in refs:
            continue
        for pad in fp.Pads():
            if pad.GetNetCode() != net.GetNetCode() or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
            c = pad.GetPosition()
            bb = pad.GetBoundingBox()
            done = False
            for (vd, dr), extra, k in ((v, e, k) for v in ((via_d, drill), (0.45, 0.2))
                                       for e in (0, 0.4, 0.8, 1.2, 1.8, 2.4) for k in range(24)):
                r0 = max(bb.GetWidth(), bb.GetHeight()) / 2 + MM(vd / 2 + 0.25)
                a = 2 * math.pi * k / 24
                r = r0 + MM(extra)
                p = pcbnew.VECTOR2I(int(c.x + r * math.cos(a)), int(c.y + r * math.sin(a)))
                x, y = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
                if any(k0 <= x <= k2 and k1 <= y <= k3 for k0, k1, k2, k3 in keepouts):
                    continue
                if allow and not allow(x, y):
                    continue
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(p)
                via.SetWidth(MM(vd))
                via.SetDrill(MM(dr))
                via.SetNet(net)
                trk = pcbnew.PCB_TRACK(board)
                trk.SetStart(c)
                trk.SetEnd(p)
                trk.SetWidth(MM(min(width, vd - 0.1)))
                trk.SetLayer(layer)
                trk.SetNet(net)
                if _clear(board, via, trk, items, net, clr, hv, MM(hv_clearance)):
                    board.Add(via)
                    board.Add(trk)
                    items += [via, trk]
                    done = True
                    break
            if not done:    # the rings start a margin out from the pad: try every 0.05 mm spot within 2.5 mm
                spots = sorted(((dx * dx + dy * dy, dx, dy) for dx in range(-50, 51) for dy in range(-50, 51)))
                for vd, dr in ((via_d, drill), (0.45, 0.2)):
                    for _, dx, dy in spots:
                        p = pcbnew.VECTOR2I(c.x + MM(dx * 0.05), c.y + MM(dy * 0.05))
                        if pad.GetEffectiveShape(layer).Collide(pcbnew.SHAPE_CIRCLE(p, MM(vd / 2)), MM(0.1)):
                            continue    # no via in (or touching) the pad: solder would wick down it
                        x, y = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
                        if any(k0 <= x <= k2 and k1 <= y <= k3 for k0, k1, k2, k3 in keepouts):
                            continue
                        if allow and not allow(x, y):
                            continue
                        via = pcbnew.PCB_VIA(board)
                        via.SetPosition(p)
                        via.SetWidth(MM(vd))
                        via.SetDrill(MM(dr))
                        via.SetNet(net)
                        trk = pcbnew.PCB_TRACK(board)
                        trk.SetStart(c)
                        trk.SetEnd(p)
                        trk.SetWidth(MM(min(width, vd - 0.1)))
                        trk.SetLayer(layer)
                        trk.SetNet(net)
                        if _clear(board, via, trk, items, net, clr, hv, MM(hv_clearance)):
                            board.Add(via)
                            board.Add(trk)
                            items += [via, trk]
                            done = True
                            break
                    if done:
                        break
            if not done:
                failed.append(f"{fp.GetReference()}.{pad.GetNumber()}")
    return failed


def _clear(board, via, trk, items, net, clr, hv=(), hv_clr=0):
    # no via in, or within 0.15 mm of, any SMD pad on either side, its own net's included: solder
    # wicks down the hole and a small part tombstones
    vp, vr = via.GetPosition(), via.GetWidth(pcbnew.F_Cu) // 2
    for o in items:
        if o.Type() == pcbnew.PCB_PAD_T and o.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
            bb = o.GetBoundingBox()
            bb.Inflate(vr + MM(0.2))
            if bb.Contains(vp) and o.GetEffectiveShape(
                    pcbnew.F_Cu if o.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu).Collide(
                        pcbnew.SHAPE_CIRCLE(vp, vr), MM(0.15)):
                return False
    vbb = via.GetBoundingBox()
    tbb = trk.GetBoundingBox()
    vbb.Inflate(max(clr, hv_clr))
    tbb.Inflate(max(clr, hv_clr))
    edge = board.GetBoardEdgesBoundingBox()
    edge.Inflate(-(via.GetWidth(pcbnew.F_Cu) // 2 + pcbnew.FromMM(0.4)))
    if not edge.Contains(via.GetPosition()):
        return False
    for it in items:
        if it is via or it.m_Uuid == via.m_Uuid:
            continue
        if it.GetNetCode() == net.GetNetCode():
            # same net: copper may touch, but drilled holes still need 0.25 mm between them
            if it.Type() in (pcbnew.PCB_VIA_T, pcbnew.PCB_PAD_T) and it.HasHole():
                d = (it.GetPosition() - via.GetPosition()).EuclideanNorm()
                hole_r = it.GetDrillValue() // 2 if it.Type() == pcbnew.PCB_VIA_T else it.GetDrillSizeX() // 2
                if d < hole_r + via.GetDrillValue() // 2 + pcbnew.FromMM(0.3):
                    return False
            continue
        ib = it.GetBoundingBox()
        for mine, bb in ((via, vbb), (trk, tbb)):
            if not bb.Intersects(ib):
                continue
            for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
                if not (mine.IsOnLayer(layer) and it.IsOnLayer(layer)):
                    continue
                gap = max(clr, hv_clr) if it.GetNetname().lstrip("/") in hv else clr
                if mine.GetEffectiveShape(layer).Collide(it.GetEffectiveShape(layer), gap):
                    return False
    return True


def star(board, netname, pads, x, y, via_d=0.6, drill=0.3, width=0.25, clearance=0.2, inset=3.6):
    """Join a QFP's pads (list of (ref, number)) to one via at its centre (x, y).

    Each track leaves its pin straight inward, past the pad row (to `inset` mm from the
    centre), then runs diagonally through the empty middle to the via.
    """
    net = board.FindNet(netname)
    clr = MM(clearance)
    items = [t for t in board.GetTracks()] + [p for fp in board.GetFootprints() for p in fp.Pads()]
    centre = pcbnew.VECTOR2I(MM(x), MM(y))
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(centre)
    via.SetWidth(MM(via_d))
    via.SetDrill(MM(drill))
    via.SetNet(net)
    placed_via = False
    failed = []
    for ref, num in pads:
        pad = [p for p in board.FindFootprintByReference(ref).Pads() if p.GetNumber() == num][0]
        pp = pad.GetPosition()
        dx, dy = pp.x - centre.x, pp.y - centre.y
        if abs(dx) > abs(dy):
            corner = pcbnew.VECTOR2I(centre.x + (MM(inset) if dx > 0 else -MM(inset)), pp.y)
        else:
            corner = pcbnew.VECTOR2I(pp.x, centre.y + (MM(inset) if dy > 0 else -MM(inset)))
        segs = []
        for a, b in ((pp, corner), (corner, centre)):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(a)
            t.SetEnd(b)
            t.SetWidth(MM(width))
            t.SetLayer(pcbnew.F_Cu)
            t.SetNet(net)
            segs.append(t)
        if all(_clear(board, via, t, items, net, clr) for t in segs):
            if not placed_via:
                board.Add(via)
                items.append(via)
                placed_via = True
            for t in segs:
                board.Add(t)
                items.append(t)
        else:
            failed.append(f"{ref}.{num}")
    return failed


def stitch(board, netname, x0, y0, x1, y1, pitch=4.0, keepouts=(), via_d=0.6, drill=0.3, clearance=0.25,
           keep_courtyards=True, allow=None, skip=()):
    """GND stitching vias on a grid wherever one fits clear of other copper. `allow(x, y)` can veto
    a spot; the courtyards of the footprints in `skip` do not count (vias may sit under them)."""
    net = board.FindNet(netname)
    clr = MM(clearance)
    items = [t for t in board.GetTracks()] + [p for fp in board.GetFootprints() for p in fp.Pads()]
    n = 0
    y = y0
    while y <= y1:
        x = x0
        while x <= x1:
            if not any(k0 <= x <= k2 and k1 <= y <= k3 for k0, k1, k2, k3 in keepouts) and (not allow or allow(x, y)):
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
                via.SetWidth(MM(via_d))
                via.SetDrill(MM(drill))
                via.SetNet(net)
                # a zero-length track stands in for "no track" in the clearance check
                trk = pcbnew.PCB_TRACK(board)
                trk.SetStart(via.GetPosition())
                trk.SetEnd(via.GetPosition())
                trk.SetWidth(MM(via_d))
                trk.SetLayer(pcbnew.F_Cu)
                trk.SetNet(net)
                if _clear(board, via, trk, items, net, clr) and not (keep_courtyards and _under_footprint(board, via, skip)):
                    board.Add(via)
                    items.append(via)
                    n += 1
            x += pitch
        y += pitch
    return n


def _under_footprint(board, via, skip=()):
    """Keep stitching vias out of every courtyard (no vias under parts, no paste wicking)."""
    p = via.GetPosition()
    for fp in board.GetFootprints():
        if fp.GetReference() in skip:
            continue
        for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            cy = fp.GetCourtyard(layer)
            if cy.OutlineCount() and cy.Contains(p):
                return True
    return False
