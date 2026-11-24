#!/usr/bin/env python3
"""Regenerate the LoRa pod's KiCad project from lora_pod.py.

    python3 build.py sch      # symbol lib + schematic + ERC
    python3 build.py pcb      # board with placement (pod_layout.py), unrouted
    python3 build.py route    # Freerouting -> import -> pours -> DRC (run after pcb, as its own command)
    python3 build.py view     # pictures of the board in view/

Uses the system python3 (pcbnew lives there), not the repo venv, and the generator modules of
the OBD adapter (../../obd-adapter/gen): the design model, the schematic and board writers.
"""

import glob
import json
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SHARED = os.path.normpath(os.path.join(PROJ, "..", "obd-adapter", "gen"))
LIB = os.path.join(PROJ, "lib")
NAME = "lora-pod"
SCH = os.path.join(PROJ, NAME + ".kicad_sch")
PCB = os.path.join(PROJ, NAME + ".kicad_pcb")
REV = "0"
DATE = "2026-11-23"
TITLE = "OCTAVE LoRa pod (bring-up board)"

SHEETS = [
    ("Power and cable", "power", ["power", "usb"],
     "5 to 16 V in (OBD adapter or battery), protection, 3.3 V LDO, battery sense, UART; USB-C"),
    ("LoRa MCU", "mcu", ["mcu", "ui"],
     "RAK4630 (nRF52840 + SX1262: BLE and LoRa), SWD, buttons, LEDs, headers"),
    ("GPS and IMU", "sense", ["gnss", "imu"],
     "L80-R GPS and its switched and backup supplies; ASM330LHH IMU, Qwiic"),
]
RAILS = {"GND", "3V3", "VSYS", "VBUS", "3V3_GNSS"}
POWER_FLAGS = {"GND", "VSYS", "VBUS", "V_BCKP"}
# 3D models KiCad's library names but does not ship: the LCSC / EasyEDA models of the exact JLCPCB
# parts (fetched with easyeda2kicad) in lib/3dmodels/. Offset (mm, y up) and z rotation put each on
# the KiCad footprint's pads (EasyEDA draws the L80-R a quarter turn from KiCad). The RAK4630's model
# is named in its own footprint (lib/OCTAVE.pretty).
MODELS = {
    "Quectel_L80-R": ("${KIPRJMOD}/lib/3dmodels/Quectel_L80-R_LCSC_C3029061.step", (0, -0.19, 0), 90),
    "SW_Push_1P1T_XKB_TS-1187A": ("${KIPRJMOD}/lib/3dmodels/XKB_TS-1187A_LCSC_C318884.step", (0, 0.025, 0), 0),
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12":
        ("${KIPRJMOD}/lib/3dmodels/HRO_TYPE-C-31-M-12_LCSC_C165948.step", (0, -1.025, 0), 180),
}
SIGNAL_WIDTH, SIGNAL_CLEARANCE = 200, 150   # um (0.15 mm between the IMU's LGA pads)


def run(cmd, **kw):
    print("+", " ".join(cmd))
    return subprocess.run(cmd, check=False, **kw)


def _shared():
    """Point the adapter's generator modules at this project's library and pages."""
    import kilib
    import mklib
    kilib.SYM_DIRS[0] = kilib.FP_DIRS[0] = LIB
    mklib.OUT = os.path.join(LIB, "OCTAVE.kicad_sym")


def _lib():
    """lib/OCTAVE.kicad_sym and lib/OCTAVE.pretty: this board's project symbols and footprints."""
    import mklib
    import pod_libdefs
    os.makedirs(LIB, exist_ok=True)
    mklib.write(pod_libdefs.symbols())
    pod_libdefs.rak4630_footprint(os.path.join(LIB, "OCTAVE.pretty"))


