"""Placement for the Rev 0 board: 100 x 70 mm, 4 layers, plus a tab on the left edge.

The OBD-II plug sits on a small adapter board (obd-adapter-plug, build.py plug) that stands across
the left edge: the tab goes through its slot and 16 edge pads (9-16 on top, 1-8 underneath) are
soldered to it, so the plug sticks straight out of the board's end with the board in its middle.

Three kinds of placement, in this order:

1. FIXED (`fixed_for`): explicit positions for the parts whose position is the design: the
   connectors at the edges, the ESP32 antenna out of the right end, the MCU and its crystal, the
   input-protection chain right below the car connector, and the 5 V buck laid out as a unit
   (input caps at VIN, boot cap across BOOT / SW, inductor and catch diode at SW, output caps
   on the inductor, compensation and feedback at their pins).
2. NEAR (`near_rules`): every decoupling cap, clamp, ESD part and series resistor goes to the
   free spot nearest the pin(s) it serves, as soon as those pins are placed.
3. Everything else is packed into its block's region in netlist order.
"""

import math

import pcbnew

import os as _os
W, H = float(_os.environ.get("OBD_W", 100.0)), float(_os.environ.get("OBD_H", 70.0))   # OBD_W / OBD_H: try a size
# the parts below the car front ends, round the MCU and in the input chain were laid out on a
# 75 mm board centred on y = 37.5: they keep their places relative to the middle; the MCU and its
# periphery also moved DXM to the left
DY = H / 2 - 37.5
# one-sided (every part on top, JLCPCB Economic PCBA) unless OBD_TWO_SIDED=1: 84 x 58 two-sided
# was tried and is too dense to route on 4 layers (twice the wire per mm^2 of the 110 x 75 board
# that routed clean, see congestion.py)
TWO_SIDED = _os.environ.get("OBD_TWO_SIDED") == "1"
DXM = -8.0 if TWO_SIDED else 0.0
MX, MY = (0.0, 0.0) if TWO_SIDED else (3.0, 0.5)   # MY: U1 on y = H / 2 with the plug and the ESP32     # one-sided: the MCU, its periphery, the flash and the LDO 3 mm right (room for the tile columns)
# The small parts (0603 / 1206 R and C, SOD-123 and SOT-23 diodes and transistors, test points): on a
# matrix of their own (CELL x ROW), all the same way round; with TWO_SIDED on the bottom, under the
# parts they serve (JLCPCB Standard PCBA then: Economic is one side only).
SMALL = ("R_0603", "C_0603", "R_1206", "C_1206", "L_0603", "D_SOD-123", "TestPoint")
SMALL_EXACT = ("SOT-23",)
X0 = 0.0            # left edge (the tab sticks out of it to x = -mklib.TAB_DEPTH)
# the plug's edge pads (J1962_Edge_Joint): origin on the left edge at the middle of the pins, centred
# on the edge (y = H / 2) so the plug sits square on the board's end. The pins run down the edge in
# pairs (9 / 1 at H / 2 - 14 ... 16 / 8 at H / 2 + 14, 9-16 top, 1-8 bottom). The tab is as wide as
# mklib.TAB, centred on the pins.
PLUG = (0.0, H / 2, 0)
# VBAT_IN lane: pin 16's pad (y H / 2 + 14) right, then up beside the edge pads into F1's pad 1
# (prewire.py); nothing else is placed in it
VBAT_LANE = (3.4, H / 2 - 1.5, 6.0, H / 2 + 15.5)
BACK = set()        # refs placed on the bottom side (filled by fixed_for)
# fixed parts in the notch at a corner of another part's courtyard (filled by fixed_for): the bounding
# boxes place_all compares overlap there, the courtyards do not (DRC's courtyards_overlap is what counts)
NOTCHED = set()
CENTRED = set()     # fixed refs whose (x, y) is their courtyard's centre, not their origin (filled by fixed_for)
# Left to right the board follows the signal: car plug and front ends, the STM32 in the middle, the
# ESP32 at the far end with its antenna pointing out of it (centred, like the plug), the two USB-C
# ports mirrored on the long edges beside it: the STM32's on top, the ESP32's (with its CH340C) at
# the bottom, where the module's UART pins face.
# The antenna section (6.2 mm of the module) hangs off the right end, as Espressif's layout guide
# prefers: its edge starts at the board's end, so there is no copper keep-out on the board at all.
ESP = (W - 6.56, H / 2, 270)      # pins 1-14 face up, 25-38 face down; the antenna points out to +x
ESP_BODY = (W - 19.5, H / 2 - 9.3, W, H / 2 + 9.3)  # the module on the board (its courtyard also spans the keep-out)
USB_X = W - 6.0                   # both USB-C ports, mirrored about y = H / 2, in line with the module
# The five buttons in one row along the top edge, left to right: the STM32's RESET / BOOT / USER, then
# the ESP32's RST / BOOT; the three status LEDs in a row under the STM32's
# (turned 90 degrees: their pads point along y, so each is only 5.7 mm wide)
BTN_Y, BTN_PITCH, BTN_ROT = 5.6, 6.3, 90     # (1.3 mm off the edge: room for the labels above)
# the CONFIG DIP switch starts the row (7.4 x 9.5 mm turned 90: switches 1-4 read left to right),
# top-aligned with the buttons; then RESET, USER, ESP RST, ESP BOOT. The row ends 0.4 mm short of the
# top-right mounting hole's keep-out (HOLES)
DIP_AT = (48.5 + DXM + 3.71, BTN_Y - 3.8 + 4.75, 90)        # courtyard centre
BTN_X = [DIP_AT[0] + 3.71 + 0.6 + 2.85 + i * BTN_PITCH for i in range(4)]
# Four M3 mounting holes (3.2 mm, not plated), a rectangle of 81.5 x 63.0 mm: 3.5 mm in from the left end
# and the long edges, the right pair left of the USB-C ports (15 mm in from the right end, where the ports
# and the module take the corners). HOLE_KEEP round each centre is kept free of parts, tracks and vias on
# both sides (the footprint's courtyard: a pan head, a 5.5 mm hex standoff or a 7 mm washer); pours may
# run under it
HOLES = [(3.5, 3.5), (W - 15.0, 3.5), (3.5, H - 3.5), (W - 15.0, H - 3.5)]
HOLE_KEEP = 3.45
# The bottom edge, left to right: the bottom-left hole, the test points, the panic input's parts and J9
# (panic button, JST SH), J5 (STM32 expansion, DNP, under the J1850 receive column, pin 1 left, rows along
# x; its pin 9 is the panic net too), J10 microSD (card out of the edge), Qwiic, the bottom-right hole,
# the ESP32's USB-C. J8 (ESP32 IO, DNP) above the module, its signal pins over the module pins they wire to
EXP_AT = (40.9, H - 2.4, 90)
PANIC_AT = (35.88, H - 3.3, 0)      # JST SH, opening out, like the Qwiic socket
SD_AT = (65.9, H - 6.55, 180)       # courtyard to the edge, card slot (contact side) facing out
ESPIO_AT = (69.0, 21.35, 90)
# the Qwiic socket on the bottom edge, opening out, between the card and the bottom-right hole
QWIIC_AT = (USB_X - 17.0, H - 3.3, 0)
LED_X, LED_Y = 64.02 + DXM, 10.9    # the middle LED (the three sit under RESET / USER, over their resistors' tile)
TP_X = 8.5          # the test points' first column (bottom-left, right of the hole): two rows of five
TP_ROW = 3.3        # between the two rows
# USB-C front face (footprint y +3.65) 0.3 mm past the long edges, so a plug seats fully
USB_Y = H + 0.3 - 3.65
GAP = 0.25
# every placed (not fixed) part's courtyard centre sits on this grid, so they line up in rows and
# columns; two-pad parts only turn 0 / 90 degrees, so they all read the same way
GRID = 1.0
# bottom-side small parts sit in a matrix of CELL x ROW cells: an 0603 or a SOT-23 fills one cell,
# a SOD-123 two; so they are all equally spaced
CELL, ROW = 3.5, 2.5

