"""Project symbols (pinouts checked against the datasheets, 2026-11-02)."""

from mklib import ic, write

P, I, O, B, PI, PO, OC, NC = ("passive", "input", "output", "bidirectional", "power_in", "power_out",
                              "open_collector", "no_connect")

SYMBOLS = [
    # SOIC-8 N-MOSFET, AO4264E style: S 1-3, G 4, D 5-8
    ic("NMOS_SO8", [(4, "G", I), None, (1, "S", P), (2, "S", P), (3, "S", P)],
       [(5, "D", P), (6, "D", P), (7, "D", P), (8, "D", P)], ref="Q", width=10.16,
       footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", desc="N-MOSFET, SOIC-8 (AO4264E)"),
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
    # ST LIS2DH12 LGA-12 (datasheet table 2): 1 SCL, 2 CS, 3 SDO/SA0, 4 SDA, 5 RES (to GND), 6-8 GND,
    # 9 VDD, 10 VDD_IO, 11 INT2, 12 INT1
    ic("LIS2DH12", [(9, "VDD", PI), (10, "VDD_IO", PI), (2, "~{CS}", I), (3, "SDO/SA0", I), (5, "RES", P),
                    (6, "GND", PI), (7, "GND", PI), (8, "GND", PI)],
       [(1, "SCL/SPC", I), (4, "SDA/SDI/SDO", B), None, (12, "INT1", O), (11, "INT2", O)],
       footprint="Package_LGA:LGA-12_2x2mm_P0.5mm", desc="3-axis accelerometer, I2C / SPI"),
]

if __name__ == "__main__":
    write(SYMBOLS)

# Project footprints (lib/OCTAVE.pretty)
from mklib import j1962_edge_joint, j1962_plug_adapter  # noqa: E402

FOOTPRINTS = [j1962_edge_joint(), j1962_plug_adapter()]