def project_files():
    with open(os.path.join(PROJ, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n\t(version 7)\n\t(lib (name "OCTAVE") (type "KiCad") '
                '(uri "${KIPRJMOD}/lib/OCTAVE.kicad_sym") (options "") (descr "OCTAVE project symbols"))\n)\n')
    with open(os.path.join(PROJ, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n\t(version 7)\n\t(lib (name "OCTAVE") (type "KiCad") '
                '(uri "${KIPRJMOD}/lib/OCTAVE.pretty") (options "") (descr "OCTAVE project footprints"))\n)\n')
    # JLCPCB 4-layer capabilities, with a little margin
    rules = {"min_clearance": 0.15, "min_track_width": 0.15, "min_via_diameter": 0.45,
             "min_via_annular_width": 0.1, "min_through_hole_diameter": 0.2, "min_hole_to_hole": 0.25,
             "min_copper_edge_clearance": 0.3, "min_silk_clearance": 0.0, "min_text_height": 1.0,
             "min_text_thickness": 0.15, "min_hole_clearance": 0.2}
    pro = {"meta": {"filename": NAME + ".kicad_pro", "version": 3},
           "board": {"design_settings": {"rules": rules}}, "schematic": {}, "libraries": {},
           "net_settings": {"meta": {"version": 4}, "classes": [{
               "name": "Default", "priority": 2147483647, "clearance": SIGNAL_CLEARANCE / 1000,
               "track_width": SIGNAL_WIDTH / 1000, "via_diameter": 0.6, "via_drill": 0.3,
               "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2, "diff_pair_gap": 0.25,
               "diff_pair_via_gap": 0.25, "bus_width": 12, "wire_width": 6, "line_style": 0,
               "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)"}]}}
    with open(os.path.join(PROJ, NAME + ".kicad_pro"), "w") as f:
        json.dump(pro, f, indent=2)


def _write_root(design, path, pages, title, rev, date, comments=()):
    """The top page: one sheet symbol per page, the cable on the left, the sensors on the right."""
    import schsheets
    from design import stable_uuid
    from schgen import font, text_w
    from sexpr import Q, dump
    root = stable_uuid(design.name, "root")
    items = []
    pos = {"Power and cable": (25, 60), "LoRa MCU": (115, 60), "GPS and IMU": (205, 60)}
    BW, BH = 65, 24
    for n, (name, suffix, _, what) in enumerate(schsheets.SHEETS):
        x, y = pos[name]
        u = schsheets.sheet_uuid(design, name)
        items.append(["sheet", ["at", x, y], ["size", BW, BH], ["exclude_from_sim", "no"], ["in_bom", "yes"],
                      ["on_board", "yes"], ["dnp", "no"], ["fields_autoplaced", "yes"],
                      ["stroke", ["width", 0.3], ["type", "solid"]], ["fill", ["color", 0, 0, 0, 0.0]],
                      ["uuid", Q(u)],
                      ["property", Q("Sheetname"), Q(name), ["at", x, y - 0.7, 0],
                       ["effects", font(2, True), ["justify", "left", "bottom"]]],
                      ["property", Q("Sheetfile"), Q(schsheets.sheet_file(design, suffix)), ["at", x, y + BH + 0.6, 0],
                       ["effects", font(), ["justify", "left", "top"], ["hide", "yes"]]],
                      ["instances", ["project", Q(design.name), ["path", Q(f"/{root}"), ["page", Q(str(n + 2))]]]]])
        lines, cur = [], ""
        for wd in what.split():
            if text_w(cur + " " + wd) > BW - 6 and cur:
                lines.append(cur)
                cur = wd
            else:
                cur = (cur + " " + wd).strip()
        lines.append(cur)
        for i, line in enumerate(lines):
            items.append(["text", Q(line), ["exclude_from_sim", "no"], ["at", x + 3, y + 5 + i * 3.5, 0],
                          ["effects", font(), ["justify", "left", "top"]],
                          ["uuid", Q(stable_uuid(design.name, "roottext", name, i))]])

    def arrow(pts, label, key):
        items.append(["polyline", ["pts", *[["xy", px, py] for px, py in pts]],
                      ["stroke", ["width", 0.4], ["type", "default"]], ["fill", ["type", "none"]],
                      ["uuid", Q(stable_uuid(design.name, "arrow", key))]])
        (ax, ay), (bx, by) = pts[-2], pts[-1]
        dd = math.hypot(bx - ax, by - ay) or 1
        ux, uy = (bx - ax) / dd, (by - ay) / dd
        head = [(bx - 3 * ux + 1.5 * uy, by - 3 * uy - 1.5 * ux), (bx, by), (bx - 3 * ux - 1.5 * uy, by - 3 * uy + 1.5 * ux)]
        items.append(["polyline", ["pts", *[["xy", px, py] for px, py in head]],
                      ["stroke", ["width", 0.4], ["type", "default"]], ["fill", ["type", "none"]],
                      ["uuid", Q(stable_uuid(design.name, "arrowhead", key))]])
        mx, my = (pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2
        items.append(["text", Q(label), ["exclude_from_sim", "no"], ["at", mx, my - 1.5, 0],
                      ["effects", font(), ["justify", "bottom"]],
                      ["uuid", Q(stable_uuid(design.name, "arrowtext", key))]])

    arrow([(90, 68), (115, 68)], "3V3, UART, USB", "power-mcu")
    arrow([(180, 68), (205, 68)], "UART, I2C", "mcu-sense")
    items.append(["text", Q(title), ["exclude_from_sim", "no"], ["at", 25, 25, 0],
                  ["effects", font(4, True), ["justify", "left", "bottom"]],
                  ["uuid", Q(stable_uuid(design.name, "roottitle"))]])
    items.append(["text", Q("Every net has the same name on every page and on the board: supply rails by "
                            "power symbol, other nets by global label."),
                  ["exclude_from_sim", "no"], ["at", 25, 30, 0], ["effects", font(), ["justify", "left", "top"]],
                  ["uuid", Q(stable_uuid(design.name, "rootnote"))]])
    tb = ["title_block", ["title", Q(title)], ["date", Q(date)], ["rev", Q(rev)], ["company", Q("OCTAVE")]]
    for i, c in enumerate(comments):
        tb.append(["comment", i + 1, Q(c)])
    inst = [["path", Q("/"), ["page", Q("1")]]]
    for n, (name, *_r) in enumerate(schsheets.SHEETS):
        inst.append(["path", Q(f"/{schsheets.sheet_uuid(design, name)}"), ["page", Q(str(n + 2))]])
    doc = ["kicad_sch", ["version", "20250114"], ["generator", Q("octave_gen")], ["generator_version", Q("9.0")],
           ["uuid", Q(root)], ["paper", Q("A4")], tb, ["lib_symbols"], *items,
           ["sheet_instances", *inst], ["embedded_fonts", "no"]]
    with open(path, "w") as f:
        f.write(dump(doc) + "\n")
    return root


def _pages():
    """This board's pages in the adapter's sheet writer (all laid out by the column packer)."""
    import drawn
    import schsheets
    schsheets.SHEETS, schsheets.RAILS, schsheets.POWER_FLAGS = SHEETS, RAILS, POWER_FLAGS
    schsheets.write_root = _write_root
    drawn.PAGES, drawn.PAGE_OF, drawn.SPLIT = {}, {}, set()
    return schsheets


def design():
    _shared()
    if not os.path.exists(os.path.join(LIB, "OCTAVE.kicad_sym")):
        _lib()
    import lora_pod
    return lora_pod.d


def sch():
    _shared()
    _lib()
    project_files()
    d = design()
    nets, problems = d.check()
    if problems:
        sys.exit("design problems:\n" + "\n".join(problems))
    schsheets = _pages()
    for old in glob.glob(os.path.join(PROJ, NAME + "-*.kicad_sch")):
        os.remove(old)
    problems = schsheets.write_all(d, PROJ, TITLE, REV, DATE, comments=(
        "Generated by gen/build.py: edit the Python, not this sheet.",
        "Rails by power symbol, other nets by global label: same name = same net."))
    if problems:
        sys.exit("schematic layout problems:\n" + "\n".join(problems))
    rpt = os.path.join(PROJ, "erc.rpt")
    run(["kicad-cli", "sch", "erc", "--severity-all", SCH, "-o", rpt], capture_output=True)
    print(open(rpt).read().split("** ERC messages")[1].split("\n")[0] if os.path.exists(rpt) else "no report")


def _centre(fp, x, y, rot):
    """Turn the footprint, then move it so that its courtyard's centre is at (x, y)."""
    import pcbnew
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(0, 0))
    bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
    c = bb.Centre()
    fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x) - c.x, pcbnew.FromMM(y) - c.y))


def pcb():
    import pcbnew
    import pod_layout as L
    d = design()
    schsheets = _pages()
    import pcbgen
    pcbgen.MODELS = MODELS
    board = pcbgen.new_board(4)
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(4)
    ds.m_TrackMinWidth = pcbnew.FromMM(0.15)
    ds.m_ViasMinSize = pcbnew.FromMM(0.45)
    ds.m_MinThroughDrill = pcbnew.FromMM(0.2)
    ds.m_MinClearance = pcbnew.FromMM(0.15)
    ds.m_CopperEdgeClearance = pcbnew.FromMM(0.3)
    fps, nets = pcbgen.add_footprints(board, d, sch_name=NAME, sheets=schsheets.sheet_of(d))
    missing = sorted(set(fps) - set(L.PLACE))
    if missing:
        sys.exit(f"no place in pod_layout.PLACE for: {missing}")
    for ref, fp in fps.items():
        r = fp.Reference()
        r.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.0), pcbnew.FromMM(1.0)))
        r.SetTextThickness(pcbnew.FromMM(0.15))
        _centre(fp, *L.PLACE[ref])
    fps.update(pcbgen.add_holes(board, L.HOLES, L.HOLE_KEEP, footprint="MountingHole:MountingHole_2.7mm_M2.5"))
    for ref, fp in fps.items():
        if ref.startswith("H"):
            fp.SetValue("M2.5")
    import layout as adapter_layout      # its label placer, on this board's size
    adapter_layout.W, adapter_layout.H, adapter_layout.TILES = L.W, L.H, []
    adapter_layout.LABEL_BELOW, adapter_layout.LABEL_LEFT = set(), set()
    crowded = adapter_layout.place_labels(fps)
    if crowded:
        print("labels with no clear spot (hidden):", " ".join(sorted(crowded)))
    pcbgen.outline(board, 0, 0, L.W, L.H)
    # In1 is the GND plane; In2 is poured with 3V3 at routing time
    pcbgen.zone(board, nets["GND"], pcbnew.In1_Cu, 0, 0, L.W, L.H, clearance=0.25, solid=True)
    # the patch's feed point: no copper on any layer (the footprint's own rule area is F.Cu only)
    u3 = fps["U3"]
    a = math.radians(u3.GetOrientationDegrees())
    fx, fy, fr = L.FEED
    cx = pcbnew.ToMM(u3.GetPosition().x) + fx * math.cos(a) + fy * math.sin(a)
    cy = pcbnew.ToMM(u3.GetPosition().y) - fx * math.sin(a) + fy * math.cos(a)
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName("GPS patch feed point")
    ls = pcbnew.LSET()
    for layer in (pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowZoneFills(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    o = z.Outline()
    o.NewOutline()
    for k in range(32):
        o.Append(pcbnew.FromMM(cx + fr * math.cos(2 * math.pi * k / 32)),
                 pcbnew.FromMM(cy + fr * math.sin(2 * math.pi * k / 32)))
    board.Add(z)
    board.Save(PCB)
    print("placed", len(fps) - len(L.HOLES), "footprints,", len(L.HOLES), "mounting holes")
    drc(unrouted=True)


# track widths in um; anything not listed is SIGNAL_WIDTH. A class is no wider than the smallest pad
# it lands on (Freerouting does not neck down): 3V3 reaches the IMU's 0.25 mm lands, so it stays a signal
WIDTHS = {
    500: ["VIN", "VIN_F", "VSYS"],
    400: ["VBUS", "3V3_GNSS"],
}
FREEROUTING_JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser(
    "~/.local/share/freerouting/freerouting-2.5.0.jar"))
# The finished routing, frozen: every track and via, written when a route ends with nothing left open.
# While it exists `route` replays it, so a rebuild gives the same board; POD_REROUTE=1 routes afresh.
FROZEN = os.path.join(HERE, "routing.json")


def _width_of(net):
    for w, nets in WIDTHS.items():
        if net.lstrip("/") in nets:
            return w / 1000
    return SIGNAL_WIDTH / 1000


def _clearance_of(net):
    return SIGNAL_CLEARANCE / 1000


def _dsn_postprocess(text, loose=()):
    """In1 is the GND plane, In2 the 3V3 plane (signals on F.Cu / B.Cu); supply nets get width classes."""
    import re
    for layer in ("In1.Cu", "In2.Cu"):
        text = text.replace(f"(layer {layer}\n      (type signal)", f"(layer {layer}\n      (type power)")
    # the default rule comes from the board's Default net class, which LoadBoard doesn't read
    text = re.sub(r"\(rule\s+\(width [\d.]+\)\s+\(clearance [\d.]+\)",
                  f"(rule\n      (width {SIGNAL_WIDTH})\n      (clearance {SIGNAL_CLEARANCE})", text, count=1)
    # every GND / 3V3 pad has its own via into its plane: give Freerouting one pin of each only, and
    # the pads the fan-out found no room for (`loose`: {net: ["U6.3", ...]}), which it then routes itself
    for net in ("3V3", "GND"):
        more = "".join(" " + p.replace(".", "-") for p in loose.get(net, ()))
        text = re.sub(r"\(net %s\s+\(pins (\S+)[^)]*\)" % net,
                      lambda m, net=net, more=more: f"(net {net}\n      (pins {m.group(1)}{more})", text)
    m = re.search(r"\(class kicad_default (.*?)\(circuit", text, re.S)
    names = m.group(1).split()
    special = {n for nets in WIDTHS.values() for n in nets}
    bare = lambda n: n.strip('"').lstrip("/")    # noqa: E731
    keep = [n for n in names if bare(n) not in special]
    classes = ""
    for w, nets in WIDTHS.items():
        classes += (f"(class w{w} {' '.join(nets)}\n      (circuit\n        (use_via \"Via[0-3]_600:300_um\")\n"
                    f"        (use_layer F.Cu B.Cu)\n      )\n      (rule\n        (width {w})\n"
                    f"        (clearance {SIGNAL_CLEARANCE})\n      )\n    )\n    ")
    return text[:m.start()] + classes + "(class kicad_default " + " ".join(keep) + "\n      (circuit" + text[m.end():]


def _placement(board):
    import pcbnew
    return {fp.GetReference(): [round(pcbnew.ToMM(fp.GetPosition().x), 4), round(pcbnew.ToMM(fp.GetPosition().y), 4),
                                round(fp.GetOrientationDegrees(), 2)] for fp in board.GetFootprints()}


def freeze():
    """Save the current board's routing (every track and via) to gen/routing.json."""
    import pcbnew
    board = pcbnew.LoadBoard(PCB)
    items = []
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            items.append({"via": [t.GetPosition().x, t.GetPosition().y], "d": t.GetWidth(pcbnew.F_Cu),
                          "drill": t.GetDrillValue(), "net": t.GetNetname()})
        elif t.Type() == pcbnew.PCB_TRACE_T:
            items.append({"trk": [t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y], "w": t.GetWidth(),
                          "layer": board.GetLayerName(t.GetLayer()), "net": t.GetNetname()})
        else:
            sys.exit(f"freeze: unsupported track type {t.GetClass()} (arcs?)")
    with open(FROZEN, "w") as f:
        json.dump({"placement": _placement(board), "items": items}, f, indent=0)
    print(f"froze {len(items)} tracks / vias into {FROZEN}")


def _replay(board):
    import pcbnew
    with open(FROZEN) as f:
        data = json.load(f)
    now = _placement(board)
    moved = sorted(r for r in set(now) | set(data["placement"]) if now.get(r) != data["placement"].get(r))
    if moved:
        sys.exit(f"placement changed since the routing was frozen ({', '.join(moved[:12])}"
                 f"{' ...' if len(moved) > 12 else ''}): route afresh with POD_REROUTE=1, or move them back")
    for it in data["items"]:
        n = board.FindNet(it["net"])
        if "via" in it:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(*it["via"]))
            v.SetWidth(it["d"])
            v.SetDrill(it["drill"])
            v.SetNet(n)
            board.Add(v)
        else:
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I(*it["trk"][:2]))
            t.SetEnd(pcbnew.VECTOR2I(*it["trk"][2:]))
            t.SetWidth(it["w"])
            t.SetLayer(board.GetLayerID(it["layer"]))
            t.SetNet(n)
            board.Add(t)
    return len(data["items"])