# block -> region (x0, y0, x1, y1), board coordinates in mm, y down
REGIONS = {
    # (the car front ends' top-side parts are all fixed: these regions are for their small parts,
    # on the bottom, which may also go under the buck)
    "iso": (6, 1, 44, H - 1) if TWO_SIDED else (6, 1, 49, H - 1),
    "dlcsw": (6, 1, 44, H - 1) if TWO_SIDED else (6, 1, 49, H - 1),
    "j1850": (6, 1, 44, H - 1) if TWO_SIDED else (6, 1, 49, H - 1),
    "car": (0, 0, 0, 0),
    "power": (1, 1, 40, 24),
    "can": (40, H - 18, W - 12, H - 1),
    "mcu": (40, 12, W - 20, H - 18),
    "flash": (40, 12, W - 20, H - 18),
    "ui": (40, 10, W - 20, 20),
    # (top-side parts fit round the module; the small ones also go under it, on the bottom)
    "esp32": (W - 34, 9, W, H / 2),
    "usb": (W - 34, 9, W, H / 2),
    "exp": (W - 34, 9, W, H / 2),
    "espusb": (W - 34, H / 2, W, H),
    # (every part of these two is fixed or tiled; the regions are only a fallback)
    "sense": (40, 12, W - 20, H - 1),
    "sd": (W - 40, H / 2, W - 10, H),
}

# keep-outs for placement (both sides) and routing: the mounting holes (the ESP32 antenna's is off the
# board's end)
KEEPOUTS = [(x - HOLE_KEEP, y - HOLE_KEEP, x + HOLE_KEEP, y + HOLE_KEEP) for x, y in HOLES]
# copper beside the LDO's tab (U8 at (54.4, 48.2 + DY)): heat spreading + vias into the In2 3V3 plane
LDO_TAB = (56.4 + DXM + MX, 45.0 + DY + MY, 60.2 + DXM + MX, 51.4 + DY + MY)

# the buck converter, laid out around U7 at (BX, BY), in the top-left corner (above the input chain)
BX, BY = 15.0, 16.0

# reference labels placed by hand in KiCad (absolute x, y in mm, text angle): applied after placement
# (round the buck, so relative to BY)
LABELS = {
    "J6": (2.2, H / 2 - 18.0, 0),      # the plug: its own outline is off the board
    "U4": (86.0, 28.6, 0),              # on the module, clear of its pads (round it is all taken)
    "C19": (13.6, 45.0, 90),            # the input chain is tight: under C19, turned
    "R19": (BX - 4.6, BY + 4.7, 90),
    "C27": (BX + 12.0, BY + 1.0, 0),
    "C28": (BX + 8.5, BY + 2.5, 0), "R20": (BX + 6.0, BY + 4.7, 90), "R21": (BX + 1.5, BY + 4.7, 270),
}


def _refs(design):
    by_value = {}
    for p in design.parts:
        by_value.setdefault((p.block.name, p.value), []).append(p.ref)

    def ref(block, value, i=0):
        return by_value[(block, value)][i]
    return ref


