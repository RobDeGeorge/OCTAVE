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
    "Fuse_2920_7451Metric": ("${KIPRJMOD}/lib/3dmodels/Fuse_2920_LCSC_C49326266.step", (0, 0, 0), 0),
    "Molex_Micro-Fit_3.0_43045-1000_2x05_P3.00mm_Horizontal":
        ("${KIPRJMOD}/lib/3dmodels/Molex_43045-1000_LCSC_C122421.step", (6.0, -1.5, 0), 180),
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12":
        ("${KIPRJMOD}/lib/3dmodels/HRO_TYPE-C-31-M-12_LCSC_C165948.step", (0, 1.575, 0), 180),
    "SW_Push_1P1T_XKB_TS-1187A": ("${KIPRJMOD}/lib/3dmodels/XKB_TS-1187A_LCSC_C318884.step", (0, 0.025, 0), 0),
    "TI_SO-PowerPAD-8_ThermalVias": ("${KICAD10_3DMODEL_DIR}/Package_SO.3dshapes/TI_SO-PowerPAD-8.step", (0, 0, 0), 0),
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


def add_footprints(board, design, sch_name=None):
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
                pad.SetNet(net("/" + pin_net[num]))
            else:
                nm = pin_name.get(num, "")
                nm = nm.replace("/", "{slash}")
                label = f"{part.ref}-{nm}-Pad{num}" if nm and nm != "~" else f"{part.ref}-Pad{num}"
                pad.SetNet(net(f"unconnected-({label})"))
        board.Add(fp)
        fps[part.ref] = fp
    return fps, nets


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


def outline(board, x0, y0, x1, y1, r=1.0):
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        a, b = pts[i], pts[(i + 1) % 4]
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
           hv=(), hv_clearance=0.0):
    """Give every SMD pad on `netname` a short track + via down to its plane layer.

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
           keep_courtyards=True):
    """GND stitching vias on a grid wherever one fits clear of other copper."""
    net = board.FindNet(netname)
    clr = MM(clearance)
    items = [t for t in board.GetTracks()] + [p for fp in board.GetFootprints() for p in fp.Pads()]
    n = 0
    y = y0
    while y <= y1:
        x = x0
        while x <= x1:
            if not any(k0 <= x <= k2 and k1 <= y <= k3 for k0, k1, k2, k3 in keepouts):
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
                if _clear(board, via, trk, items, net, clr) and not (keep_courtyards and _under_footprint(board, via)):
                    board.Add(via)
                    items.append(via)
                    n += 1
            x += pitch
        y += pitch
    return n


def _under_footprint(board, via):
    """Keep stitching vias out of every courtyard (no vias under parts, no paste wicking)."""
    p = via.GetPosition()
    for fp in board.GetFootprints():
        for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            cy = fp.GetCourtyard(layer)
            if cy.OutlineCount() and cy.Contains(p):
                return True
    return False
