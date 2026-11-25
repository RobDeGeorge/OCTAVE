"""Placement for the compact board: 80 x 55 mm, 6 layers, parts on both sides, plus a tab on the left edge.

The OBD-II plug sits on a small adapter board (obd-adapter-plug, build.py plug) that stands across
the left edge: the tab goes through its slot and 16 edge pads (9-16 on top, 1-8 underneath) are
soldered to it, so the plug sticks straight out of the board's end with the board in its middle.

Left to right the board follows the signal: the car plug, the input chain and the buck, the car front
ends, the STM32 in the middle, the ESP32 at the far end with its antenna pointing out of it. The top
keeps what a hand or a cable touches and what is tall or hot (connectors, buttons, the power stage, the
chips that face the plug); the bottom carries the other circuits, each as a block, the test points and SWD.
The resistors and small capacitors on logic-level nets are 0402 (obd_adapter.py says which stay 0603).

Three kinds of placement, in this order:

1. FIXED (`fixed_for`): explicit positions for the parts whose position is the design: the
   connectors at the edges, the ESP32, the MCU, the input-protection chain right behind the car
   connector, and the 5 V buck laid out as a unit.
2. TILES: each circuit as a block of rows (its chip first, then its parts in signal order), on the
   side the tile names, at the free spot nearest its target. silk.py fences and names each tile.
3. NEAR (`near_rules`): the parts that belong at one pin (the MCU's decoupling caps) go to the free
   spot nearest that pin, on the bottom.
"""

import math

import pcbnew

import os as _os
W, H = float(_os.environ.get("OBD_W", 80.0)), float(_os.environ.get("OBD_H", 55.0))   # OBD_W / OBD_H: try a size
YC = H / 2          # the plug, the input chain, the MCU and the ESP32 are placed about the middle
# The small parts (R and C to 1206, SOD-123 and SOT-23 diodes and transistors, test points) that
# no tile names go on the bottom, on a matrix of their own (CELL x ROW), all the same way round.
SMALL = ("R_0402", "C_0402", "R_0603", "C_0603", "R_1206", "C_1206", "D_SOD-123", "TestPoint")
SMALL_EXACT = ("SOT-23",)
X0 = 0.0            # left edge (the tab sticks out of it to x = -mklib.TAB_DEPTH)
# the plug's edge pads (J1962_Edge_Joint): origin on the left edge at the middle of the pins, centred
# on the edge (y = H / 2) so the plug sits square on the board's end. The pins run down the edge in
# pairs (9 / 1 at H / 2 - 14 ... 16 / 8 at H / 2 + 14, 9-16 top, 1-8 bottom). The tab is as wide as
# mklib.TAB, centred on the pins.
PLUG = (0.0, YC, 0)
# VBAT_IN lane: pin 16's pad (y H / 2 + 14) right, then up beside the edge pads into F1's pad 1
# (prewire.py); nothing else is placed in it, on the top
VBAT_LANE = (3.6, YC - 1.0, 6.9, YC + 15.5)
BACK = set()        # refs placed on the bottom side (filled by fixed_for and the tiles)
STRIP = set()       # the control strip's parts and the USB-C port: their courtyards meet (filled by fixed_for)
CENTRED = set()     # fixed refs whose (x, y) is their courtyard's centre, not their origin (filled by fixed_for)
# The antenna section (6.2 mm of the module) hangs off the right end, as Espressif's layout guide
# prefers: its edge starts at the board's end, so there is no copper keep-out on the board at all.
ESP = (W - 6.56, YC, 270)         # pins 1-14 face up, 25-38 face down; the antenna points out to +x
ESP_BODY = (W - 19.5, YC - 9.3, W, YC + 9.3)    # the module on the board (its courtyard also spans the keep-out)
USB_X = W - 5.6                   # the one USB-C port, on the top edge at the module's end
# The control strip along the top edge, from the buck to the USB-C port, everything a hand touches in one
# row with no room to spare (the courtyards meet): the CONFIG switch, RESET, USER, the ESP32's RST / BOOT.
# The buttons are turned 90 degrees (their pads point along y, so each is only 5.7 mm wide). The CONFIG
# switch's positions are printed under it; the RGB LED and its resistors stand under the USB-C port.
BTN_Y, BTN_PITCH, BTN_ROT = 5.6, 5.74, 90     # (1.3 mm off the edge: room for the labels above)
DIP_AT = (42.3, 5.6, 90)            # courtyard centre: 7.4 x 9.5 mm, switches 1-4 read left to right
BTN_X = [48.9 + i * BTN_PITCH for i in range(4)]
LED_X, LED_Y = W - 2.1, 10.4
# The STM32 and the ESP32 stand on the board's centre line, with the plug
MCU_AT = (50.0, YC, 0)
# No mounting holes: the board hangs from the plug (a printed case holds it by its edges)
HOLES = []
HOLE_KEEP = 3.45
# The bottom edge, left to right: J9 (panic button, JST SH) under the input chain, J5 (STM32 expansion,
# DNP, rows along x, pin 1 left), J11 (pod bus, JST GH), the Qwiic socket, J10 microSD under the module
# (card out of the edge). J8 (ESP32 IO, DNP) between the buttons and the module.
PANIC_AT = (7.5, H - 3.3, 0)        # JST SH, opening out
EXP_AT = (23.6, H - 2.4, 90)        # (its courtyard starts 1.8 mm left of pin 1)
POD_AT = (45.9, H - 3.5, 0)         # courtyard centre; JST GH, opening out
QWIIC_AT = (54.95, H - 3.3, 0)
SD_AT = (W - 8.6, H - 6.55, 180)       # courtyard to the edge, card slot (contact side) facing out
ESPIO_AT = (51.0, 11.9, 90)         # pin 1; the header lies between the buttons and the module
LOBE = None                         # (build.py and raster.py still take one: the outline is a plain rectangle)
H_ALL = H
# USB-C front face (footprint y +3.65) 0.3 mm past the top edge, so a plug seats fully
USB_Y = 3.65 - 0.3
GAP = 0.25          # between two courtyards, and between two tiles
# a part no tile, rule or fixed place names (there should be none) is packed on these lattices: small
# parts on the bottom in CELL x ROW cells, the rest on the top on GRID
GRID = 1.0
CELL, ROW = 3.5, 2.5