def fixed_for(design):
    ref = _refs(design)
    X, Y = BX, BY
    # laid out round the middle of a 75 mm board: shifted by DY
    mid = {
        ref("mcu", "STM32G474RET6"): (65.0, 37.0, 0),
        # MCU at (65, 37): 100n at VDD pins 16 / 32 / 48 / 64, VDDA / VREF+ (29 / 28) caps, crystal at PF0/PF1 (5/6)
        ref("mcu", "100n", 0): (57.0, 41.0, 90),
        ref("mcu", "100n", 1): (68.5, 45.0, 0),
        ref("mcu", "100n", 2): (73.0, 33.0, 90),
        ref("mcu", "100n", 3): (61.5, 29.0, 0),
        ref("mcu", "100n", 4): (66.0, 46.8, 0),
        ref("mcu", "1u"): (66.0, 48.6, 0),
        ref("mcu", "16MHz"): (55.2, 35.5, 90),
        ref("mcu", "10p", 0): (51.4, 37.6, 180),     # HSE_IN (Y1 pin 1) / HSE_OUT (pin 3): signal pad toward Y1
        ref("mcu", "10p", 1): (51.4, 33.4, 180),
        ref("mcu", "100n", 5): (54.0, 39.5, 0),      # NRST (pin 7)
        # VBAT sense 100n (PA4, pin 18): by hand, clear of the fan-out lanes of the MCU's west and south pins
        ref("power", "100n", 4): (59.0, 43.5, 180),
        # QSPI flash under the MCU, next to its QSPI pins (PA6 / PA7 / PB0 / PB1 / PB10 on the bottom edge)
        ref("flash", "W25Q128JVSIQ"): (59.5, 55.4, 0),
        # input chain right of the plug: J6.16 -> PTC -> TVS -> ideal diode (LM74700 + FET)
        ref("power", "2A 60V"): (8.0, 34.6, 0),        # pad 1 VBAT_IN left (the lane from pin 16), pad 2 VBAT_F right
        ref("power", "5.0SMDJ26CA"): (9.4, 42.6, 270),    # pad 1 (VBAT_F) up at F1
        ref("power", "AO4264E"): (16.6, 36.0, 0),      # source pins 1-3 left, drain 5-8 right
        ref("power", "LM74700QDBVRQ1"): (16.5, 41.3, 180),   # GATE / CATHODE left (to the FET), VCAP right
        ref("power", "100n", 1): (19.5, 41.3, 90),             # VCAP cap: pad 2 (VBAT_F) up at ANODE
        ref("power", "100n", 0): (13.6, 44.6, 270),            # LM74700 input cap at EN / VBAT_F
        ref("power", "47u 50V"): (19.2, 47.8, 0),
        # 3.3 V LDO (SOT-223) below the MCU
        ref("power", "AP2114H-3.3"): (54.4, 48.2, 0),
    }
    chain = {ref("power", v) for v in ("2A 60V", "5.0SMDJ26CA", "AO4264E", "LM74700QDBVRQ1", "47u 50V")} | \
        {ref("power", "100n", 0), ref("power", "100n", 1)}
    f = {r: (x + (0 if r in chain else DXM + MX), y + DY + (0 if r in chain else MY), rot) for r, (x, y, rot) in mid.items()}
    NOTCHED.add(ref("power", "100n", 4))    # in the notch at the LQFP's south-west corner, 0.2 mm off the pin 16 cap's box
    if TWO_SIDED:
        BACK.add(ref("mcu", "100n", 2))     # VDD pin 48's cap right under the pin (the ESP32 is beside it)
    btn = [ref("ui", "RESET"), ref("ui", "USER"), ref("espusb", "ESP_RST"), ref("espusb", "ESP_BOOT")]
    f.update({r: (x, BTN_Y, BTN_ROT) for r, x in zip(btn, BTN_X, strict=True)})
    f.update({ref("ui", c): (LED_X + k * 3.5, LED_Y, 0) for c, k in zip(("red", "green", "blue"), (-1, 0, 1), strict=True)})
    f.update({
        ref("car", "OBD-II plug (J1962 male)"): PLUG,
        ref("exp", "EXP"): EXP_AT,
        ref("esp32", "ESP_IO"): ESPIO_AT,
        ref("exp", "Qwiic"): QWIIC_AT,
        ref("sense", "PANIC"): PANIC_AT,
        ref("sd", "microSD"): SD_AT,
    })
    # the rest of the top side in rows and columns, like the buttons (courtyard centres):
    # car front ends in two columns: the J1850 ICs, the K / L-line 2 W pull-ups, the CAN
    # transceivers, their termination jumpers
    # (rows with equal 2.7 mm gaps between their courtyards: 9.2, 4.0 and 5.5 mm tall)
    c1, c2 = 28.2, 36.7
    r1, r2, r3 = 30.0, 39.3, 47.25
    grid = {
        ref("j1850", "LM317LIDR"): (c1, r1, 0),
        ref("j1850", "LM2901BIDR"): (c2, r1, 0),
        ref("iso", "510 2W", 0): (c1, r2, 0),
        ref("iso", "510 2W", 1): (c2, r2, 0),
        ref("can", "TCAN1044VDRQ1", 0): (c1, r3, 0),
        ref("can", "TCAN1044VDRQ1", 1): (c2, r3, 0),
        # middle: the VBUS OR diode between the CAN column and the flash, on the CAN row (the flash
        # too); SWD under the LEDs
        ref("ui", "SWD"): (47.875, 17.0, 90),     # centred over the J1850 receive column
        # ESP32 end: the strips above and below the module mirror each other (each USB-C's ESD array
        # beside it; the load switch above, the ESP USB's OR diode below)
        ref("usb", "USBLC6-2SC6"): (W - 8.0, 11.0, 0),
        ref("espusb", "USBLC6-2SC6"): (W - 8.0, H - 12.0, 0),
        ref("esp32", "TPS22918"): (W - 3.0, 11.0, 0),
        ref("espusb", "SS34"): (W - 5.5, H - 17.0, 0),
    }
    # test points: two rows of five at the bottom edge on the left, under the car-side circuits, on the
    # small-part pitch: TP1 ... TP5 (the supplies and GND) over TP6 ... TP10 (the bus lines by protocol).
    # The rows sit where every label fits above its pad: the top row's under the labels of D21 / D23
    # (which are below their parts), the bottom row's between the rows
    tps = sorted((p.ref for p in design.parts if p.footprint.startswith("TestPoint:")), key=lambda r: int(r[2:]))
    per_row = (len(tps) + 1) // 2
    grid.update({r: (TP_X + (i % per_row) * CELL, H - 1.65 - (TP_ROW if i < per_row else 0), 0) for i, r in enumerate(tps)})
    grid[ref("ui", "CONFIG")] = DIP_AT
    f.update(grid)
    CENTRED.update(grid)
    f.update({
        # ESP32 at the right end, antenna out; its supply parts and CH340C go by NEAR rules
        ref("esp32", "ESP32-WROOM-32E-N8"): ESP,
        # USB-C ports mirrored on the long edges (the same edge offset, the top one turned round)
        ref("usb", "USB-C"): (USB_X, H - USB_Y, 180),
        ref("espusb", "USB-C ESP32"): (USB_X, USB_Y, 0),
        # buck: U7 (TPS54360B) at (X, Y); VIN / BOOT left, SW / GND / COMP / FB right
        ref("power", "TPS54360BDDAR"): (X, Y, 0),
        # hot loop (VIN caps -> VIN -> SW -> catch diode -> GND -> caps) kept tight round the IC's
        # top: the diode lies above U7, cathode over pin 8 (SW), anode beside the input caps' GND
        ref("power", "2.2u", 0): (X - 7.25, Y - 6.43, 90),    # GND pad up at the anode, VIN pad down
        ref("power", "2.2u", 1): (X - 9.65, Y - 6.43, 90),
        ref("power", "100n", 3): (X, Y - 3.65, 0),            # boot cap across pins 1 / 8
        ref("power", "8.2u"): (X + 8.2, Y - 4.6, 90),         # SW pad down at pin 8
        ref("power", "B560C"): (X - 1.1, Y - 7.9, 180),       # cathode right, over pin 8
        ref("power", "47u", 0): (X + 5.5, Y - 11.45, 90),
        ref("power", "47u", 1): (X + 8.0, Y - 11.45, 90),
        ref("power", "47u", 2): (X + 10.5, Y - 11.45, 90),
        ref("power", "SS34", 0): (X + 15.35, Y - 11.9, 180),  # 5V_BUCK -> 5V, anode at the caps
        ref("power", "13k"): (X + 5.6, Y + 0.63, 0),          # COMP: R19 + C27 along pin 6's row,
        ref("power", "6.8n"): (X + 8.8, Y + 0.63, 0),         # 39p just below
        ref("power", "39p"): (X + 5.6, Y + 2.3, 0),
        ref("power", "10.2k"): (X + 2.9, Y + 4.7, 270),       # FB divider under pin 5
        ref("power", "57.6k"): (X + 4.6, Y + 4.7, 90),
        ref("power", "162k"): (X - 2.9, Y + 4.7, 270),        # RT under pin 4
    })
    return f


