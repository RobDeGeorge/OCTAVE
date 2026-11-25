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
# ... less its bottom-left corner, where the J1850 TVSs' tile stands (layout.ESD_AT): two rectangles
# ... plus the compensation and feedback parts under the IC's right side (all wired here: nothing else has
# business beside COMP and FB)
_BUCK_KEEPOUTS = [(BUCK_KEEPOUT[0], BUCK_KEEPOUT[1], BUCK_KEEPOUT[2], layout.ESD_AT[1] - 0.3),
                  (layout.ESD_AT[0] + 6.3, layout.ESD_AT[1] - 0.3, BUCK_KEEPOUT[2], BUCK_KEEPOUT[3]),
                  (layout.BX + 3.6, BUCK_KEEPOUT[3], BUCK_KEEPOUT[2], layout.BY + 6.3)]
LDO_TAB = layout.LDO_TAB
# The plug's tab (x -1.3 to 0, inside the adapter board's slot) and the first 0.3 mm behind the
# adapter's back face, where the joint fillets sit: no track or via on any layer. The route of
# 2026-11-08 ran CAN2_H, CAN2_L and LLINE over the tab, up to 0.7 mm past the board's edge (REVIEW 50)
TAB_KEEPOUT = (layout.X0 - mklib.ADAPTER[2] - 1.0, layout.PLUG[1] - mklib.TAB / 2,
               layout.X0 + 0.3, layout.PLUG[1] + mklib.TAB / 2)
