"""Project symbols (pinouts checked against the datasheets, 2026-11-02)."""

from mklib import ic, write

P, I, O, B, PI, PO, OC, NC = ("passive", "input", "output", "bidirectional", "power_in", "power_out",
                              "open_collector", "no_connect")

SYMBOLS = [
    None,       # (the SOIC-8 N-MOSFET, drawn below)
    # TI TPS22918 DBV: 1 VIN, 2 GND, 3 ON, 4 CT, 5 QOD, 6 VOUT
    ic("TPS22918", [(1, "VIN", PI), (3, "ON", I), (4, "CT", P)],
       [(6, "VOUT", PO), (5, "QOD", P), (2, "GND", PI)],
       footprint="Package_TO_SOT_SMD:SOT-23-6", desc="5.5 V 2 A load switch"),
    # WCH CH340C SOIC-16 (internal clock). V3 is its regulator output, tied to VCC when VCC is 3.3 V:
    # passive here, so ERC doesn't call that two power outputs
    ic("CH340C", [(16, "VCC", PI), (4, "V3", P), (5, "UD+", B), (6, "UD-", B), None, (1, "GND", PI)],
       [(2, "TXD", O), (3, "RXD", I), (13, "~{DTR}", O), (14, "~{RTS}", O), (9, "~{CTS}", I), (10, "~{DSR}", I),
        (11, "~{RI}", I), (12, "~{DCD}", I), (15, "R232", I), (7, "NC", NC), (8, "NC", NC)],
       footprint="Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", desc="USB to UART, internal clock"),
    # WCH CH334P QFN-16 (datasheet V2.4 figure 1-1). V5 and VDD33 both on the board's 3.3 V
    ic("CH334P", [(15, "V5", PI), (16, "VDD33", PI), (12, "DPU", B), (11, "DMU", B), None, (2, "XI", I), (1, "XO", O),
                  (13, "~{RESET}", I), (14, "PGANG", B), (17, "GND", PI)],
       [(10, "DP1", B), (9, "DM1", B), (8, "DP2", B), (7, "DM2", B), (6, "DP3", B), (5, "DM3", B), (4, "DP4", B),
        (3, "DM4", B)],
       footprint="Package_DFN_QFN:WCH_QFN-16-1EP_3x3mm_P0.5mm_EP1.8x1.8mm", desc="USB 2.0 hub, 4 ports"),
    # ST LIS2DH12 LGA-12 (datasheet table 2): 1 SCL, 2 CS, 3 SDO/SA0, 4 SDA, 5 RES (to GND), 6-8 GND,
    # 9 VDD, 10 VDD_IO, 11 INT2, 12 INT1
    ic("LIS2DH12", [(9, "VDD", PI), (10, "VDD_IO", PI), (2, "~{CS}", I), (3, "SDO/SA0", I), (5, "RES", P),
                    (6, "GND", PI), (7, "GND", PI), (8, "GND", PI)],
       [None, None, (1, "SCL/SPC", I), None, (4, "SDA/SDI/SDO", B), None, (12, "INT1", O), (11, "INT2", O)],
       footprint="Package_LGA:LGA-12_2x2mm_P0.5mm", desc="3-axis accelerometer, I2C / SPI"),
    # TI TPS27S100A, HTSSOP-14 (SLVSE42B p3). IN and OUT passive, so ERC wants no power flag on the fused rail
    ic("TPS27S100A", [(8, "IN", P), (9, "IN", P), (10, "IN", P), None, (3, "EN", I), (12, "DIAG_EN", I),
                      (13, "ILIM", P), None, (2, "GND", PI), (15, "GND", PI)],
       [(5, "OUT", P), (6, "OUT", P), (7, "OUT", P), None, (14, "~{FLT}", OC), None, (1, "NC", NC), (4, "NC", NC),
        (11, "NC", NC)],
       footprint="Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask2.94x3.34mm",
       desc="40 V high-side switch, adjustable current limit"),
    # half-duplex RS-485 transceiver in the standard SOIC-8 pinout (THVD2450, MAX3485, SP3485...)
    ic("RS485_HD", [(4, "DI", I), (3, "DE", I), (2, "RE", I), (1, "RO", O)],
       [(8, "VCC", PI), (6, "A", B), (7, "B", B), (5, "GND", PI)],
       footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", desc="Half-duplex RS-485 transceiver, 3.3 V, RE active low"),
]



def _nmos_so8():
    """SOIC-8 N-MOSFET, AO4264E style (S 1-3, G 4, D 5-8): the box and pins of ic(), with the
    transistor and its body diode drawn in it in place of the pin names. Source left, drain right."""
    sym = ic("NMOS_SO8", [(4, "G", I), None, (1, "S", P), (2, "S", P), (3, "S", P)],
             [(5, "D", P), (6, "D", P), (7, "D", P), (8, "D", P)], ref="Q", width=10.16,
             footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", desc="N-MOSFET, SOIC-8 (AO4264E)")
    sym[2] = ["pin_names", ["offset", 1.016], ["hide", "yes"]]
    body = next(n for n in sym if isinstance(n, list) and n[0] == "symbol" and str(n[1]).strip('"').endswith("_0_1"))

    def line(*pts, w=0.2):
        body.append(["polyline", ["pts", *[["xy", x, y] for x, y in pts]],
                     ["stroke", ["width", w], ["type", "default"]], ["fill", ["type", "none"]]])
    line((-5.08, 0), (-2.54, 0), (-2.54, 1.27))                 # source: pins 1-3 on one bar
    line((-3.81, 0), (-3.81, -5.08))
    line((-5.08, -2.54), (-3.81, -2.54))
    line((-5.08, -5.08), (-3.81, -5.08))
    line((5.08, 0), (2.54, 0), (2.54, 1.27))                    # drain: pins 5-8 on one bar
    line((3.81, 5.08), (3.81, -2.54))
    for y in (5.08, 2.54, -2.54):
        line((5.08, y), (3.81, y))
    for x0, x1 in ((-3.05, -1.52), (-0.76, 0.76), (1.52, 3.05)):    # the channel, in three pieces
        line((x0, 1.27), (x1, 1.27), w=0.3)
    line((-2.54, 2.29), (2.54, 2.29), w=0.3)                    # the gate plate and its lead
    line((-5.08, 5.08), (0, 5.08), (0, 2.29))
    line((0, 1.27), (0, 0), (-2.54, 0))                         # bulk to source
    line((-0.5, 0.45), (0, 1.2), (0.5, 0.45))                   # arrow into the channel: N
    line((-2.54, 0), (-2.54, -2.54), (2.54, -2.54), (2.54, 0))  # body diode, source to drain
    body.append(["polyline", ["pts", ["xy", -0.76, -1.78], ["xy", -0.76, -3.3], ["xy", 0.76, -2.54], ["xy", -0.76, -1.78]],
                 ["stroke", ["width", 0.2], ["type", "default"]], ["fill", ["type", "outline"]]])
    line((0.76, -1.78), (0.76, -3.3))
    return sym


SYMBOLS[0] = _nmos_so8()

if __name__ == "__main__":
    write(SYMBOLS)

# Project footprints (lib/OCTAVE.pretty)
from mklib import j1962_edge_joint, j1962_plug_adapter, led_rgb_1615  # noqa: E402

FOOTPRINTS = [j1962_edge_joint(), j1962_plug_adapter(), led_rgb_1615()]