def near_rules(design):
    """[(ref, [(target ref, pad number), ...], max distance mm)]: put ref as close as possible
    to the mean of the target pads."""
    ref = _refs(design)
    parts = {p.ref: p for p in design.parts}
    mcu = ref("mcu", "STM32G474RET6")
    car = ref("car", "OBD-II plug (J1962 male)")
    rules = []

    def on(r, *targets, dist=10.0):
        rules.append((r, list(targets), dist))

    def by_net(block, value, net):
        return [p.ref for p in design.parts if p.block.name == block and p.value == value
                and net in p.pin_net().values()]

    def ic(block, value, i=0):
        return ref(block, value, i)

    # MCU periphery
    on(ref("mcu", "4.7u"), (mcu, "32"))
    on(ref("mcu", "10k"), (mcu, "61"))                      # BOOT0 pull-down
    on(ref("ui", "SWD"), (mcu, "46"), (mcu, "49"), dist=20)  # beside SWDIO / SWCLK
    for net, pin in (("ESP_CTS_M", "13"), ("MCU_TX_ESP_M", "14"), ("ESP_IO0_M", "58"), ("ESP_EN_M", "60")):
        on(by_net("esp32", "1k", net)[0], (mcu, pin))
    on(ref("flash", "10k"), (ref("flash", "W25Q128JVSIQ"), "1"))
    on(ref("flash", "100n"), (ref("flash", "W25Q128JVSIQ"), "8"))
    # input chain
    fet = ic("power", "AO4264E")
    tvs = ic("power", "5.0SMDJ26CA")
    on(ref("power", "10u", 0), (fet, "5"))                  # VBAT_P 10u
    on(ref("power", "100n", 2), (fet, "7"))                 # VBAT_P 100n
    on(ref("power", "100k 0.1%"), (tvs, "1"), dist=16)               # VBAT sense: 100k at the source,
    on(ref("power", "10k 0.1%"), (mcu, "18"))               # 10k at PA4 (its 100n has a fixed spot, above)
    # 5 V OR + LDO
    ldo = ic("power", "AP2114H-3.3")
    on(ref("power", "10u", 2), (ldo, "3"))                  # LDO input
    on(ref("power", "10u", 3), (ldo, "2"))                  # LDO output
    on(ref("power", "SS34", 1), (ldo, "3"), dist=14)        # VBUS -> 5V
    on(ref("power", "10u", 1), (ref("power", "SS34", 0), "1"))
    # car-connector protection: clamp right at the connector pin
    # (J1962 pin numbers; the edge pads are at x 0-3, so "close" is just right of them)
    on(ic("j1850", "SMF24A", 0), (car, "2"), dist=20)
    on(ic("j1850", "SMF24A", 1), (car, "10"), dist=20)
    on(ic("iso", "SMF24A", 0), (car, "7"), dist=20)
    on(ic("iso", "SMF24A", 1), (car, "15"), dist=20)
    on(ic("can", "SZNUP2105L", 0), (car, "6"), (car, "14"), dist=20)
    on(ic("can", "SZNUP2105L", 1), (car, "3"), (car, "11"), dist=20)
    # CAN transceivers: VCC / VIO caps and the STB pull-up at their pins
    for i in (0, 1):
        u = ic("can", "TCAN1044VDRQ1", i)
        on(by_net("can", "100n", "5V_BUCK")[i], (u, "3"))
        on(by_net("can", "100n", "3V3")[i], (u, "5"))
        on(ref("can", "4.7k", i), (u, "8"))
    # DLC_SW, J1850, K-line front ends
    on(ref("dlcsw", "1u"), (ic("dlcsw", "SI2309CDS"), "3"))
    on(ref("dlcsw", "BZT52C12"), (ic("dlcsw", "SI2309CDS"), "1"))
    reg = ic("j1850", "LM317LIDR")
    cmp_ = ic("j1850", "LM2901BIDR")
    on(by_net("j1850", "100n", "DLC_SW")[0], (reg, "1"))
    on(ref("j1850", "1u"), (reg, "2"))
    on(by_net("j1850", "100n", "CMP_VCC")[0], (cmp_, "3"))
    on(ref("j1850", "BZT52C33"), (cmp_, "3"))
    on(by_net("j1850", "470", "CMP_VCC")[0], (cmp_, "3"))
    # ESP32 USB port: ESD and CC at the connector, CH340C periphery at its pins
    j7 = ref("espusb", "USB-C ESP32")
    ch = ic("espusb", "CH340C")
    on(ic("espusb", "USBLC6-2SC6"), (j7, "A6"), (j7, "A7"), dist=8)
    on(ref("espusb", "5.1k", 0), (j7, "A5"), dist=8)
    on(ref("espusb", "5.1k", 1), (j7, "B5"), dist=8)
    on(ref("espusb", "10u"), (j7, "A4"), dist=8)
    on(ref("espusb", "SS34"), (j7, "A4"), dist=10)
    on(ref("espusb", "100n"), (ch, "16"), dist=10)
    on(ref("espusb", "1k"), (ch, "2"), dist=10)
    for i, pin in ((0, "13"), (1, "14")):
        on(ref("espusb", "10k", i), (ch, pin), dist=14)
        on(ref("espusb", "MMBT3904", i), (ch, pin), dist=14)
    # ESP32 (antenna right): supply on the top row (3V3 pin 2, EN pin 3), CH340C and the
    # programming header at the UART0 pins on the bottom row (34 / 35)
    esp = ic("esp32", "ESP32-WROOM-32E-N8")
    tps = ic("esp32", "TPS22918")
    on(ref("esp32", "100n"), (esp, "2"), dist=8)
    on(ref("esp32", "22u"), (esp, "2"), dist=8)
    on(ref("esp32", "1u", 0), (esp, "3"), dist=8)          # EN RC
    on(ref("esp32", "10k", 0), (esp, "3"), dist=8)
    on(tps, (esp, "2"), dist=14)
    on(ref("esp32", "1u", 1), (tps, "1"), dist=6)           # VIN
    on(ref("esp32", "1n"), (tps, "4"), dist=10)             # CT
    on(ref("esp32", "100k"), (tps, "3"), dist=10)           # ON pull-down
    on(ref("espusb", "1N4148W", 0), (tps, "3"), dist=18)    # PB6 -> ON
    on(ref("espusb", "1N4148W", 1), (tps, "3"), dist=18)    # VBUS2 -> ON
    on(ch, (esp, "35"), dist=18)
    # audit round 5: CONFIG switch 1's series resistor at the switch (last, so it takes what is free and
    # moves nothing else). The VBUS2 bleed (R69) is in the USB2 tile, beside J7's CC resistors
    on(by_net("ui", "1k", "ESP_ON_SW")[0], (ref("ui", "CONFIG"), "1"), dist=12)
    assert all(r in parts for r, _, _ in rules)
    return rules