# keep-outs for placement (both sides) and routing (the ESP32 antenna's is off the board's end)
KEEPOUTS = []
# no via and no copper pour beside the plug's edge pads (F.Cu / B.Cu): they are soldered by hand, and a slip
# through the mask there would reach copper ahead of the fuse
# (to 0.45 mm past the pads: beside pins 12-15 a via still has to fit between this and the battery lane)
VIA_KEEPOUTS = [(0.0, YC - 16.9, 3.45, YC + 16.9)]
# copper beside the LDO's tab: heat spreading + vias into the 3V3 plane
# (1.8 mm under the STM32: its bottom row's tracks need the room; at 0.3 mm two of them found no way out)
LDO_AT = (47.7, YC + 12.2, 0)
LDO_TAB = (LDO_AT[0] + 2.0, LDO_AT[1] - 3.2, LDO_AT[0] + 5.8, LDO_AT[1] + 3.2)

# the buck converter, laid out around U7 at (BX, BY), top-left, above the input chain. Its hand-laid
# copper (prewire.py) has vias under the IC, its input side and its output capacitors: BUCK_BACK is what
# that closes to parts on the bottom (the left of it is the wizard's place). Under the inductor and the
# OR diode the bottom is open: the GND plane lies between.
BX, BY = 19.0, 14.5
BUCK_BOX = (BX - 11.0, BY - 14.0, BX + 19.2, BY + 6.6)
# (the corner behind the input capacitors and the catch diode is open: the DLC switch sits there, at VBAT_P)
DLCSW_AT = (10.2, 0.5)
BUCK_BACK = [(BUCK_BOX[0], 12.8, BX + 5.4, BUCK_BOX[3]), (BX + 0.9, BUCK_BOX[1], BUCK_BOX[2], BY - 8.3), (BX + 0.9, BY - 8.3, BX + 5.4, 12.8)]
ESD_AT = (4.9, 10.75)   # the J1850 TVSs' tile (top-left corner): silk.py steps the buck's fence round it
CHAIN_DY = 0.5      # the input chain, moved down from the centre line to clear the buck
ROW_Y = BUCK_BOX[3] + 0.2 + GAP / 2     # the blocks right of the input chain start here, under the buck's fence

