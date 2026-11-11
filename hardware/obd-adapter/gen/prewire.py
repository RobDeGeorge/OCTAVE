"""Hand-placed copper laid down before the autorouter runs (all of it locked).

The power stage is not left to Freerouting: the buck's hot loop (VIN caps -> VIN -> SW ->
inductor / catch diode -> GND), its boot, feedback and compensation nodes, the inductor-to-
output-cap connection and the car-input chain are wired here from pad to pad, short and wide.
`route()` also tells Freerouting to keep every other net off F.Cu around the buck
(`BUCK_KEEPOUT`), so nothing is routed between the switch node and the IC again.

Coordinates come from the placed pads, so moving a part in layout.py moves its copper.
"""

import pcbnew

import layout
import mklib

MM = pcbnew.FromMM

# F.Cu areas no foreign net may cross: U7's VIN / SW side + the inductor + the catch diode, and
# the LDO's tab copper
BUCK_KEEPOUT = (layout.BX - 11.0, layout.BY - 11.5, layout.BX + 13.0, layout.BY + 0.1)
LDO_TAB = layout.LDO_TAB
# The plug's tab (x -1.3 to 0, inside the adapter board's slot) and the first 0.3 mm behind the
# adapter's back face, where the joint fillets sit: no track or via on any layer. The route of
# 2026-11-08 ran CAN2_H, CAN2_L and LLINE over the tab, up to 0.7 mm past the board's edge (REVIEW 50)
TAB_KEEPOUT = (layout.X0 - mklib.ADAPTER[2] - 1.0, layout.PLUG[1] - mklib.TAB / 2,
               layout.X0 + 0.3, layout.PLUG[1] + mklib.TAB / 2)
KEEPOUTS = [BUCK_KEEPOUT, LDO_TAB, TAB_KEEPOUT]


def on_tab(board):
    """Tracks and vias with copper on the plug's tab (x < the board's left edge): there must be none."""
    out = []
    for t in board.GetTracks():
        bb = t.GetBoundingBox()
        if bb.GetLeft() < MM(layout.X0) and bb.GetBottom() > MM(TAB_KEEPOUT[1]) and bb.GetTop() < MM(TAB_KEEPOUT[3]):
            out.append(f"{t.GetNetname()} ({pcbnew.ToMM(bb.GetLeft()):.2f}, {pcbnew.ToMM(bb.GetTop()):.2f})")
    return out


def _ref(design, block, value, i=0):
    return [p.ref for p in design.parts if p.block.name == block and p.value == value][i]


def _pad(board, ref, num):
    fp = board.FindFootprintByReference(ref)
    for p in fp.Pads():
        if p.GetNumber() == num:
            return p
    raise KeyError(f"{ref}.{num}")


def _xy(pad):
    q = pad.GetPosition()
    return pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)