# Tiles: each circuit as a block of rows, its IC first, then its parts in signal order, every row
# left-aligned. A part's slot is its courtyard width rounded up to TILE_PITCH (0603: 2, SOT-23 /
# SOD-123 / 1206: 3, SOIC-8 / 2512: 5), so tiles whose left edges sit on the same pitch share columns;
# rows are TILE_VGAP apart (room for the reference above each part). The first row and any row of one
# part are centred on the tile. "REF@90" turns a part. An optional fourth item "below" puts the
# tile's labels under its parts (the LEDs' resistors; the tiles hugging the module's underside); a
# fifth, the row gap for that tile.
TILE_PITCH, TILE_VGAP = 1.75, 0.95
TILES = [
    # name, (left, top), rows
    # car side. Clamps at the plug: the bus TVSs in a row above the input chain (J1850 pins 2 / 10,
    # both CAN pairs), the K / L-line TVSs under D4 (pins 7 / 15)
    ("bus clamps", (4.0, 23.5), [["D19", "D20", "D9", "D8"]]),
    ("K/L clamps", (6.25, 45.3), [["D22@90", "D24@90"]]),
    ("VBAT_P caps", (20.75, 28.66), [["C23"], ["C21@90"]]),
    ("DLC_SW", (34.75, 2.55), [["Q3", "Q2"], ["R31", "R29"], ["D10"], ["R30"], ["C40"]]),
    # three columns of front ends, tops level: bus side (CAN2, CAN1, J1850 PWM-), J1850 drive (its
    # supply, PWM+), the LM2901 receiver with all four input channels round it (each row a channel's
    # clamp + series resistor, then the reference dividers and the output pull-ups). The CAN
    # transceivers turned so their bus pins face the plug.
    ("CAN2", (24.5, 23.5), [["U10@180"], ["C39", "C38"], ["R27", "R28"]]),
    ("CAN1", (24.5, 36.44), [["U9@180"], ["C37", "C36"], ["R25", "R26"]]),
    ("J1850 PWM-", (24.5, 49.82), [["Q7", "R39"], ["R40", "R41"], ["D13", "R42"]]),
    ("J1850 supply", (34.0, 23.5), [["U11"], ["C41", "C42"], ["R32", "R33"], ["R34", "R35"], ["Q4"]]),
    ("J1850 PWM+", (34.0, 43.4), [["Q5", "R36"], ["Q6", "R37"], ["D11", "R38"], ["D12"]]),
    ("J1850 receive", (43.5, 23.5), [["U12"], ["R43", "C43"], ["D14", "R47"], ["D15", "R44"], ["D16", "R48"],
                                     ["D17", "R49"], ["D18", "R57"], ["R45", "R46"], ["R58", "R59"],
                                     ["R50"]]),
    ("K-line", (6.25, 50.05), [["R51"], ["Q8", "R52"], ["D21", "R53"]]),
    ("L-line", (15.375, 50.05), [["R54"], ["Q9", "R55"], ["D23", "R56"]]),
    # middle: the LED resistors under their LEDs, the MCU's IO0 / EN link resistors and BOOT0
    # pull-down above it, the LDO's caps and the VBUS OR diode under the LDO, the flash's beside it,
    # a column between the MCU and the ESP32 (I2C pull-ups, 4.7u, the UART link
    # resistors on their way to the module's UART pins)
    ("LED resistors", (58.77, 12.75), [["R3", "R4", "R5"]], "below"),
    ("MCU top", (59.75, 19.6), [["R13", "R14"], ["R1"]]),
    ("LDO", (53.0, 50.1), [["C33"], ["C34"], ["D7@90"]]),
    # flash cap / pull-up, then the microSD's pull-ups and supply caps, between U3 and the card
    ("flash + microSD", (67.25, 50.6), [["C13", "R8", "R68"], ["RN1@90", "C50", "C51"]]),
    # left of the MCU's top-left corner (PC14 / PC15 interrupts, I2C): secure element over the accelerometer
    ("sense", (52.25, 12.4), [["U16@90"], ["C48", "C46"], ["U15", "C47"]]),
    # panic input: pull-up and cap over the clamp and the series resistor (that one nearest J9, right of it)
    ("panic", (24.5, 63.4), [["R67", "C49"], ["D28", "R66"]]),
    ("MCU right", (76.0, 33.0), [["R16"], ["R17"], ["R18"], ["C5"], ["R11"], ["R12"]]),
    # ESP32 end. Above the module: the ESP_ON diodes, the EN pull-up and a 2 x 3 block (U2's CC
    # resistors and VBUS cap under U2; U5's ON / CT / VIN parts under U5), the bulk cap under it, the
    # module's 100n and EN cap right at pins 2 / 3 (x 83-92 under J8 is left for J8's traces).
    # Below it, mirrored: the CH340C (turned so VCC pin 16 meets its cap) and the auto-reset / USB
    # block, its ESD array nearest J7 as U2 is nearest J2.
    ("ESP_ON", (78.25, 13.5), [["D26@90", "D27@90"]]),
    ("ESP32 EN", (85.25, 14.2), [["R9"]]),
    ("ESP32 strip", (88.75, 14.2), [["R6", "C12", "R15"], ["R7", "C18", "C17"]]),
    ("ESP bulk", (93.5, 18.5), [["C14@90"]]),
    ("ESP pins", (92.5, 23.45), [["C16", "C15"]]),
    ("CH340C", (80.25, 45.0), [["U14@180"], ["C45", "R63"], ["D25"]], "below"),
    ("USB2", (89.0, 45.0), [["Q10", "Q11"], ["R64", "R65"], ["R61", "R62", "R69"], ["U13", "C44"]], "below", 1.4),
]