# Room kept free of parts on the bottom for what silk.py prints there: the plug's pin names beside its
# pads, the board's name / revision / order number
RESERVED = {
    "pins": (3.3, YC - 15.9, 9.9, YC + 15.9),
    "title": (0.5, 0.5, 7.7, 11.0),
}
# ... and on the top: the CONFIG switch's positions under the switch, J8's pin names under its pins (with
# its own name before them), J5's pin table over J5
RESERVED_TOP = {
    "config": (38.45, 10.5, 47.6, 20.0),
    "j8": (47.8, ESPIO_AT[1] + 1.9, ESPIO_AT[0] + 23.6, ESPIO_AT[1] + 5.8),
    "exp": (24.2, H - 15.35, 40.3, H - 6.85),
}

# reference labels placed by hand (absolute x, y in mm, text angle): applied after placement
LABELS = {
    "U1": (MCU_AT[0] + 6.6, MCU_AT[1] - 7.8, 0),    # over the chip, after "STM32"
    "U8": (LDO_AT[0] + 3.4, LDO_AT[1] + 4.5, 0),    # under the LDO, after "3V3 LDO"
    "U4": (W - 20.9, YC + 5.0, 90),             # left of the module, reading up
    "U6": (13.6, YC - 26.2 + 28.3 + CHAIN_DY, 90),         # turned, between the TVS and its own pins
}


def _refs(design):
    by_value = {}
    for p in design.parts:
        by_value.setdefault((p.block.name, p.value), []).append(p.ref)

    def ref(block, value, i=0):
        return by_value[(block, value)][i]
    return ref


BUCK = set()        # the buck's parts (filled by fixed_for): they take their place on both sides
CHAIN = set()       # the input chain's parts (filled by fixed_for): laid out by hand, tight on purpose


def fixed_for(design):
    ref = _refs(design)
    X, Y = BX, BY
    S = YC - 37.5 + CHAIN_DY       # the input chain was laid out round the middle of a 75 mm board
    f = {
        ref("mcu", "STM32G474RET6"): MCU_AT,
        # input chain right of the plug: J6.16 -> PTC -> TVS -> ideal diode (LM74700 + FET)
        # (the PTC 0.55 mm and the FET, its cap and the LM74700 0.35 mm right of where Rev 0 had them: the PTC's
        # battery pad stands 1.2 mm from the plug's hand-soldered pads, not 0.65)
        ref("power", "2A 60V"): (8.55, 34.6 + S, 0),       # pad 1 VBAT_IN left (the lane from pin 16), pad 2 VBAT_F right
        ref("power", "5.0SMDJ33CA"): (9.4, 42.6 + S, 270),    # pad 1 (VBAT_F) up at F1
        ref("power", "AO4264E"): (16.95, 36.0 + S, 0),     # source pins 1-3 left, drain 5-8 right
        ref("power", "LM74700QDBVRQ1"): (16.85, 41.3 + S, 180),  # GATE / CATHODE left (to the FET), VCAP right
        ref("power", "100n", 1): (19.85, 41.3 + S, 90),            # VCAP cap: pad 2 (VBAT_F) up at ANODE
        ref("power", "100n", 0): (13.6, 44.6 + S, 270),            # LM74700 input cap at EN / VBAT_F
        ref("power", "47u 50V"): (19.2, 47.8 + S, 0),
        ref("power", "10u", 0): (22.6, 36.0 + S, 90),              # VBAT_P 10u at the FET's drain
        # 3.3 V LDO (SOT-223) below the MCU, the QSPI flash beside it
        ref("power", "AP2114H-3.3"): LDO_AT,
        ref("flash", "W25Q128JVSIQ"): (W - 21.3, YC + 13.2, 0),
    }
    CHAIN.update(r for r in f if design_block(design, r) == "power" and f[r] != LDO_AT)
    btn = [ref("ui", "RESET"), ref("ui", "USER"), ref("espusb", "ESP_RST"), ref("espusb", "ESP_BOOT")]
    f.update({r: (x, BTN_Y, BTN_ROT) for r, x in zip(btn, BTN_X, strict=True)})
    STRIP.update(btn + [ref("ui", "CONFIG"), ref("usb", "USB-C")])
    f.update({
        ref("car", "OBD-II plug (J1962 male)"): PLUG,
        ref("exp", "EXP"): EXP_AT,
        ref("esp32", "ESP_IO"): ESPIO_AT,
        ref("exp", "Qwiic"): QWIIC_AT,
        ref("sense", "PANIC"): PANIC_AT,
        ref("sd", "microSD"): SD_AT,
    })
    grid = {ref("ui", "RGB"): (LED_X, LED_Y, 0)}
    grid[ref("ui", "CONFIG")] = DIP_AT
    grid[ref("pod", "POD BUS")] = POD_AT
    grid[ref("pod", "0.5A 60V")] = (17.2, YC + 16.9, 0)    # the pod fuse: under the 47u, at VBAT_P
    f.update(grid)
    CENTRED.update(grid)
    buck = {
        ref("esp32", "ESP32-WROOM-32E-N8"): ESP,
        ref("usb", "USB-C"): (USB_X, USB_Y, 180),
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
        ref("power", "13k"): (X + 5.6, Y + 1.08, 0),          # COMP: R22 + C27 just under pin 6's row (1.1 mm
        ref("power", "6.8n"): (X + 8.8, Y + 1.08, 0),         # from the inductor's switch-node pad), 47p below
        ref("power", "47p"): (X + 7.0, Y + 2.65, 0),
        ref("power", "12k"): (X + 2.9, Y + 4.7, 270),       # FB divider under pin 5
        ref("power", "68k"): (X + 4.6, Y + 4.7, 90),
        ref("power", "160k"): (X - 2.9, Y + 4.7, 270),        # RT under pin 4
        # VIN high-frequency cap right at pin 2 (REVIEW #72): pad 1 (VBAT_P) on pin 2's row, pad 2 (GND)
        # beside the unused EN pin, its via to the GND plane just below
        ref("power", "100n", 2): (X - 4.6, Y + 0.145, 270),
    }
    f.update(buck)
    BUCK.update(r for r in buck if design_block(design, r) == "power")
    return f