def track(board, net, pts, width, layer=pcbnew.F_Cu):
    n = board.FindNet(net)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:], strict=False):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(MM(x0), MM(y0)))
        t.SetEnd(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        t.SetWidth(MM(width))
        t.SetLayer(layer)
        t.SetNet(n)
        t.SetLocked(True)
        board.Add(t)


def via(board, net, x, y, d=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    v.SetWidth(MM(d))
    v.SetDrill(MM(drill))
    v.SetNet(board.FindNet(net))
    v.SetLocked(True)
    board.Add(v)


def wire(board, design):
    ref = lambda block, value, i=0: _ref(design, block, value, i)  # noqa: E731
    P = lambda r, n: _xy(_pad(board, r, n))  # noqa: E731

    u7 = ref("power", "TPS54360BDDAR")
    cin = [ref("power", "2.2u", 0), ref("power", "2.2u", 1)]
    cboot = ref("power", "100n", 3)
    l1 = ref("power", "8.2u")
    d5 = ref("power", "B560C")
    cout = [ref("power", "47u", i) for i in range(3)]
    d6 = ref("power", "SS34", 0)
    r_rt, r_fb_lo, r_fb_hi = ref("power", "160k"), ref("power", "12k"), ref("power", "68k")
    r_comp, c_comp, c_hf = ref("power", "13k"), ref("power", "6.8n"), ref("power", "47p")

    # VIN: the input caps' VIN pads joined, then down the IC's left side into pin 2
    vin = P(u7, "2")
    c0, c1 = P(cin[0], "1"), P(cin[1], "1")
    jx = vin[0] - 2.4
    track(board, "VBAT_P", [c1, c0, (jx, c0[1]), (jx, vin[1])], 1.0)
    track(board, "VBAT_P", [(jx, vin[1]), vin], 0.6)
    # ... and out of the keep-out to a via just below it. The island (both caps and pin 2) lies inside the
    # keep-out, which the grid finisher treats as closed to every net, so only Freerouting could reach it,
    # and on 2026-11-09 it left the buck's input cut off from the ideal diode
    vy = BUCK_KEEPOUT[3] + 0.9
    track(board, "VBAT_P", [(jx, vin[1]), (jx, vy)], 0.6)
    via(board, "VBAT_P", jx, vy)
    # BOOT across pins 1 / 8
    boot, sw = P(u7, "1"), P(u7, "8")
    b1, b2 = P(cboot, "1"), P(cboot, "2")
    track(board, "BUCK_BOOT", [boot, (boot[0], b1[1]), b1], 0.4)
    track(board, "BUCK_SW", [b2, (sw[0], b2[1]), sw], 0.4)
    # SW: pin 8 straight up into the catch diode's cathode, and right into the inductor pad
    lsw, dk = P(l1, "1"), P(d5, "1")
    track(board, "BUCK_SW", [sw, (sw[0], dk[1] + 1.0)], 1.2)
    track(board, "BUCK_SW", [sw, (lsw[0] - 3.0, sw[1])], 1.0)
    # 5V_BUCK: inductor pad up into the three output caps, then the OR diode's anode
    lout = P(l1, "2")
    for c in cout:
        cx, cy = P(c, "1")
        track(board, "5V_BUCK", [(cx, cy), (cx, lout[1])], 1.2)
    xs = [P(c, "1")[0] for c in cout]
    cy = P(cout[0], "1")[1]
    track(board, "5V_BUCK", [(xs[0], cy), (xs[-1], cy)], 1.2)
    a6 = P(d6, "2")
    track(board, "5V_BUCK", [(xs[-1], cy), (a6[0], cy), a6], 1.0)
    # GND: pin 7 into the exposed pad; extra vias at the catch diode and the cap grounds
    g7 = P(u7, "7")
    q = board.FindFootprintByReference(u7).GetPosition()
    ep = (pcbnew.ToMM(q.x), pcbnew.ToMM(q.y))
    track(board, "GND", [g7, (ep[0] + 0.9, g7[1])], 0.5)
    # the hot loop's GND: catch-diode anode and both input caps' GND pads on one wide strip, with
    # vias into the In1 plane between them and above the anode
    ax, ay = P(d5, "2")
    g0, g1 = P(cin[0], "2"), P(cin[1], "2")
    track(board, "GND", [g1, (ax, ay)], 1.5)
    for vx, vy in (((g0[0] + g1[0]) / 2, ay), (ax - 0.8, ay - 2.0), (ax + 0.8, ay - 2.0)):
        via(board, "GND", vx, vy)
    track(board, "GND", [(ax - 0.8, ay - 2.0), (ax + 0.8, ay - 2.0)], 0.6)
    # output caps' GND: vias between the pads (and left of the row; D6 is on the right), leaving the
    # strip above the caps for their labels
    gs = [P(c, "2") for c in cout]
    gy = gs[0][1]
    track(board, "GND", [(gs[0][0] - 1.25, gy), gs[-1]], 0.8)
    xs_v = [gs[0][0] - 1.25] + [(a[0] + b[0]) / 2 for a, b in zip(gs, gs[1:], strict=False)]
    for vx in xs_v:
        via(board, "GND", vx, gy)
    # FB, COMP, RT: pin to part, nothing else on these nets
    fb = P(u7, "5")
    lo, hi = P(r_fb_lo, "1"), P(r_fb_hi, "2")
    track(board, "BUCK_FB", [fb, (lo[0], fb[1] + 0.6), lo], 0.25)
    track(board, "BUCK_FB", [lo, hi], 0.25)
    comp = P(u7, "6")
    rc1, cc1 = P(r_comp, "1"), P(c_hf, "1")
    track(board, "BUCK_COMP", [comp, rc1], 0.25)
    track(board, "BUCK_COMP", [rc1, cc1], 0.25)
    track(board, "BUCK_COMP_RC", [P(r_comp, "2"), P(c_comp, "1")], 0.25)
    rt = P(u7, "4")
    track(board, "BUCK_RT", [rt, (rt[0], rt[1] + 0.9), P(r_rt, "1")], 0.25)

    # car input: plug pin 16's edge pad -> right, up layout.VBAT_LANE into the PTC's pad 1; PTC -> TVS
    # and the FET's source pins, drain pins tied
    j6 = ref("car", "OBD-II plug (J1962 male)")
    f1 = ref("power", "2A 60V")
    tvs = ref("power", "5.0SMDJ26CA")
    fet = ref("power", "AO4264E")
    j16, fa, fb2 = P(j6, "16"), P(f1, "1"), P(f1, "2")
    track(board, "VBAT_IN", [j16, (fa[0], j16[1]), fa], 1.6)
    t1 = P(tvs, "1")
    track(board, "VBAT_F", [fb2, (t1[0], t1[1])], 1.2)
    s1, s3 = P(fet, "1"), P(fet, "3")
    track(board, "VBAT_F", [(fb2[0] + 0.8, P(fet, "2")[1]), (s1[0], P(fet, "2")[1])], 0.6)
    track(board, "VBAT_F", [s1, s3], 0.6)
    d8, d5p = P(fet, "8"), P(fet, "5")
    track(board, "VBAT_P", [d8, d5p], 0.6)

    # LM74700 (rotated 180: CATHODE / GATE / EN on the left, ANODE / GND / VCAP on the right)
    lm = ref("power", "LM74700QDBVRQ1")
    c_in, c_cap = ref("power", "100n", 0), ref("power", "100n", 1)
    an, ca, ga, en, vc = P(lm, "3"), P(lm, "4"), P(lm, "5"), P(lm, "6"), P(lm, "1")
    mid = (an[0] + en[0]) / 2
    track(board, "VBAT_F", [an, (mid, an[1]), (mid, en[1]), en], 0.25)    # ANODE to EN under the body
    track(board, "VBAT_F", [an, P(c_cap, "2")], 0.3)
    track(board, "IDEAL_VCAP", [vc, (P(c_cap, "1")[0], vc[1]), P(c_cap, "1")], 0.3)
    ci = P(c_in, "1")
    track(board, "VBAT_F", [en, (ci[0], en[1]), ci], 0.4)
    track(board, "VBAT_F", [(ci[0], en[1]), (t1[0] + 1.6, en[1]), (t1[0] + 0.6, en[1] - 1.0), (t1[0] + 0.6, t1[1] + 1.3)], 0.6)
    g4 = P(fet, "4")
    track(board, "IDEAL_GATE", [ga, (g4[0] - 0.12, ga[1]), (g4[0] - 0.12, g4[1] + 0.3), g4], 0.3)
    track(board, "VBAT_P", [ca, (ca[0], ca[1] - 0.9), (d5p[0], ca[1] - 0.9), d5p], 0.4)

    # buck FB divider top: 5V_BUCK sensed at the output caps, run on B.Cu (under the In1 plane, away from SW)
    sense = P(r_fb_hi, "1")
    a6x = P(d6, "2")[0] - 0.85
    via(board, "5V_BUCK", sense[0], sense[1] + 1.4)
    via(board, "5V_BUCK", a6x, cy + 1.1)
    track(board, "5V_BUCK", [sense, (sense[0], sense[1] + 1.4)], 0.3)
    track(board, "5V_BUCK", [(sense[0], sense[1] + 1.4), (a6x, sense[1] + 1.4), (a6x, cy + 1.1)], 0.3, pcbnew.B_Cu)
    track(board, "5V_BUCK", [(a6x, cy + 1.1), (a6x, cy)], 0.4)

    # LDO tab: vias into the In2 3V3 plane beside the tab (the F.Cu tab copper is poured after routing)
    ldo = ref("power", "AP2114H-3.3")
    tab_pad = max((pd for pd in board.FindFootprintByReference(ldo).Pads() if pd.GetNumber() == "2"),
                  key=lambda pd: pd.GetSize(pcbnew.F_Cu).y)
    tx, ty = _xy(tab_pad)
    for vy in (ty - 2.4, ty - 1.2, ty, ty + 1.2, ty + 2.4):
        via(board, "3V3", tx + 1.7, vy)
    track(board, "3V3", [(tx + 1.7, ty - 2.4), (tx + 1.7, ty + 2.4)], 0.8)
    track(board, "3V3", [(tx, ty), (tx + 1.7, ty)], 1.2)

    # accelerometer (LGA-12, 0.5 mm pitch): the plane fan-out's 0.3 mm stub can't leave its four 3V3 pads
    # (the route of 2026-11-09 left all four open). VDD / VDD_IO (9, 10) run into the 10u beside them,
    # CS / SA0 (2, 3) to a via on the other side
    acc = ref("sense", "LIS2DH12TR")
    p2, p3, p9, p10 = (P(acc, n) for n in ("2", "3", "9", "10"))
    cap = P(ref("sense", "10u"), "1")
    track(board, "3V3", [p10, p9, (cap[0] - abs(cap[1] - p9[1]), p9[1]), cap], 0.2)
    track(board, "3V3", [p3, p2, (p2[0] - 0.89, p2[1])], 0.2)
    via(board, "3V3", p2[0] - 0.89, p2[1])