def _tile_pitch(w):
    return math.ceil((w + 0.4) / TILE_PITCH - 1e-6) * TILE_PITCH


def place_tiles(fps, occupied, placed):
    """Place TILES (see above); the parts become fixed. Prints any clash."""
    from pcbgen import place
    for name, (x0, y0), rows, *opt in TILES:
        vgap = opt[1] if len(opt) > 1 else TILE_VGAP
        shapes = []
        for row in rows:
            items = []
            for spec in row:
                r, _, rot = spec.partition("@")
                fp = fps[r]
                place(fp, 0, 0, int(rot or 0))
                items.append((r, fp, int(rot or 0), courtyard(fp)))
            shapes.append(items)
        width = max(sum(_tile_pitch(c[2] - c[0]) for *_, c in items) for items in shapes)
        y = y0
        for items in shapes:
            h = max(c[3] - c[1] for *_, c in items)
            centred = items is shapes[0] or len(items) == 1
            x = x0 if not centred else x0 + (width - sum(_tile_pitch(c[2] - c[0]) for *_, c in items)) / 2
            for r, fp, rot, c in items:
                p = _tile_pitch(c[2] - c[0])
                cx, cy = x + p / 2, y + h / 2
                place(fp, cx - (c[0] + c[2]) / 2, cy - (c[1] + c[3]) / 2, rot)
                rect = courtyard(fp)
                hits = [o for o in occupied if _overlap(rect, o)]
                if hits or not _inside(rect):
                    print(f"layout: tile {name}: {r} {tuple(round(v, 2) for v in rect)} clashes with",
                          [tuple(round(v, 2) for v in o) for o in hits] or "the board edge")
                occupied.append(rect)
                placed.add(r)
                x += p
            y += h + vgap


def small(fp):
    """A part that goes on the bottom side (see SMALL)."""
    n = str(fp.GetFPID().GetLibItemName())
    return n.startswith(SMALL) or n in SMALL_EXACT


def tht(fp):
    """Has through-hole pins (not counting thermal vias in an exposed pad, like the ESP32's)."""
    return any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and pcbnew.ToMM(p.GetDrillSize().x) >= 0.6
               for p in fp.Pads())