def _keepouts():
    """Squares no via goes in: the mounting holes and the GPS patch's feed point."""
    import pod_layout as L
    k = L.HOLE_KEEP + 0.4
    out = [(x - k, y - k, x + k, y + k) for x, y in L.HOLES]
    fx, fy, fr = L.FEED     # the module is turned 180 degrees: the feed point is at centre - offset
    cx, cy = L.GNSS[0] - fx, L.GNSS[1] - fy
    out.append((cx - fr - 0.5, cy - fr - 0.5, cx + fr + 0.5, cy + fr + 0.5))
    return out


def _pours(board, stitch):
    """After routing: GND stitching, GND pours on F.Cu / B.Cu, the 3V3 plane on In2."""
    import pcbnew
    import pcbgen
    import pod_layout as L
    if stitch:
        print("stitching vias:", pcbgen.stitch(board, "GND", 2, 2, L.W - 2, L.H - 2, pitch=3.0,
                                                keepouts=_keepouts(), clearance=0.25))
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        pcbgen.zone(board, gnd, layer, 0, 0, L.W, L.H, clearance=0.3, solid=False)
    if not any(z.GetNetname() == "3V3" and z.IsOnLayer(pcbnew.In2_Cu) for z in board.Zones()):
        pcbgen.zone(board, board.FindNet("3V3"), pcbnew.In2_Cu, 0, 0, L.W, L.H, clearance=0.3, solid=True)
    # thermal relief on the pours (no tombstoned 0603s); big pads, exposed pads and through-hole pins solid
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() not in ("GND", "3V3"):
                continue
            sz = pad.GetSize(pcbnew.F_Cu)
            if pcbnew.ToMM(sz.x) * pcbnew.ToMM(sz.y) >= 3.0 or pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def route():
    """GND / 3V3 fan-out to the planes, Freerouting for the rest on F.Cu / B.Cu, the grid finisher for
    what it leaves, stitching and pours, DRC. Run `pcb` first (as its own command)."""
    import pcbnew
    import pcbgen
    import pod_layout as L
    design()
    board = pcbnew.LoadBoard(PCB)
    if os.path.exists(FROZEN) and not os.environ.get("POD_REROUTE"):
        print(f"routing: replaying {_replay(board)} frozen tracks / vias from gen/routing.json")
        _pours(board, stitch=False)     # the stitching vias are part of the frozen routing
        board.Save(PCB)
        return drc()
    work = os.path.join(PROJ, "route")
    os.makedirs(work, exist_ok=True)
    keep = _keepouts()
    loose = {}
    for net, layer in (("GND", pcbnew.In1_Cu), ("3V3", pcbnew.In2_Cu)):
        loose[net] = pcbgen.fanout(board, net, layer, keepouts=keep)
        print(f"{net} fan-out, pads without a via (left to Freerouting):", loose[net] or "none")
    for t in board.GetTracks():
        t.SetLocked(True)
    pre = {_tkey(t) for t in board.GetTracks()}
    pcbgen.zone(board, board.FindNet("3V3"), pcbnew.In2_Cu, 0, 0, L.W, L.H, clearance=0.3, solid=True)
    dsn, ses = os.path.join(work, NAME + ".dsn"), os.path.join(work, NAME + ".ses")

    def freeroute(log):
        pcbnew.ExportSpecctraDSN(board, dsn)
        with open(dsn) as f:
            text = _dsn_postprocess(f.read(), loose)
        with open(dsn, "w") as f:
            f.write(text)
        if os.path.exists(ses):
            os.remove(ses)
        subprocess.run(["timeout", "1200", "java", "-jar", FREEROUTING_JAR, "-de", dsn, "-do", ses,
                        "--gui.enabled=false", "--feature_flags.multi_threading=false", "-mp", "200",
                        "--router.automatic_neckdown=false", "--router.copper_to_edge_clearance_um=400",
                        "--usage_and_diagnostic_data.disable_analytics=true"],
                       stdout=open(os.path.join(work, log), "w"), stderr=subprocess.STDOUT)
        if not os.path.exists(ses):
            sys.exit(f"freerouting produced no session; see route/{log}")
        pcbnew.ImportSpecctraSES(board, ses)
        for t in board.GetTracks():   # Freerouting necks down at some pads despite the setting
            if t.Type() == pcbnew.PCB_TRACE_T and t.GetWidth() < pcbnew.FromMM(0.15):
                t.SetWidth(pcbnew.FromMM(0.15))

    def unrouted():
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        board.BuildConnectivity()
        return board.GetConnectivity().GetUnconnectedCount(False)

    freeroute("freerouting.log")
    for i in range(2):
        left = unrouted()
        print(f"unrouted after pass {i + 1}: {left}")
        if not left:
            break
        for t in board.GetTracks():     # only the fan-out stays fixed: Freerouting may rip up its own tracks
            t.SetLocked(_tkey(t) in pre)
        freeroute(f"freerouting-{i + 2}.log")
    _pours(board, stitch=True)
    import maze
    left = maze.finish(board, _width_of, _clearance_of, keep, skip=())
    print("nets still in pieces after the finisher:", left or "none")
    print("dangling tracks / vias removed:", maze.prune(board))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(PCB)
    drc()
    board.BuildConnectivity()
    if not board.GetConnectivity().GetUnconnectedCount(False):
        freeze()