def design_block(design, r):
    return next(p.block.name for p in design.parts if p.ref == r)


def near_rules(design):
    """[(ref, [(target ref, pad number), ...], max distance mm, (dx, dy))]: put ref as close as possible
    to the mean of the target pads plus (dx, dy) (on the bottom: these are all small parts)."""
    ref = _refs(design)
    parts = {p.ref: p for p in design.parts}
    mcu = ref("mcu", "STM32G474RET6")
    rules = []

    def on(r, *targets, dist=10.0, off=(0.0, 0.0)):
        rules.append((r, list(targets), dist, off))

    # the MCU's decoupling, under the chip at the pin each cap serves: VDD pins 16 / 32 / 48 / 64,
    # VDDA / VREF+ (29 / 28), NRST (7); the 4.7u bulk at VBAT / VDD (1 / 64), the battery divider's
    # filter cap at PA4 (18), BOOT0's pull-down at PB8 (61)
    for i, pin in enumerate(("16", "32", "48", "64", "29", "7")):
        on(ref("mcu", "100n", i), (mcu, pin), dist=6)
    # (the 1u inside the 100n, towards the chip's middle: beside it, its vias walled in pins 25 / 26)
    on(ref("mcu", "1u"), (mcu, "29"), dist=6, off=(0.25, -2.3))
    on(ref("mcu", "4.7u"), (mcu, "1"), dist=6)
    on(ref("power", "100n", 4), (mcu, "18"), dist=6)
    on(ref("mcu", "10k"), (mcu, "61"), dist=6)
    assert all(r[0] in parts for r in rules)
    return rules