def courtyard(fp):
    cy = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
    if cy.OutlineCount():
        bb = cy.BBox()
    else:
        bb = fp.GetBoundingBox(False)
    return (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def _overlap(a, b):
    return not (a[2] + GAP <= b[0] or b[2] + GAP <= a[0] or a[3] + GAP <= b[1] or b[3] + GAP <= a[1])


def _inside(r):
    return r[0] >= 0.5 and r[1] >= 0.5 and r[2] <= W - 0.5 and r[3] <= H - 0.5


def _find(region, w, h, occupied, row=GRID, col=GRID):
    """First free rect of w x h in region, row by row, its centre on a lattice `col` across and
    `row` down."""
    cy = math.ceil((region[1] + h / 2) / row) * row
    while cy + h / 2 <= region[3]:
        cx = math.ceil((region[0] + w / 2) / col) * col
        while cx + w / 2 <= region[2]:
            rect = (cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
            if not any(_overlap(rect, o) for o in occupied):
                return rect
            cx += col
        cy += row
    return None


def _lattice(fp):
    """(row, col) pitch of the lattice fp's centre goes on."""
    return (ROW, CELL) if small(fp) else (GRID, GRID)


def _rots(fp):
    if small(fp):           # the small-part matrix: every part the same way round
        return (0,)
    return (0, 90) if len({p.GetNumber() for p in fp.Pads()}) <= 2 else (0, 90, 180, 270)


def _pad_pos(fp, num):
    pts = [p.GetPosition() for p in fp.Pads() if p.GetNumber() == num]
    if not pts:
        raise KeyError(f"{fp.GetReference()} has no pad {num}")
    return pcbnew.ToMM(pts[0].x), pcbnew.ToMM(pts[0].y)


_RINGS = None


def _rings(dist, row=GRID, col=GRID):
    """Lattice offsets (`col` across, `row` down) within dist, nearest first."""
    global _RINGS
    if _RINGS is None or _RINGS[0] != (dist, row, col):
        n, m = int(dist / col), int(dist / row)
        pts = [(i * col, j * row) for i in range(-n, n + 1) for j in range(-m, m + 1)
               if math.hypot(i * col, j * row) <= dist]
        pts.sort(key=lambda p: math.hypot(*p))
        _RINGS = ((dist, row, col), pts)
    return _RINGS[1]


def _place_near(fp, target, dist, occupied, back=False):
    """The free spot nearest `target` for fp (rotations 0 / 90 / 180 / 270), by fp's centre."""
    from pcbgen import place
    row, col = _lattice(fp)
    tx, ty = round(target[0] / col) * col, round(target[1] / row) * row
    shapes = []
    place(fp, 0, 0, 0, back=back)
    for rot in _rots(fp):
        place(fp, 0, 0, rot, back=back)
        c = courtyard(fp)
        shapes.append((rot, c))
    for dx, dy in _rings(dist, row, col):
        cx, cy = tx + dx, ty + dy
        for rot, (x0, y0, x1, y1) in shapes:
            ox, oy = (x0 + x1) / 2, (y0 + y1) / 2  # courtyard centre relative to the origin
            rect = (cx - ox + x0, cy - oy + y0, cx - ox + x1, cy - oy + y1)
            if _inside(rect) and not any(_overlap(rect, o) for o in occupied):
                place(fp, cx - ox, cy - oy, rot, back=back)
                return rect
    return None


def place_all(board, design, fps):
    from pcbgen import place
    tiled = {spec.partition("@")[0] for _, _, rows, *_ in TILES for row in rows for spec in row}
    fixed = {r: v for r, v in fixed_for(design).items() if r not in tiled}
    rules = near_rules(design)
    # one occupancy list per side; through-hole parts (and the plug's two-sided edge pads) take both
    # (the buck's hand-laid copper includes a B.Cu feedback-sense track and GND vias round it: the
    # whole buck area is off limits on the bottom too)
    top, bot = list(KEEPOUTS) + [LDO_TAB, VBAT_LANE], list(KEEPOUTS) + [(BX - 11, BY - 13, BX + 20, BY + 7)]
    placed = set()
    power = {p.ref for p in design.parts if p.block.name == "power"}
    for r, (x, y, rot) in fixed.items():
        place(fps[r], x, y, rot, back=r in BACK)
        if r in CENTRED:    # move it so its courtyard's centre lands on (x, y)
            c = courtyard(fps[r])
            place(fps[r], 2 * x - (c[0] + c[2]) / 2, 2 * y - (c[1] + c[3]) / 2, rot, back=r in BACK)
        # the ESP32's courtyard is the module plus its antenna keep-out (in KEEPOUTS): only the body
        c = ESP_BODY if (x, y, rot) == ESP else courtyard(fps[r])
        # the power stage's hand-laid copper and vias (prewire.py) run on both sides round its parts
        both = tht(fps[r]) or (x, y, rot) == PLUG or r in power
        if both or r not in BACK:
            top.append(c)
        if both or r in BACK:
            bot.append(c)
        # copper a top-side part has on the bottom (a thermal pad's underside, its via array)
        if not both and r not in BACK:
            bp = [p.GetBoundingBox() for p in fps[r].Pads() if p.IsOnLayer(pcbnew.B_Cu)]
            if bp:
                T = pcbnew.ToMM
                bot.append((min(T(q.GetLeft()) for q in bp) - 0.3, min(T(q.GetTop()) for q in bp) - 0.3,
                            max(T(q.GetRight()) for q in bp) + 0.3, max(T(q.GetBottom()) for q in bp) + 0.3))
        placed.add(r)
    place_tiles(fps, top, placed)
    near_refs = {r for r, _, _ in rules}
    # fixed parts are not checked against each other when placed: say so if two collide (the power
    # stage's own hand layout is tight on purpose, and the ESP32 counts by its body)
    fx = [(r, ESP_BODY if fixed[r] == ESP else courtyard(fps[r]), r in BACK or tht(fps[r]),
           r not in BACK or tht(fps[r])) for r in fixed if fixed[r] != PLUG]
    for i, (ra, ca, ba, ta) in enumerate(fx):
        for rb, cb, bb, tb in fx[i + 1:]:
            if ((ba and bb) or (ta and tb)) and _overlap(ca, cb) and not {ra, rb} <= power and not {ra, rb} & NOTCHED:
                print(f"layout: fixed parts {ra} and {rb} overlap")
    # ICs that go underneath too: the CH340C under the ESP32 module, at its UART0 pins
    under = {p.ref for p in design.parts if p.value == "CH340C"}

    def side(fp):
        return (bot, True) if TWO_SIDED and (small(fp) or fp.GetReference() in under) else (top, False)

    # Sticky placement: a part keeps its place in the frozen routing (gen/routing.json) when that
    # spot is still free, so a small change moves only the parts it has to (and `route` with
    # OBD_REROUTE=patch reroutes only round them). OBD_STICKY=0 places everything afresh.
    import json
    frozen = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "routing.json")
    if _os.path.exists(frozen) and _os.environ.get("OBD_STICKY", "1") != "0":
        with open(frozen) as fh:
            old = json.load(fh)["placement"]
        kept = 0
        for r, (x, y, rot) in old.items():
            if r in placed or r not in fps:
                continue
            fp = fps[r]
            occ, back = side(fp)
            place(fp, x, y, rot, back=back)
            c = courtyard(fp)
            if _inside(c) and not any(_overlap(c, o) for o in occ):
                occ.append(c)
                if tht(fp):
                    (top if back else bot).append(c)
                placed.add(r)
                kept += 1
        print(f"layout: {kept} parts kept their frozen places")

    def run_rules():
        progress = True
        while progress:
            progress = False
            for r, targets, dist in rules:
                if r in placed or not all(t in placed for t, _ in targets):
                    continue
                pts = [_pad_pos(fps[t], n) for t, n in targets]
                tgt = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
                occ, back = side(fps[r])
                # (the small-part matrix has fewer spots than free placement: try twice as far before
                # giving up)
                spot = _place_near(fps[r], tgt, dist, occ, back) or _place_near(fps[r], tgt, 2 * dist, occ, back)
                if spot is None:
                    print(f"layout: {r} found no spot within {dist} mm of {targets}")
                    continue
                occ.append(spot)
                if tht(fps[r]):
                    (top if back else bot).append(spot)
                placed.add(r)
                progress = True

    run_rules()
    for b in design.blocks:
        region = REGIONS[b.name]
        for p in b.parts:
            if p.ref in placed or p.ref in near_refs or p.ref not in fps:
                continue
            fp = fps[p.ref]
            occ, back = side(fp)
            spot = None
            place(fp, 0, 0, 0, back=back)
            for rot in _rots(fp)[:2]:
                place(fp, 0, 0, rot, back=back)
                cx0, cy0, cx1, cy1 = courtyard(fp)
                w, h = cx1 - cx0, cy1 - cy0
                spot = _find(region, w, h, occ, *_lattice(fp))
                if spot:
                    break
            if spot is None:
                place(fp, 0, 0, 0, back=back)
                cx0, cy0, cx1, cy1 = courtyard(fp)
                w, h = cx1 - cx0, cy1 - cy0
                spot = _find((1, 1, W - 1, H - 1), w, h, occ, *_lattice(fp))
                print(f"layout: {p.ref} ({p.value}) overflowed region {b.name}")
            if spot is None:
                raise RuntimeError(f"{p.ref} ({p.value}) does not fit anywhere")
            place(fp, spot[0] - cx0, spot[1] - cy0, fp.GetOrientationDegrees(), back=back)
            occ.append(spot)
            if tht(fp):
                (top if back else bot).append(spot)
            placed.add(p.ref)
            run_rules()
    run_rules()
    missing = [r for r in near_refs if r not in placed]
    for r in missing:  # a NEAR part whose spot was taken: pack it anywhere
        fp = fps[r]
        occ, back = side(fp)
        place(fp, 0, 0, 0, back=back)
        cx0, cy0, cx1, cy1 = courtyard(fp)
        spot = _find((1, 1, W - 1, H - 1), cx1 - cx0, cy1 - cy0, occ, *_lattice(fp))
        place(fp, spot[0] - cx0, spot[1] - cy0, 0, back=back)
        occ.append(spot)
        print(f"layout: {r} packed anywhere (no NEAR spot)")


# fixed parts whose label goes under them (the LEDs: the buttons' labels are above the buttons)
LABEL_BELOW = {"D1", "D2", "D3"}
LABEL_LEFT = {"U2"}      # (J2 above, U5 right, the strip below)


def place_labels(fps, skip=()):
    """Every reference upright, just outside its part: above, else below, left or right; first a spot
    clear of pads, other labels and other parts' courtyards, then one clear of pads and labels only.
    Returns the refs that found no spot (their label is hidden)."""
    MMf = pcbnew.FromMM
    T = pcbnew.ToMM
    pads = []
    for fp in fps.values():
        for p in fp.Pads():
            b = p.GetBoundingBox()
            pads.append((fp.GetReference(), (T(b.GetLeft()), T(b.GetTop()), T(b.GetRight()), T(b.GetBottom()))))
    yards = {r: courtyard(fp) for r, fp in fps.items()}
    below_refs = {spec.partition("@")[0] for t in TILES if t[3:4] == ("below",) for row in t[2] for spec in row}
    below_refs |= LABEL_BELOW
    # every part's silkscreen drawing (outlines, pin-1 marks): labels keep off it too
    silk = []
    for fp in fps.values():
        for g in fp.GraphicalItems():
            if g.GetLayer() == pcbnew.F_SilkS and not isinstance(g, pcbnew.PCB_TEXT):
                b = g.GetBoundingBox()
                silk.append((fp.GetReference(), (T(b.GetLeft()), T(b.GetTop()), T(b.GetRight()), T(b.GetBottom()))))
    labels = []
    failed = []

    def box(t):
        b = t.GetBoundingBox()
        return (T(b.GetLeft()), T(b.GetTop()), T(b.GetRight()), T(b.GetBottom()))

    def hit(a, b, g=0.1):
        return not (a[2] + g <= b[0] or b[2] + g <= a[0] or a[3] + g <= b[1] or b[3] + g <= a[1])

    # big parts first, then the rest top to bottom, left to right (a label above its part never takes
    # the spot below the part over it)
    def big(r):
        c = yards[r]
        return (c[2] - c[0]) * (c[3] - c[1]) > 40
    order = sorted(fps, key=lambda r: (not big(r), round(yards[r][1], 1), yards[r][0]))
    for r in order:
        if r in skip:
            continue
        t = fps[r].Reference()
        if not t.IsVisible():
            continue
        t.SetTextAngleDegrees(0)
        # the part's own extent: its pads and body (courtyard less its 0.25 mm margin)
        own = [pb for o, pb in pads if o == r]
        lines = [sb for o, sb in silk if o == r]
        c = yards[r]
        body = (min([c[0] + 0.25] + [pb[0] for pb in own]), min([c[1] + 0.25] + [pb[1] for pb in own]),
                max([c[2] - 0.25] + [pb[2] for pb in own]), max([c[3] - 0.25] + [pb[3] for pb in own]))
        x0 = min([body[0]] + [sb[0] for sb in lines])
        y0 = min([body[1]] + [sb[1] for sb in lines])
        x1 = max([body[2]] + [sb[2] for sb in lines])
        y1 = max([body[3]] + [sb[3] for sb in lines])
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        it = ib = 0.15      # the text box's air may reach 0.15 mm over the part (a spot that then touches
        #                     one of its own silk lines is refused below)
        t.SetPosition(pcbnew.VECTOR2I(MMf(cx), MMf(cy)))
        b = box(t)
        w, h = b[2] - b[0], b[3] - b[1]
        # just outside the part's body (its courtyard is 0.25 mm bigger)
        # (the box's air lets it reach 0.15 mm into the part's own outline: the glyphs stay clear)
        spots = []
        for d in (0.0, 0.6):    # right outside the part, then a little further out
            above, below = (cx, y0 + it - d - h / 2), (cx, y1 - ib + d + h / 2)
            left, right = (x0 + 0.1 - d - w / 2, cy), (x1 - 0.1 + d + w / 2, cy)
            if r in LABEL_LEFT:
                spots += [left, above, below, right]
            else:
                spots += ([below, above] if r in below_refs else [above, below]) + [left, right]
        spots += [(x0 + w / 2, y0 + 0.15 - h / 2), (x1 - w / 2, y0 + 0.15 - h / 2),      # corners
                  (x0 + w / 2, y1 - 0.15 + h / 2), (x1 - w / 2, y1 - 0.15 + h / 2)]
        if x1 - x0 > 12 and y1 - y0 > 12:      # a module: its label in the middle, before crowding a neighbour
            spots.insert(2, (cx, cy))
        chosen = None
        for strict in (True, False):
            for sx, sy in spots:
                t.SetPosition(pcbnew.VECTOR2I(MMf(sx), MMf(sy)))
                bb = box(t)
                if _os.environ.get("OBD_LABEL_DEBUG") == r:
                    print(r, strict, [round(v, 2) for v in bb],
                          [(o, [round(v, 2) for v in pb]) for o, pb in pads if hit(bb, pb, 0.0)][:3],
                          [[round(v, 2) for v in lb] for lb in labels if hit(bb, lb, -0.1)][:3],
                          [o for o, c in yards.items() if o != r and hit(bb, c, -0.2)][:3])
                if bb[0] < 0.3 or bb[1] < 0.3 or bb[2] > W - 0.3 or bb[3] > H - 0.3:
                    continue
                # (KiCad's text box has ~0.25 mm of air round the glyphs: no extra gap)
                inside = (sx, sy) == (cx, cy)
                if any(hit(bb, pb, 0.0 if (o != r or inside) else -0.2) for o, pb in pads) or \
                        any(hit(bb, lb, -0.1) for lb in labels) or \
                        any(hit(bb, sb, 0.0 if (o != r or inside) else -0.2) for o, sb in silk):
                    continue
                if strict and any(o != r and hit(bb, c, -0.35) for o, c in yards.items()) and not inside:
                    continue
                chosen = bb
                break
            if chosen:
                break
        if chosen is None:      # nowhere clean: no label rather than one on top of something
            t.SetVisible(False)
            failed.append(r)
            continue
        labels.append(chosen)
    return failed
