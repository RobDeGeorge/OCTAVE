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

MM = pcbnew.FromMM

# F.Cu areas no foreign net may cross: U7's VIN / SW side + the inductor + the catch diode, and
# the LDO's tab copper
BUCK_KEEPOUT = (layout.BX - 4.0, layout.BY - 9.0, layout.BX + 20.0, layout.BY + 0.1)
LDO_TAB = layout.LDO_TAB
KEEPOUTS = [BUCK_KEEPOUT, LDO_TAB]


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
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
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
    r_rt, r_fb_lo, r_fb_hi = ref("power", "162k"), ref("power", "10.2k"), ref("power", "57.6k")
    r_comp, c_comp, c_hf = ref("power", "13k"), ref("power", "6.8n"), ref("power", "39p")

    # VIN: both input caps straight onto pin 2
    vin = P(u7, "2")
    c0, c1 = P(cin[0], "1"), P(cin[1], "1")
    track(board, "/VBAT_P", [c0, vin], 0.6)
    track(board, "/VBAT_P", [c1, (c1[0], c0[1])], 1.0)
    # BOOT across pins 1 / 8
    boot, sw = P(u7, "1"), P(u7, "8")
    b1, b2 = P(cboot, "1"), P(cboot, "2")
    track(board, "/BUCK_BOOT", [boot, (boot[0], b1[1]), b1], 0.4)
    track(board, "/BUCK_SW", [b2, (sw[0], b2[1]), sw], 0.4)
    # SW: pin 8 -> inductor pad -> catch-diode cathode, wide and short
    lsw, dk = P(l1, "1"), P(d5, "1")
    track(board, "/BUCK_SW", [sw, (lsw[0] - 3.0, sw[1])], 0.6)
    track(board, "/BUCK_SW", [(lsw[0] + 3.0, lsw[1]), dk], 2.0)
    # 5V_BUCK: inductor pad up into the three output caps, then the OR diode's anode
    lout = P(l1, "2")
    for c in cout:
        cx, cy = P(c, "1")
        track(board, "/5V_BUCK", [(cx, cy), (cx, lout[1])], 1.2)
    xs = [P(c, "1")[0] for c in cout]
    cy = P(cout[0], "1")[1]
    track(board, "/5V_BUCK", [(xs[0], cy), (xs[-1], cy)], 1.2)
    a6 = P(d6, "2")
    track(board, "/5V_BUCK", [(xs[-1], cy), (a6[0], cy), a6], 1.0)
    # GND: pin 7 into the exposed pad; extra vias at the catch diode and the cap grounds
    g7 = P(u7, "7")
    q = board.FindFootprintByReference(u7).GetPosition()
    ep = (pcbnew.ToMM(q.x), pcbnew.ToMM(q.y))
    track(board, "/GND", [g7, (ep[0] + 0.9, g7[1])], 0.5)
    ax, ay = P(d5, "2")
    for dx, dy in ((-2.0, 0.4), (-2.0, 1.6), (2.0, 0.4), (2.0, 1.6)):
        via(board, "/GND", ax + dx, ay + dy)
        track(board, "/GND", [(ax + dx, ay + dy), (ax + dx * 0.5, ay + dy)], 0.6)
    for c in cin:
        gx, gy = P(c, "2")
        via(board, "/GND", gx - 1.3, gy - 0.45)
        via(board, "/GND", gx - 1.3, gy + 0.45)
        track(board, "/GND", [(gx - 1.3, gy - 0.45), (gx, gy), (gx - 1.3, gy + 0.45)], 0.5)
    for c in cout:
        gx, gy = P(c, "2")
        via(board, "/GND", gx, gy - 1.2)
        track(board, "/GND", [(gx, gy), (gx, gy - 1.2)], 0.6)
    # FB, COMP, RT: pin to part, nothing else on these nets
    fb = P(u7, "5")
    lo, hi = P(r_fb_lo, "1"), P(r_fb_hi, "2")
    track(board, "/BUCK_FB", [fb, (lo[0], fb[1] + 0.6), lo], 0.25)
    track(board, "/BUCK_FB", [lo, hi], 0.25)
    comp = P(u7, "6")
    rc1, cc1 = P(r_comp, "1"), P(c_hf, "1")
    track(board, "/BUCK_COMP", [comp, rc1], 0.25)
    track(board, "/BUCK_COMP", [rc1, cc1], 0.25)
    track(board, "/BUCK_COMP_RC", [P(r_comp, "2"), P(c_comp, "1")], 0.25)
    rt = P(u7, "4")
    track(board, "/BUCK_RT", [rt, (rt[0], rt[1] + 0.9), P(r_rt, "1")], 0.25)

    # car input: J6.1 -> PTC, PTC -> TVS and the FET's source pins, drain pins tied
    j6 = ref("car", "OBD (Micro-Fit 2x5)")
    f1 = ref("power", "2A 60V")
    tvs = ref("power", "5.0SMDJ26CA")
    fet = ref("power", "AO4264E")
    j1, fa, fb2 = P(j6, "1"), P(f1, "1"), P(f1, "2")
    track(board, "/VBAT_IN", [j1, (j1[0], 30.5), (fa[0], 30.5), (fa[0], fa[1] - 1.5)], 1.2)
    t1 = P(tvs, "1")
    track(board, "/VBAT_F", [fb2, (t1[0], t1[1])], 1.2)
    s1, s3 = P(fet, "1"), P(fet, "3")
    track(board, "/VBAT_F", [(fb2[0] + 0.8, P(fet, "2")[1]), (s1[0], P(fet, "2")[1])], 0.6)
    track(board, "/VBAT_F", [s1, s3], 0.6)
    d8, d5p = P(fet, "8"), P(fet, "5")
    track(board, "/VBAT_P", [d8, d5p], 0.6)

    # LM74700 (rotated 180: CATHODE / GATE / EN on the left, ANODE / GND / VCAP on the right)
    lm = ref("power", "LM74700QDBVRQ1")
    c_in, c_cap = ref("power", "100n", 0), ref("power", "100n", 1)
    an, ca, ga, en, vc = P(lm, "3"), P(lm, "4"), P(lm, "5"), P(lm, "6"), P(lm, "1")
    mid = (an[0] + en[0]) / 2
    track(board, "/VBAT_F", [an, (mid, an[1]), (mid, en[1]), en], 0.25)    # ANODE to EN under the body
    track(board, "/VBAT_F", [an, P(c_cap, "2")], 0.3)
    track(board, "/IDEAL_VCAP", [vc, (P(c_cap, "1")[0], vc[1]), P(c_cap, "1")], 0.3)
    ci = P(c_in, "1")
    track(board, "/VBAT_F", [en, (ci[0], en[1]), ci], 0.4)
    track(board, "/VBAT_F", [(ci[0], en[1]), (t1[0] + 1.6, en[1]), (t1[0] + 0.6, en[1] - 1.0), (t1[0] + 0.6, t1[1] + 1.3)], 0.6)
    g4 = P(fet, "4")
    track(board, "/IDEAL_GATE", [ga, (g4[0] - 0.12, ga[1]), (g4[0] - 0.12, g4[1] + 0.3), g4], 0.3)
    track(board, "/VBAT_P", [ca, (ca[0], ca[1] - 0.9), (d5p[0], ca[1] - 0.9), d5p], 0.4)

    # buck FB divider top: 5V_BUCK sensed at the output caps, run on B.Cu (under the In1 plane, away from SW)
    sense = P(r_fb_hi, "1")
    a6x = P(d6, "2")[0] - 0.85
    via(board, "/5V_BUCK", sense[0], sense[1] + 1.4)
    via(board, "/5V_BUCK", a6x, cy + 1.1)
    track(board, "/5V_BUCK", [sense, (sense[0], sense[1] + 1.4)], 0.3)
    track(board, "/5V_BUCK", [(sense[0], sense[1] + 1.4), (a6x, sense[1] + 1.4), (a6x, cy + 1.1)], 0.3, pcbnew.B_Cu)
    track(board, "/5V_BUCK", [(a6x, cy + 1.1), (a6x, cy)], 0.4)

    # LDO tab: vias into the In2 3V3 plane beside the tab (the F.Cu tab copper is poured after routing)
    ldo = ref("power", "AP2114H-3.3")
    tab_pad = max((pd for pd in board.FindFootprintByReference(ldo).Pads() if pd.GetNumber() == "2"),
                  key=lambda pd: pd.GetSize(pcbnew.F_Cu).y)
    tx, ty = _xy(tab_pad)
    for vy in (ty - 2.4, ty - 1.2, ty, ty + 1.2, ty + 2.4):
        via(board, "/3V3", tx + 1.7, vy)
    track(board, "/3V3", [(tx + 1.7, ty - 2.4), (tx + 1.7, ty + 2.4)], 0.8)
    track(board, "/3V3", [(tx, ty), (tx + 1.7, ty)], 1.2)