# F.Cu under the ESP32 module from its ground pad to the antenna end: the autorouter ran battery voltage
# round that pad once. (Not the 8 mm nearest the STM32: closed as well, the switched-battery tile on the
# back there could not be wired at all.)
MODULE_KEEPOUT = (layout.ESP_BODY[0] + 8.2, layout.ESP_BODY[1] + 2.1, layout.W, layout.ESP_BODY[3] - 2.1)
KEEPOUTS = _BUCK_KEEPOUTS + [LDO_TAB, TAB_KEEPOUT, MODULE_KEEPOUT]


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
    # (the trunk runs 3.15 mm left of pin 2, so the VIN HF cap fits between it and the pins, its courtyard
    # clear of U7's and its GND pad 0.3 mm from the trunk's stub)
    jx = vin[0] - 3.15
    track(board, "VBAT_P", [c1, c0, (jx, c0[1]), (jx, vin[1])], 1.0)
    track(board, "VBAT_P", [(jx, vin[1]), vin], 0.6)
    # VIN HF cap (REVIEW #72): the 2.2u pair sits 6 mm from pin 2, so a 100n beside the pin carries the
    # switching edges. Pad 1 lies on the pin-2 track; pad 2 drops to a via into In1, which runs straight
    # under the IC to its thermal-pad vias
    chf = ref("power", "100n", 2)
    h1, h2 = P(chf, "1"), P(chf, "2")
    track(board, "VBAT_P", [(h1[0], vin[1]), h1], 0.6)
    gvy = h2[1] + 1.0
    track(board, "GND", [h2, (h2[0], gvy)], 0.6)
    via(board, "GND", h2[0], gvy)
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
    # (the via between the two input caps stands 1.25 mm above their GND pads' middle, in the strip's edge:
    # level with them it touched both pads)
    for vx, vy in (((g0[0] + g1[0]) / 2, ay - 1.25), (ax - 0.8, ay - 2.0), (ax + 0.8, ay - 2.0)):
        via(board, "GND", vx, vy)
    track(board, "GND", [((g0[0] + g1[0]) / 2, ay - 1.25), ((g0[0] + g1[0]) / 2, ay)], 0.6)
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
    tvs = ref("power", "5.0SMDJ33CA")
    fet = ref("power", "AO4264E")
    j16, fa, fb2 = P(j6, "16"), P(f1, "1"), P(f1, "2")
    # (the lane runs 0.45 mm right of the PTC pad's middle: left of it, between the plug's pads 12-15 and
    # this track's 0.6 mm clearance, those pins' vias need their room)
    lx = fa[0] + 0.45
    track(board, "VBAT_IN", [j16, (lx, j16[1]), (lx, fa[1])], 1.6)
    track(board, "VBAT_IN", [(lx, fa[1]), fa], 1.6)
    t1 = P(tvs, "1")
    track(board, "VBAT_F", [fb2, (t1[0], t1[1])], 1.2)
    s1, s3 = P(fet, "1"), P(fet, "3")
    track(board, "VBAT_F", [(fb2[0] + 0.8, P(fet, "2")[1]), (s1[0], P(fet, "2")[1])], 1.0)
    track(board, "VBAT_F", [s1, s3], 0.6)
    d8, d5p = P(fet, "8"), P(fet, "5")
    track(board, "VBAT_P", [d8, d5p], 0.6)
    # VBAT_P's trunk (review 2026-11-24: everything behind the FET hung on the LM74700's 0.4 mm cathode
    # sense track and one via). From the drain pins' tips: up between the FET and the 10u, left under the
    # buck's small parts and up into the buck's own VIN trunk; down to the 10u and round the LM74700 into
    # the 47u. (The router takes its branches from the via at the buck's input.)
    tx = d8[0] + 1.1                    # just past the drain pads' tips
    ty = layout.BUCK_BOX[3] + 0.3       # between the buck's RT / FB resistors and the PTC's and the FET's pads
    c10 = P(ref("power", "10u", 0), "1")
    c47 = P(ref("power", "47u 50V"), "1")
    for pin in ("5", "6", "7", "8"):
        track(board, "VBAT_P", [P(fet, pin), (tx, P(fet, pin)[1])], 0.6)
    ny = BUCK_KEEPOUT[3] + 2.4          # (0.6 mm past the VIN cap's GND pad, as the stub into pin 2 is; 1.0 from there)
    track(board, "VBAT_P", [(jx, BUCK_KEEPOUT[3] + 0.9), (jx, ny)], 0.6)
    track(board, "VBAT_P", [(jx, ny), (jx, ty), (tx, ty), (tx, c10[1])], 1.0)
    track(board, "VBAT_P", [(tx, c10[1]), c10], 1.0)
    bx = tx + 0.55                      # past the VCAP cap, clear of the 10u's GND pad
    by = c47[1] - 3.75                  # over the 47u's can
    track(board, "VBAT_P", [(tx, c10[1]), (bx, c10[1] + 0.9), (bx, by), (c47[0], by), c47], 0.8)


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

    # buck FB divider top: 5V_BUCK sensed at the output caps, run on In2 (under the In1 plane, away from SW;
    # not on B.Cu: other circuits' parts stand there, under the inductor)
    sense = P(r_fb_hi, "1")
    a6x = P(d6, "2")[0] - 0.85
    via(board, "5V_BUCK", sense[0], sense[1] + 1.4)
    via(board, "5V_BUCK", a6x, cy + 1.1)
    track(board, "5V_BUCK", [sense, (sense[0], sense[1] + 1.4)], 0.3)
    track(board, "5V_BUCK", [(sense[0], sense[1] + 1.4), (a6x, sense[1] + 1.4), (a6x, cy + 1.1)], 0.3, pcbnew.In2_Cu)
    track(board, "5V_BUCK", [(a6x, cy + 1.1), (a6x, cy)], 0.4)

    # LDO tab: vias into the 3V3 plane (In4) beside the tab (the F.Cu tab copper is poured after routing)
    ldo = ref("power", "AP2114H-3.3")
    tab_pad = max((pd for pd in board.FindFootprintByReference(ldo).Pads() if pd.GetNumber() == "2"),
                  key=lambda pd: pd.GetSize(pcbnew.F_Cu).y)
    tx, ty = _xy(tab_pad)
    for vy in (ty - 2.4, ty - 1.2, ty, ty + 1.2, ty + 2.4):
        via(board, "3V3", tx + 1.7, vy)
    track(board, "3V3", [(tx + 1.7, ty - 2.4), (tx + 1.7, ty + 2.4)], 0.8)
    track(board, "3V3", [(tx, ty), (tx + 1.7, ty)], 1.2)
    for vy in (ty - 1.35, ty - 0.45, ty + 0.45, ty + 1.35):      # four more in the tab itself (filled and capped)
        via(board, "3V3", tx, vy)

    # USB hub (QFN-16): its exposed pad is its only ground and is ringed by its pins, so the fan-out finds
    # no place beside it. Four small vias in the pad, into the GND plane (0.2 mm drill; the board is
    # ordered with filled and capped vias)
    hub = ref("usb", "CH334P")
    hx, hy = P(hub, "17")
    for dx, dy in ((-0.45, -0.45), (0.45, -0.45), (-0.45, 0.45), (0.45, 0.45)):
        via(board, "GND", hx + dx, hy + dy, d=0.45, drill=0.2)

    # The high-speed USB pair, socket -> ESD array -> hub, by hand (the autorouter's had 4-6 mm of skew and one
    # line alone on an inner layer). D+ joins its two socket pads on F.Cu under the pad row and drops to B.Cu;
    # D- drops from each of its pads and is joined on B.Cu. Then both run on B.Cu, 0.4-0.6 mm apart: up past
    # the socket's locating hole, through the gap between the shell's two stakes, through the array's pads
    # (one line each side of its GND / supply pins) and into the hub's upstream pins.
    j2, esd = ref("usb", "USB-C"), ref("usb", "USBLC6-2SC6")
    BC = pcbnew.B_Cu

    def pads_of(r, net):
        return sorted(_xy(pd) for pd in board.FindFootprintByReference(r).Pads() if pd.GetNetname() == net)
    (dp_b6, dp_a6), (dm_a7, dm_b7) = pads_of(j2, "USB_DP"), pads_of(j2, "USB_DM")
    (dp_l, dp_r), (dm_l, dm_r) = pads_of(esd, "USB_DP"), pads_of(esd, "USB_DM")
    h_dp, h_dm = P(hub, "12"), P(hub, "11")
    yb, yv = dp_b6[1] + 1.9, dm_a7[1] + 1.35          # the D+ bridge, the D- vias: under the socket's pad row
    xp, xm = dp_b6[0] - 1.45, dp_b6[0] - 1.05         # the two lines' way up, right of the locating hole
    yp, ym_ = dp_r[1] - 0.72, dm_r[1] + 0.58          # ... and left, between the shell's stakes
    track(board, "USB_DP", [dp_b6, (dp_b6[0], yb)], 0.15)
    track(board, "USB_DP", [dp_a6, (dp_a6[0], yb), (dp_b6[0] - 0.55, yb)], 0.15)
    via(board, "USB_DP", dp_b6[0] - 0.55, yb, d=0.45, drill=0.2)
    track(board, "USB_DP", [(dp_b6[0] - 0.55, yb), (xp, yb), (xp, yp), (dp_r[0] + 1.72, yp), (dp_r[0] + 1.0, dp_r[1]),
                            dp_r, dp_l], 0.15, BC)
    for p_ in (dm_a7, dm_b7):
        track(board, "USB_DM", [p_, (p_[0], yv)], 0.15)
        via(board, "USB_DM", p_[0], yv, d=0.45, drill=0.2)
    track(board, "USB_DM", [(dm_b7[0], yv), (xm, yv), (xm, ym_), (dm_r[0] + 1.58, ym_), (dm_r[0] + 1.0, dm_r[1]),
                            dm_r, dm_l], 0.15, BC)
    hx1 = pcbnew.ToMM(_pad(board, hub, "12").GetBoundingBox().GetRight())
    xv = (hx1 + dp_l[0] - 0.66) / 2 + 0.04            # between the hub's pin tips and the array's pads
    track(board, "USB_DM", [dm_l, (xv, dm_l[1]), (xv, h_dm[1]), h_dm], 0.15, BC)
    track(board, "USB_DP", [dp_l, (hx1 + 0.4, dp_l[1]), (hx1 + 0.2, h_dp[1]), h_dp], 0.15, BC)

    # The CAN transceivers' 5 V, on its own path from the OR diode's anode at the output caps: down In2
    # beside the feedback-sense trace (not off it: the router had tapped the sense trace for them) to a via
    # at the MS CAN tile and into its 5 V cap. The router joins the HS CAN tile to that.
    d6a = P(d6, "2")
    can_cap = [p.ref for p in design.parts if p.block.name == "can" and p.value == "100n"][2]
    cc = next(_xy(pd) for pd in board.FindFootprintByReference(can_cap).Pads() if pd.GetNetname() == "5V_BUCK")
    fx, fy0, fy1 = d6a[0] + 0.25, cy + 1.1, cc[1] - 1.25
    track(board, "5V_BUCK", [(fx, d6a[1]), (fx, fy0)], 0.4)
    via(board, "5V_BUCK", fx, fy0)
    track(board, "5V_BUCK", [(fx, fy0), (fx, fy1)], 0.4, pcbnew.In2_Cu)
    via(board, "5V_BUCK", fx, fy1)
    track(board, "5V_BUCK", [(fx, fy1), (cc[0], fy1), cc], 0.25)
    # ... and on down In2 to the HS CAN tile's 5 V cap, the same way
    can_cap2 = [p.ref for p in design.parts if p.block.name == "can" and p.value == "100n"][0]
    c2 = next(_xy(pd) for pd in board.FindFootprintByReference(can_cap2).Pads() if pd.GetNetname() == "5V_BUCK")
    fy2 = c2[1] - 1.25
    track(board, "5V_BUCK", [(fx, fy1), (fx, fy2)], 0.4, pcbnew.In2_Cu)
    via(board, "5V_BUCK", fx, fy2)
    track(board, "5V_BUCK", [(fx, fy2), (c2[0], fy2), c2], 0.25)

    # The plug's two ground pins (4 and 5, on the back): a 0.5 mm bus just outside the no-via strip with
    # four small vias into the GND plane, placed where the top side leaves room between the plug's pads,
    # the PTC's battery pad and the battery lane. (The fan-out finds no legal spot here by itself; the
    # second review found pin 5 with no via within 10 mm.)
    g4, g5 = P(j6, "4"), P(j6, "5")
    bx_ = layout.VIA_KEEPOUTS[0][2] + 0.4
    ys = (g4[1] - 4.0, g5[1] - 0.8, g5[1], g5[1] + 0.8)
    track(board, "GND", [(bx_, ys[0]), (bx_, ys[-1])], 0.5, pcbnew.B_Cu)
    for g in (g4, g5):          # (2 mm: these two stubs are the whole board's return and the TVS's)
        track(board, "GND", [g, (bx_ - 0.75, g[1])], 2.0, pcbnew.B_Cu)
        track(board, "GND", [g, (bx_, g[1])], 0.5, pcbnew.B_Cu)
    for y_ in ys:
        via(board, "GND", bx_, y_, d=0.45, drill=0.2)

    # The LM317L's input cap straight to its input pin, up the tile's edge past the pins' tips (0.3 mm: the
    # gap takes no more with 0.3 mm clearance each side). The router had joined them by 56 mm of track.
    u11 = ref("j1850", "LM317LIDR")
    vi = next(_xy(pd) for pd in board.FindFootprintByReference(u11).Pads() if pd.GetNetname() == "VJ_IN")
    cv = next(_xy(pd) for pd in board.FindFootprintByReference(ref("j1850", "100n", 0)).Pads() if pd.GetNetname() == "VJ_IN")
    tip = min(pcbnew.ToMM(pd.GetBoundingBox().GetLeft()) for pd in board.FindFootprintByReference(u11).Pads())
    ex = tip - 0.45
    track(board, "VJ_IN", [cv, (ex, cv[1]), (ex, vi[1]), vi], 0.3)

    # The pod switch's exposed pad: six vias into the GND plane (it cycles at its thermal limit into a
    # shorted cable), placed between the card socket's shell tabs on the other side
    ep = _pad(board, ref("pod", "TPS27S100APWPR"), "15").GetBoundingBox()
    ex0, ey0 = pcbnew.ToMM(ep.GetLeft()), pcbnew.ToMM(ep.GetTop())
    for dx in (1.5, 2.7):
        for dy in (0.8, 2.5, 4.2):
            via(board, "GND", ex0 + dx, ey0 + dy)

    # The ESP32's supply pin (2, top side) straight down to its 100n, which stands under it on the back:
    # one via just inside the pad row. Without it the pin's only way to its caps was 13-17 mm round by
    # a header pin's barrel.
    esp = ref("esp32", "ESP32-WROOM-32E-N8")
    e2 = P(esp, "2")
    ec = P(ref("esp32", "100n"), "1")
    ev = (e2[0], e2[1] + 1.5)
    track(board, "3V3_ESP", [e2, ev], 0.4)
    via(board, "3V3_ESP", *ev, d=0.45, drill=0.2)
    track(board, "3V3_ESP", [ev, ec], 0.4, pcbnew.B_Cu)
    # ... and its 22u round the tile's edge to the same via (the router had joined the 22u through a header
    # pin, 17 mm away by copper)
    eb = P(ref("esp32", "22u"), "1")
    bbx = _pad(board, ref("esp32", "22u"), "1").GetBoundingBox()
    lx_ = pcbnew.ToMM(bbx.GetLeft()) - 1.0
    track(board, "3V3_ESP", [eb, (lx_, eb[1]), (lx_, ev[1]), ev], 0.4, pcbnew.B_Cu)
    # That track and the 22u close the EN pin's two parts in on the back, so EN (pin 3) gets its own way
    # in: from its capacitor up between the 22u's pad and the GND via beside it, a via above the 22u, and
    # 3 mm on the top side into the pin's pad
    e3 = P(esp, "3")
    enr = next(p.ref for p in design.parts if p.block.name == "esp32" and p.value == "1u" and "ESP_EN" in p.nets.values())
    en_c = next(_xy(pd) for pd in board.FindFootprintByReference(enr).Pads() if pd.GetNetname() == "ESP_EN")
    ux, uy = pcbnew.ToMM(bbx.GetRight()) + 0.275, pcbnew.ToMM(bbx.GetTop()) - 0.5
    track(board, "ESP_EN", [en_c, (ux, en_c[1] - 0.5), (ux, uy)], 0.15, pcbnew.B_Cu)
    via(board, "ESP_EN", ux, uy, d=0.45, drill=0.2)
    track(board, "ESP_EN", [(ux, uy), (ux, e3[1] - 1.15), (e3[0] - 0.21, e3[1] - 0.55), e3], 0.15)     # (down past the GND via, into the pad's corner)

    # pod supply switch (HTSSOP-14, 0.65 mm pitch): its three IN pins and its three OUT pins joined through
    # the pads' middles, so the router can leave from the middle pin of each: the outer ones stand 0.25 mm
    # from an unused pin, closer than the battery-side clearance lets a track start
    sw19 = ref("pod", "TPS27S100APWPR")
    s19 = pcbnew.B_Cu if board.FindFootprintByReference(sw19).IsFlipped() else pcbnew.F_Cu
    track(board, "POD_VIN", [P(sw19, "5"), P(sw19, "6"), P(sw19, "7")], 0.2, s19)
    track(board, "POD_F", [P(sw19, "8"), P(sw19, "9"), P(sw19, "10")], 0.2, s19)

    # DLC_SW's switch, in the corner behind the buck's input: the three VBAT_P pads of its tile (the gate
    # resistor, the P-FET's source, the zener) lie in one column. Joined by hand, 0.3 mm: the resistor straight
    # down to the source past the catch diode's GND via, the zener up the tile's edge round the FET's gate pad.
    def box(r, n):
        bb = _pad(board, r, n).GetBoundingBox()
        return pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())
    q2 = ref("dlcsw", "SI2309CDS")
    dz, rg = ref("dlcsw", "BZT52C12"), ref("dlcsw", "47k")
    sd = pcbnew.B_Cu if board.FindFootprintByReference(q2).IsFlipped() else pcbnew.F_Cu
    vp = lambda r: next(pd.GetNumber() for pd in board.FindFootprintByReference(r).Pads() if pd.GetNetname() == "VBAT_P")  # noqa: E731
    src, gate, rb, zb = box(q2, "2"), box(q2, "1"), box(rg, vp(rg)), box(dz, vp(dz))
    assert rb[3] < src[1] < gate[1] < zb[1], "DLC SW tile: not gate resistor / FET / zener from the top down"
    xs = min(rb[2], src[2]) - 0.17
    assert xs - 0.15 >= max(rb[0], src[0]), "DLC SW tile: the gate resistor's VBAT_P pad is not over the FET's source"
    track(board, "VBAT_P", [(xs, (rb[1] + rb[3]) / 2), (xs, (src[1] + src[3]) / 2)], 0.3, sd)
    xl, yj = gate[0] - 0.47, src[3] - 0.15
    track(board, "VBAT_P", [P(dz, vp(dz)), (xl, P(dz, vp(dz))[1]), (xl, yj), (src[0] + 0.3, yj)], 0.3, sd)
    # its feed: one via from that link straight up into the buck's VIN copper, which lies over it on the top
    via(board, "VBAT_P", xl, P(cin[0], "1")[1])

    # The LM317L's follower (SOT-89 on the back): four vias in its collector tab to a patch of DLC_SW copper
    # on the top, under the J5 table's lettering, the only spare copper near it (about 0.25 cm2: it carries
    # the LM317L's current limit when Bus+ is shorted)
    q15 = ref("j1850", "BCX56-16")
    tb = _pad(board, q15, "2").GetBoundingBox()
    tx0, tx1 = pcbnew.ToMM(tb.GetLeft()), pcbnew.ToMM(tb.GetRight())
    tyc = (pcbnew.ToMM(tb.GetTop()) + pcbnew.ToMM(tb.GetBottom())) / 2
    for k in range(4):
        via(board, "DLC_SW", tx1 - 0.5 - k * 0.9, tyc)
    track(board, "DLC_SW", [(tx1 - 3.2, tyc + 0.7), (tx1 - 0.5, tyc + 0.7)], 4.0)

    # K-line: from the plug's pin straight to its TVS, which is the first thing on the line (the router
    # had it last, 34 mm on, behind the driver)
    kp = _pad(board, j6, "7").GetBoundingBox()
    ktv = next(p.ref for p in design.parts if p.value == "SMF24A" and "KLINE" in p.nets.values())
    kt = next(_xy(pd) for pd in board.FindFootprintByReference(ktv).Pads() if pd.GetNetname() == "KLINE")
    kx = layout.RESERVED["pins"][2] - 0.9
    track(board, "KLINE", [(pcbnew.ToMM(kp.GetRight()) - 0.5, P(j6, "7")[1]), (kx, P(j6, "7")[1]), (kx, kt[1]), kt], 0.4, pcbnew.B_Cu)

    # CAN transceivers: each one's 5 V cap straight to its VCC pin (3), round the RXD pin's tip. Left to the
    # router, U10's cap was joined to the rail 33 mm from the pin it serves
    for i in (0, 1):
        u = ref("can", "TCAN1044VDRQ1", i)
        cap = [p.ref for p in design.parts if p.block.name == "can" and p.value == "100n"][2 * i]
        cp = next(_xy(pd) for pd in board.FindFootprintByReference(cap).Pads() if pd.GetNetname() == "5V_BUCK")
        p3, p4 = P(u, "3"), P(u, "4")
        ux = pcbnew.ToMM(board.FindFootprintByReference(u).GetPosition().x)
        xo = p3[0] + (1.48 if p3[0] > ux else -1.48)
        ym = (cp[1] + p4[1]) / 2
        track(board, "5V_BUCK", [cp, (cp[0], ym), (xo, ym), (xo, p3[1]), p3], 0.25)

    # accelerometer (LGA-12, 0.5 mm pitch): the plane fan-out's 0.3 mm stub can't leave its four 3V3 pads
    # (the route of 2026-11-09 left all four open). Each pair (VDD / VDD_IO, 9 / 10; CS / SA0, 2 / 3) is
    # joined and runs straight out of its side of the chip to a small via into the 3V3 plane
    acc = ref("sense", "LIS2DH12TR")
    fa = board.FindFootprintByReference(acc)
    side = pcbnew.B_Cu if fa.IsFlipped() else pcbnew.F_Cu
    ax0 = pcbnew.ToMM(fa.GetPosition().x)
    for a, b in (("10", "9"), ("3", "2")):
        pa, pb = P(acc, a), P(acc, b)
        out = 0.75 if pb[0] > ax0 else -0.75
        track(board, "3V3", [pa, pb, (pb[0] + out, pb[1])], 0.2, side)
        via(board, "3V3", pb[0] + out, pb[1], d=0.45, drill=0.2)
    # ... and its GND pads the fan-out can't reach (5, 8) each run into the GND pad beside it (6, 7)
    for a, b in (("5", "6"), ("8", "7")):
        track(board, "GND", [P(acc, a), P(acc, b)], 0.2, side)