# Tiles: each circuit as a block of rows, its chip first, then its parts in signal order, every row
# starting at the same end. A part's slot is its courtyard width rounded up to TILE_PITCH (0402: 2,
# 0603: 3, SOT-23: 4, SOD-123 / 1206: 5, SOIC-8: 7), so tiles share columns; rows are TILE_VGAP apart:
# 0.9 mm between two rows of 0402 pads and 1.1 between 0603, which a 0.45 mm via with its clearance fits.
# The small parts carry no reference on the silkscreen, so no room is kept for one. The first row
# and any row of one part are centred on the tile, unless the row's first part is written "<REF".
# "REF@90" turns a part.
#   (name on the silkscreen, side "F" / "B", target (x, y) of the tile's centre, rows[, row gap])
# A tile takes the free spot nearest its target on its side, in this order; TILE_TITLE mm above its
# first row are kept for its name (none for the tiles in NO_TITLE). A target (x, y, "tl") is the tile's
# top-left corner; (x, y, "tl") may also be ("below", name) or ("right", name): against that tile, GAP
# away, so the two share a fence. On the bottom the rows are written as they read with the board turned over (left to
# right), so they run right to left in board coordinates.
TILE_PITCH, TILE_VGAP, TILE_TITLE, TILE_MARGIN = 1.1, 0.5, 1.4, 0.2
NO_TITLE = {"PANIC", "LED", "VBAT", "FLASH C", "BOOT RST", "VJ CLAMP"}
_MX, _MY = MCU_AT[0], MCU_AT[1]
_TL = "tl"
TILES = [
    # top, right of the input chain: the two CAN transceivers, turned so their bus pins face the plug
    # (their caps over them, at the supply pins' end), the J1850 supply; the panic input beside J9; the LED's resistors under it
    ("MS CAN", "F", (24.2, ROW_Y, _TL), [["C39", "R27", "C38"], ["U10@180", "R28@90"]]),
    ("HS CAN", "F", ("below", "MS CAN"), [["C37", "R25", "C36"], ["U9@180", "R26@90"]]),
    ("VJ", "F", ("right", "MS CAN"), [["U11"], ["C41", "C42"], ["R32", "R33"], ["R34", "R35"], ["<Q4"]]),
    ("PANIC", "F", (11.0, H - 7.5, _TL), [["R67", "C49", "D28"], ["R66"], ["D30"]]),
    ("LED", "F", (LED_X - 1.3, LED_Y + 1.3, _TL), [["R3"], ["R4"], ["R5"]]),
    ("FLASH C", "F", (W - 16.35, YC + 12.1, _TL), [["C13", "R8"]]),
    # top, beside the plug's pins 2 / 10, in the buck's empty corner: the J1850 lines' TVSs
    ("ESD", "F", ESD_AT + (_TL,), [["D19"], ["D20"]]),
    # bottom, car side, right of the plug's pin names: the clamps (both CAN pairs', the K / L lines'), the
    # K / L-line drivers with their test points, the J1850 receiver and drivers (the Bus+ driver under the
    # buck's inductor, with the battery divider and the J1850 test points)
    # (its two rows either side of the three GND vias of the buck's catch diode and input capacitors)
    ("DLC SW", "B", DLCSW_AT + (_TL,), [["R31", "C40", "R29"], ["Q3", "Q2"], ["R30", "D10"]], [1.6, 0.3]),
    ("CAN ESD", "B", (10.15, 21.4, _TL), [["D9", "D8"]]),
    ("K/L ESD", "B", ("below", "CAN ESD"), [["D24", "D22"]]),     # (the K-line's nearest the plug: prewire.py runs the pin straight to it)
    ("K-LINE", "B", ("below", "K/L ESD"), [["R51"], ["Q8", "D21"], ["R52", "R53", "TP9"]]),
    ("L-LINE", "B", (3.8, H - 9.35, _TL), [["R54", "Q9", "TP10"], ["R55", "D23", "R56"]]),
    ("J1850 RX", "B", (21.8, 21.75, _TL), [["U12@270", "C43", "R50", "R58"], ["D14", "R47", "D15", "R44", "R59"],
                                           ["D16", "R48", "D17", "R49", "C58"], ["D18", "R57", "R45", "R46", "R43"]]),
    ("J1850 PWM-", "B", ("below", "J1850 RX"), [["Q7", "D13"], ["R39", "R40", "R41", "R42"]]),
    # the J1850 regulator's input clamp, on the back under the regulator's corner
    ("VJ CLAMP", "B", ("right", "J1850 PWM-"), [["Q15", "D31@90"], ["R80"]]),
    ("J1850 PWM+", "B", (24.65, 6.45, _TL), [["Q5", "R36", "Q6", "R37"], ["D11", "R38", "D12"]]),
    ("VBAT", "B", ("below", "J1850 PWM+"), [["R23", "R24"]]),
    ("TP J1850", "B", ("below", "VBAT"), [["TP6", "TP7", "TP8"]]),
    # bottom, behind the buttons: the ESP32's USB serial port with its auto-reset pair, the hub at the USB port
    ("UART", "B", (39.65, 0.5, _TL), [["U14"], ["C45", "R63", "R64", "R65"], ["Q10", "Q11"]]),
    ("USB HUB", "B", (49.7, 0.5, _TL), [["U2", "U18", "Y2"], ["R6", "R7", "C12", "R69", "C57", "C55", "C56"]]),
    # bottom, under the STM32: its crystal
    ("XTAL", "B", (_MX - 3.5, _MY - 1.5), [["Y1@90"], ["C11", "C10"]]),
    # bottom, between J8 and the module's pins: the supply test points, the STM32's series resistors to the
    # ESP32, the module's supply parts at its 3V3 pin (2, top right)
    ("TP SUPPLY", "B", (ESPIO_AT[0] - 1.7, ESPIO_AT[1] + 2.05, _TL), [["TP1", "TP2", "TP3", "TP4", "TP5"]]),
    ("LINK", "B", ("right", "TP SUPPLY"), [["R11", "R12"], ["R13", "R14"]]),
    ("ESP32", "B", (W - 7.5, ESPIO_AT[1] + 2.05, _TL), [["C14"], ["C15", "C16", "R9"]]),
    # bottom, between the STM32 and the module and under the module: the switched battery for the car
    # front ends, the secure element and the accelerometer, the pull resistors (I2C, PA10, the ESP32's TX
    # line), the FET that holds the ESP32 in reset while BOOT0 is switched on, the ESP32's load switch
    # (with the CONFIG switch's series resistor into it)
    # (the three parts on VBAT_P, Q2 / D10 / R29, down the tile's one side: prewire.py joins them there)
    ("ACCEL CRYPTO", "B", (57.25, YC + 1.7, _TL), [["U15", "U16"], ["C46", "C47", "C48"]]),
    ("PULLS", "B", (W - 10.8, YC - 7.6, _TL), [["R16", "R17", "R18", "R78"]]),
    ("BOOT RST", "B", (W - 6.75, YC - 4.5, _TL), [["Q14"]]),
    ("ESP SW", "B", (W - 10.8, YC + 0.75, _TL), [["U5", "D26"], ["C18", "C17", "R15", "R70"], ["D27"]]),
    # bottom, under the LDO, the flash and the card socket: the LDO's caps, SWD, the USB port's OR diode,
    # the card's parts, the pod bus
    ("3V3 LDO", "B", (42.25, YC + 9.0, _TL), [["C33", "C34"], ["C32"]]),
    ("SWD", "B", (42.25, YC + 15.7, _TL), [["J1@90"]]),
    ("VBUS", "B", (54.0, YC + 11.2, _TL), [["D7"]]),
    ("POD PWR", "B", ("right", "VBUS"), [["U19"], ["C52", "R72", "R79", "R73", "C53"]]),
    ("SD CARD", "B", (W - 13.0, H - 6.1, _TL), [["RN1@90", "R68", "C50", "C51"]]),
    ("RS-485", "B", (50.65, H - 9.3, _TL), [["U17", "R77@90"], ["C54", "R76", "R74", "R75"]]),
]
TILE_RECTS = {}     # name -> (side, (x0, y0, x1, y1)): filled by place_tiles, read by silk.py
TILE_FAIL = []      # (name, side, width, height) of every tile that found no room: place_all says so


