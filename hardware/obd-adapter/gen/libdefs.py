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
]

if __name__ == "__main__":
    write(SYMBOLS)
