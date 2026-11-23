"""The copper laid by hand before the autorouter runs: the boost converter's current loops and the
cell's feed. Positions are layout.py's (mm from the board centre); `wire` checks that each pad it
starts from is on the net it expects, so a moved or turned part fails here and not at the fab.
"""

import pcbnew

import layout

MM = pcbnew.FromMM


def _pt(x, y):
    return pcbnew.VECTOR2I(MM(layout.CX + x), MM(layout.CY + y))


def _pad(board, ref, num, net):
    fp = board.FindFootprintByReference(ref)
    for p in fp.Pads():
        if p.GetNumber() == num:
            if p.GetNetname() != net:
                raise SystemExit(f"prewire: {ref}.{num} is on {p.GetNetname()}, expected {net}")
            pos = p.GetPosition()
            return pcbnew.ToMM(pos.x) - layout.CX, pcbnew.ToMM(pos.y) - layout.CY
    raise SystemExit(f"prewire: no pad {ref}.{num}")


def track(board, net, pts, width, layer=pcbnew.B_Cu):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(_pt(*a))
        t.SetEnd(_pt(*b))
        t.SetWidth(MM(width))
        t.SetLayer(layer)
        t.SetNet(board.FindNet(net))
        board.Add(t)


def via(board, net, x, y, d=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(_pt(x, y))
    v.SetWidth(MM(d))
    v.SetDrill(MM(drill))
    v.SetNet(board.FindNet(net))
    board.Add(v)


def wire(board):
    B, F, IN2 = pcbnew.B_Cu, pcbnew.F_Cu, pcbnew.In2_Cu
    UP = layout.UP
    # --- boost converter (bottom). Balls are 0.2 mm at 0.4 mm pitch: 0.2 mm tracks off them.
    # A2 is its only ground; C2 is VSEL, strapped to ground for 3.3 V
    vin = _pad(board, "U4", "A1", "VBAT")
    sw = _pad(board, "U4", "B1", "BOOST_SW")
    vout = _pad(board, "U4", "B2", "3V3")
    gnd = _pad(board, "U4", "A2", "GND")
    vsel = _pad(board, "U4", "C2", "GND")
    l_in = _pad(board, "L1", "1", "VBAT")
    l_sw = _pad(board, "L1", "2", "BOOST_SW")
    c5 = _pad(board, "C5", "1", "VBAT")
    c6 = _pad(board, "C6", "1", "VBAT")
    out = [_pad(board, c, "1", "3V3") for c in ("C7", "C8")]
    out_g = [_pad(board, c, "2", "GND") for c in ("C7", "C8")]
    c9 = _pad(board, "C9", "1", "3V3")
    if abs(out[0][1] - vout[1]) > 0.05 or out_g[0][1] > gnd[1]:
        raise SystemExit("prewire: C7 is not level with the VOUT ball with its ground pad on the GND ball's side")
    # SW: straight across to the inductor's pad (level with the ball)
    track(board, "BOOST_SW", [sw, (l_sw[0] - 0.7, sw[1])], 0.2, B)
    # VIN: up and across to the inductor's battery pad
    track(board, "VBAT", [vin, (vin[0] + 0.6, vin[1] - 0.6), (l_in[0] - 0.8, l_in[1] + 0.2)], 0.2, B)
    # the hot loop: VOUT straight across to the first output capacitor, that capacitor's ground pad
    # straight back to the GND ball (0.2 mm off the ball, then 0.4 mm)
    track(board, "3V3", [vout, (out[0][0] + 0.5, vout[1])], 0.2, B)
    neck = (gnd[0] - 0.3, gnd[1] - 0.3)
    track(board, "GND", [gnd, neck], 0.2, B)
    track(board, "GND", [neck, (out_g[0][0] + 0.55, out_g[0][1] + 0.3)], 0.4, B)
    # VSEL: a thin strap to its own via
    track(board, "GND", [vsel, (vsel[0] - 0.35, vsel[1] + 0.85)], 0.2, B)
    via(board, "GND", vsel[0] - 0.35, vsel[1] + 0.9, 0.45, 0.2)
    # two output capacitors in a row: one bar across their 3V3 pads (carried on to the trunk's
    # vias), one across their ground pads, a via into the plane above each ground pad; the third
    # capacitor hangs under the bar
    # (the trunk's vias sit inside the LED ring's supply ring on the top, clear of it)
    ty = out[1][1]
    vx, vy = out[1][0] - 2.6, ty - 1.8
    tx = vx + 0.4
    track(board, "3V3", [out[0], out[1], (out[1][0] - 1.0, ty), (vx + 0.8, vy), (vx, vy)], 0.9, B)
    track(board, "3V3", [(c9[0], ty), c9], 0.6, B)
    track(board, "GND", [out_g[0], out_g[1]], 0.9, B)
    for (x, y), dx in zip(out_g, (0.6, 0.0)):     # (one outside the LEDs' supply ring, one inside; both
                                                  # off the encoder's frame pad on the top)
        track(board, "GND", [(x, y), (x + dx, y - 0.95)], 0.4, B)
        via(board, "GND", x + dx, y - 1.0, 0.5, 0.25)
    # --- the 3V3 trunk: two vias at the end of the bar, 1 mm wide on In2 under the cell to two vias
    # beside the module, then to its 10 uF and its supply pin
    for dx in (-0.4, 0.4):
        via(board, "3V3", tx + dx, vy)
    c12 = _pad(board, "C12", "1", "3V3")
    pin3 = _pad(board, "U6", "3", "3V3")
    ex = 9.3            # clear of the LED on the other side of the board at (7.0, -12.1)
    y1, y2 = -9.7 + UP, -10.6 + UP
    track(board, "3V3", [(tx - 0.4, vy), (tx + 0.4, vy)], 1.0, IN2)
    # (straight up under the module before turning: clear of the module's corner via)
    track(board, "3V3", [(tx, vy), (tx, y1 + (ex - tx)), (ex, y1), (ex, y2)], 1.0, IN2)
    for y in (y1, y2):
        via(board, "3V3", ex, y)
    track(board, "3V3", [(ex, y1), (ex, y2), (c12[0] + 0.15, c12[1] + 0.4)], 0.5, B)
    track(board, "3V3", [c12, pin3], 0.4, B)
    # the battery bus on the bottom: inductor - 10 uF - down to the 22 uF, two vias up to the trunk
    bus_x = c6[0]
    track(board, "VBAT", [(l_in[0] + 0.7, l_in[1]), c5], 0.6, B)
    vias = [(bus_x, c5[1] + 1.4), (bus_x, c5[1] + 2.2)]
    track(board, "VBAT", [c5, (bus_x, c5[1] + 0.8), vias[0], vias[1], c6], 0.6, B)
    for x, y in vias:
        via(board, "VBAT", x, y)
    # --- the cell's feed (top): + hole -> fuse -> trunk round the LED ring to the vias
    cell = _pad(board, "BT1", "1", "VCELL")
    f_in = _pad(board, "F1", "1", "VCELL")
    f_out = _pad(board, "F1", "2", "VBAT")
    track(board, "VCELL", [cell, f_in], 0.8, F)
    track(board, "VBAT", [f_out, (f_out[0], 8.0), vias[0], vias[1]], 0.8, F)
    # --- the LED ring (top): the supply as a ring just inside the LEDs, a spoke to each LED's supply
    # pad; the data from each LED's output (inner pad) across to the next one's input (outer pad)
    import math

    def any_pad(ref, num):
        for q in board.FindFootprintByReference(ref).Pads():
            if q.GetNumber() == num:
                pos = q.GetPosition()
                return (pcbnew.ToMM(pos.x) - layout.CX, pcbnew.ToMM(pos.y) - layout.CY), q.GetNetname()
        raise SystemExit(f"prewire: no pad {ref}.{num}")

    rv = layout.R_LED - 1.65     # between the encoder's parts and the vias at the boost converter
    n = layout.N_LEDS
    for i in range(1, n + 1):
        a0 = math.radians(layout.led_angle(i))
        (vx, vy), net = any_pad(f"D{i}", "4")
        if net != "VLED":
            raise SystemExit(f"prewire: D{i}.4 is on {net}, expected VLED")
        av = math.atan2(vy, vx)
        steps = 6
        arc = [(rv * math.cos(av + (2 * math.pi / n) * k / steps), rv * math.sin(av + (2 * math.pi / n) * k / steps))
               for k in range(steps + 1)]
        track(board, "VLED", [(vx, vy), arc[0]], 0.25, F)
        track(board, "VLED", arc, 0.25, F)
        if i < n:
            out, net = any_pad(f"D{i}", "1")
            din, net2 = any_pad(f"D{i + 1}", "3")
            if net != net2:
                raise SystemExit(f"prewire: D{i}.1 ({net}) does not feed D{i + 1}.3 ({net2})")
            track(board, net, [out, din], 0.2, F)
    # --- the charger's /CE leaves the module under the encoder's switch pad (top): a short escape
    # to a via just clear of that pad, where the router can pick it up on any layer
    ce = _pad(board, "U6", "21", "CHG_CE")
    track(board, "CHG_CE", [ce, (ce[0], ce[1] + 1.1)], 0.2, B)
    via(board, "CHG_CE", ce[0], ce[1] + 1.15, 0.45, 0.2)
    # --- the module's ground (bottom). Its centre pad: two vias in the gaps between its nine
    # pieces. Its ground pins and corner pads: joined by track and taken to vias under the module,
    # in the bare strip between the centre pad and the pin rows (nothing of the module's is there)
    u6 = board.FindFootprintByReference("U6").GetPosition()
    ux, uy = pcbnew.ToMM(u6.x) - layout.CX, pcbnew.ToMM(u6.y) - layout.CY
    # (the two on the cell's side only: the antenna-side gaps are under the LEDs' supply ring)
    for dx in (-0.9875, 0.9875):
        via(board, "GND", ux + dx, uy + 0.9875, 0.45, 0.2)
    pin = {n: _pad(board, "U6", str(n), "GND") for n in (1, 2, 11, 36, 39, 45, 48)}
    corners = sorted({(round(pcbnew.ToMM(q.GetPosition().x) - layout.CX, 3), round(pcbnew.ToMM(q.GetPosition().y) - layout.CY, 3))
                      for q in board.FindFootprintByReference("U6").Pads() if q.GetNumber() in ("50", "51", "52", "53")})
    top = min(c[1] for c in corners)            # the antenna end's corners, level with pins 36..48
    bot = max(c[1] for c in corners)
    row = sorted((pin[36], pin[48]))            # pins 36..48 are all ground: one track along the row
    track(board, "GND", [(-5.95, top), row[0], row[1], (5.95, top)], 0.25, B)
    track(board, "GND", [(5.95, top), pin[1], pin[2]], 0.25, B)
    for n in (39, 45):                          # the row down to two vias under the module
        track(board, "GND", [pin[n], (pin[n][0], pin[n][1] + 1.25)], 0.25, B)
        via(board, "GND", pin[n][0], pin[n][1] + 1.3, 0.45, 0.2)
    track(board, "GND", [pin[2], (pin[2][0] - 1.5, pin[2][1] + 0.5)], 0.25, B)
    via(board, "GND", pin[2][0] - 1.55, pin[2][1] + 0.5, 0.45, 0.2)
    # the cell-side corners: a via each just outside the module (not under the encoder's frame on
    # the other side of the board); pin 11 to its corner
    track(board, "GND", [pin[11], (5.95, bot), (7.0, bot)], 0.25, B)
    track(board, "GND", [(-5.95, bot), (-7.0, bot)], 0.25, B)
    for x in (-7.0, 7.0):
        via(board, "GND", x, bot, 0.45, 0.2)