def _tkey(t):
    import pcbnew
    if t.Type() == pcbnew.PCB_VIA_T:
        return ("via", t.GetPosition().x, t.GetPosition().y)
    return ("trk", t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y, t.GetLayer())


def drc(unrouted=False):
    rpt = os.path.join(PROJ, "drc.rpt")
    run(["kicad-cli", "pcb", "drc", "--schematic-parity", "--severity-all", PCB, "-o", rpt], capture_output=True)
    text = open(rpt).read()
    for line in text.splitlines():
        if line.startswith("** Found"):
            print(line.strip())
    if unrouted:    # before routing only the placement matters: list everything but the open connections
        body = text.split("** Found")[1].split("** Found")[0]
        kinds = {}
        for line in body.splitlines():
            if line.startswith("["):
                k = line[1:].split("]")[0]
                kinds[k] = kinds.get(k, 0) + 1
        print("placement DRC:", kinds or "clean")


def view():
    """Pictures for a placement review: view/lora-pod-top.svg (pads, outlines, references) and a 3D render."""
    out = os.path.join(PROJ, "view")
    os.makedirs(out, exist_ok=True)
    run(["kicad-cli", "pcb", "export", "svg", "--layers",
         "F.Cu,F.Silkscreen,F.Courtyard,Edge.Cuts", "--mode-single", "--page-size-mode", "2",
         "--exclude-drawing-sheet", "-o", os.path.join(out, NAME + "-top.svg"), PCB], capture_output=True)
    run(["kicad-cli", "pcb", "render", "--side", "top", "--width", "1900", "--height", "1100", "--quality", "basic",
         "-o", os.path.join(out, NAME + "-3d-top.png"), PCB], capture_output=True)
    print(sorted(os.listdir(out)))


if __name__ == "__main__":
    sys.path[:0] = [HERE, SHARED]
    os.chdir(HERE)
    steps = sys.argv[1:] or ["sch"]
    if "all" in steps:
        steps = ["sch", "pcb", "view"]
    for s in steps:
        globals()[s]()