def _tile_pitch(w):
    return math.ceil((w + 0.2) / TILE_PITCH - 1e-6) * TILE_PITCH


def place_tiles(fps, top, bot, placed):
    """Place TILES (see above) in the order written. Prints how far each tile landed from its target
    when that is far."""
    from pcbgen import place
    for name, side, target, rows, *opt in TILES:
        back = side == "B"
        occ = bot if back else top
        vgap = opt[0] if opt else TILE_VGAP
        shapes = []
        for row in rows:
            items = []
            for spec in row:
                r, _, rot = spec.lstrip("<").partition("@")
                fp = fps[r]
                place(fp, 0, 0, int(rot or 0), back=back)
                items.append((r, fp, int(rot or 0), courtyard(fp)))
            shapes.append(items)
        width = max(sum(_tile_pitch(c[2] - c[0]) for *_, c in items) for items in shapes)
        heights = [max(c[3] - c[1] for *_, c in items) for items in shapes]
        title = 0.0 if name in NO_TITLE else TILE_TITLE
        gaps = list(vgap) if isinstance(vgap, (list, tuple)) else [vgap] * (len(shapes) - 1)     # one gap, or one per row boundary
        height = title + sum(heights) + sum(gaps)
        # the free spot nearest the target (the tile's centre), on a 0.25 mm lattice. A target
        # (x, y, "tl") is where the tile's top-left corner goes (its margin included)
        m = TILE_MARGIN
        if target[0] in ("below", "right"):
            if target[1] not in TILE_RECTS:
                TILE_FAIL.append((name, side, round(width + 2 * m, 1), round(height + 2 * m, 1)))
                continue
            o = TILE_RECTS[target[1]][1]
            target = (o[0], o[3] + GAP, _TL) if target[0] == "below" else (o[2] + GAP, o[1], _TL)
        if len(target) == 3:
            target = (target[0] + m + width / 2, target[1] + m + height / 2)
        best = None
        for ring in range(0, 240):
            d = ring * 0.25
            cands = [(dx, dy) for dx in (-d, d) for dy in _frange(-d, d)] + [(dx, dy) for dy in (-d, d) for dx in _frange(-d, d)] \
                if ring else [(0.0, 0.0)]
            for dx, dy in sorted(set(cands), key=lambda q: math.hypot(*q)):
                x0, y0 = target[0] + dx - width / 2, target[1] + dy - height / 2
                rect = (x0 - m, y0 - m, x0 + width + m, y0 + height + m)
                if ring == 0 and _os.environ.get("OBD_TILEDBG"):
                    print("dbg", name, [round(v, 2) for v in rect], "inside", _inside(rect), "blocked by", [tuple(round(v, 2) for v in o) for o in occ if _overlap(rect, o)])
                if _inside(rect) and not any(_overlap(rect, o) for o in occ):
                    best = (x0, y0, math.hypot(dx, dy))
                    break
            if best:
                break
        if best is None:
            TILE_FAIL.append((name, side, round(width + 2 * m, 1), round(height + 2 * m, 1)))
            continue
        x0, y0, off = best
        if _os.environ.get("OBD_TILEDBG"):
            print(f"tile {name:13s} {side} {width + 2 * m:5.1f} x {height + 2 * m:5.1f} at {x0 - m:5.1f},{y0 - m:5.1f}")
        if off > 3.0:
            print(f"layout: tile {name} is {off:.1f} mm from its target")
        y = y0 + title
        for items, h, gap in zip(shapes, heights, gaps + [0.0], strict=True):
            centred = (items is shapes[0] or len(items) == 1) and not rows[shapes.index(items)][0].startswith("<")
            x = 0.0 if not centred else (width - sum(_tile_pitch(c[2] - c[0]) for *_, c in items)) / 2
            for r, fp, rot, c in items:
                p = _tile_pitch(c[2] - c[0])
                cx, cy = (x0 + width - x - p / 2) if back else (x0 + x + p / 2), y + h / 2
                place(fp, cx - (c[0] + c[2]) / 2, cy - (c[1] + c[3]) / 2, rot, back=back)
                if back:
                    BACK.add(r)
                if tht(fp):
                    (top if back else bot).append(courtyard(fp))
                placed.add(r)
                x += p
            y += h + gap
        rect = (x0 - m, y0 - m, x0 + width + m, y0 + height + m)
        occ.append(rect)
        TILE_RECTS[name] = (side, rect)


def _frange(a, b, step=0.25):
    n = int(round((b - a) / step))
    return [a + i * step for i in range(n + 1)]


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
    """The free spot nearest `target` for fp (turned 0 or 90 degrees), by fp's centre, on a 0.25 mm lattice."""
    from pcbgen import place
    row, col = 0.25, 0.25       # (not the small-part matrix: these belong at a pin)
    tx, ty = round(target[0] / col) * col, round(target[1] / row) * row
    shapes = []
    place(fp, 0, 0, 0, back=back)
    for rot in (0, 90):
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
    tiled = {spec.lstrip("<").partition("@")[0] for _, _, _, rows, *_ in TILES for row in rows for spec in row}
    fixed = {r: v for r, v in fixed_for(design).items() if r not in tiled}
    rules = near_rules(design)
    # one occupancy list per side; through-hole parts (and the plug's two-sided edge pads) take both
    top = list(KEEPOUTS) + [LDO_TAB, VBAT_LANE] + list(RESERVED_TOP.values())
    bot = list(KEEPOUTS) + BUCK_BACK + [LDO_TAB] + list(RESERVED.values())
    placed = set()
    for r, (x, y, rot) in fixed.items():
        place(fps[r], x, y, rot, back=r in BACK)
        if r in CENTRED:    # move it so its courtyard's centre lands on (x, y)
            c = courtyard(fps[r])
            place(fps[r], 2 * x - (c[0] + c[2]) / 2, 2 * y - (c[1] + c[3]) / 2, rot, back=r in BACK)
        # the ESP32's courtyard is the module plus its antenna keep-out: only the body
        c = ESP_BODY if (x, y, rot) == ESP else courtyard(fps[r])
        both = tht(fps[r]) or (x, y, rot) == PLUG
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
    # fixed parts are not checked against each other when placed: say so if two collide (the buck's
    # own hand layout is tight on purpose, and the ESP32 counts by its body)
    fx = [(r, ESP_BODY if fixed[r] == ESP else courtyard(fps[r]), r in BACK or tht(fps[r]),
           r not in BACK or tht(fps[r])) for r in fixed if fixed[r] != PLUG]
    for i, (ra, ca, ba, ta) in enumerate(fx):
        for rb, cb, bb, tb in fx[i + 1:]:
            if ((ba and bb) or (ta and tb)) and _overlap(ca, cb) and not {ra, rb} <= BUCK | CHAIN and not {ra, rb} <= STRIP \
                    and (ca[2] > cb[0] and cb[2] > ca[0] and ca[3] > cb[1] and cb[3] > ca[1]) >= ({ra, rb} <= STRIP):
                print(f"layout: fixed parts {ra} and {rb} overlap")

    def side(fp):
        return (bot, True) if small(fp) else (top, False)

    def run_rules():
        progress = True
        while progress:
            progress = False
            for r, targets, dist, off in rules:
                if r in placed or not all(t in placed for t, _ in targets):
                    continue
                pts = [_pad_pos(fps[t], n) for t, n in targets]
                tgt = (sum(p[0] for p in pts) / len(pts) + off[0], sum(p[1] for p in pts) / len(pts) + off[1])
                occ, back = side(fps[r])
                spot = _place_near(fps[r], tgt, dist, occ, back)
                if spot is None:
                    spot = _place_near(fps[r], tgt, 2 * dist, occ, back)
                    if spot:
                        print(f"layout: {r} is more than {dist} mm from {targets}")
                if spot is None:
                    print(f"layout: {r} found no spot within {dist} mm of {targets}")
                    continue
                occ.append(spot)
                if back:
                    BACK.add(r)
                if tht(fps[r]):
                    (top if back else bot).append(spot)
                placed.add(r)
                progress = True

    run_rules()                 # the pin-bound parts first: the tiles take what is left
    place_tiles(fps, top, bot, placed)
    for name, sd, w, h in TILE_FAIL:
        print(f"layout: tile {name} ({w} x {h} mm) found no room on side {sd}: its parts are packed wherever they fit")
    for b in design.blocks:     # anything no tile, rule or fixed place names: packed anywhere, and said
        for p in b.parts:
            if p.ref in placed or p.ref not in fps:
                continue
            fp = fps[p.ref]
            occ, back = side(fp)
            place(fp, 0, 0, 0, back=back)
            cx0, cy0, cx1, cy1 = courtyard(fp)
            spot = _find((1, 1, W - 1, H - 1), cx1 - cx0, cy1 - cy0, occ, *_lattice(fp))
            if spot is None:
                raise RuntimeError(f"{p.ref} ({p.value}) does not fit anywhere")
            place(fp, spot[0] - cx0, spot[1] - cy0, 0, back=back)
            occ.append(spot)
            if back:
                BACK.add(p.ref)
            placed.add(p.ref)
            print(f"layout: {p.ref} ({p.value}, {b.name}) has no tile or rule: packed at {spot[0]:.1f}, {spot[1]:.1f}")


def place_labels(fps, skip=(), back=False):
    """(`fps`: the parts of one side, with the through-hole parts of the other in `skip`.)
    Every reference upright, just outside its part: above, else below, left or right; first a spot
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
    # every part's silkscreen drawing (outlines, pin-1 marks): labels keep off it too
    silk = []
    for fp in fps.values():
        for g in fp.GraphicalItems():
            if g.GetLayer() == (pcbnew.B_SilkS if back else pcbnew.F_SilkS) and not isinstance(g, pcbnew.PCB_TEXT):
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
            spots += [above, below, left, right]
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
