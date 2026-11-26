"""Project symbols (lib/OCTAVE.kicad_sym) for the parts KiCad's libraries don't have.

Pin numbers are the datasheets': ESP32-C3-MINI-1 datasheet v2.2 table 3-1, BQ25173-Q1 (DRC package),
TPS61299 (YBH package), TPS3840 (DBV package). The footprints are in lib/OCTAVE.pretty.
"""

from mklib import ic, write

P, I, O, B, PI, PO, OC, NC = ("passive", "input", "output", "bidirectional", "power_in", "power_out",
                              "open_collector", "no_connect")

_GND = [1, 2, 11, 14] + list(range(36, 54))
_NC = [4, 7, 9, 10, 15, 17, 24, 25, 28, 29, 32, 33, 34, 35]

SYMBOLS = [
    ic("ESP32-C3-MINI-1",
       [(3, "3V3", PI), (8, "EN", I), None,
        (12, "IO0", B), (13, "IO1", B), (5, "IO2", B), (6, "IO3", B), (18, "IO4", B), (19, "IO5", B),
        (20, "IO6", B), (21, "IO7", B), (22, "IO8", B), (23, "IO9", B), (16, "IO10", B),
        (26, "IO18", B), (27, "IO19", B), (30, "RXD0", B), (31, "TXD0", B), None,
        *[(n, "NC", NC) for n in _NC]],
       [(n, "GND", PI) for n in _GND],
       footprint="OCTAVE:ESP32-C3-MINI-1", desc="ESP32-C3 module, PCB antenna, 4 MB flash", width=25.4),
    # TI BQ25173-Q1, VSON-10 (DRC): 1 IN, 2 ISET, 3 /CE, 4 NC, 5 GND, 6 NC, 7 STAT, 8 /PG, 9 FB, 10 OUT, pad GND
    ic("BQ25173-Q1_DRC",
       [(1, "IN", PI), (3, "~{CE}", I), (2, "ISET", P), None, (4, "NC", NC), (6, "NC", NC)],
       [(10, "OUT", PO), (9, "FB", I), (8, "~{PG}", OC), (7, "STAT", OC), (5, "GND", PI), (11, "EP", PI)],
       footprint="", desc="800 mA linear charger, adjustable voltage (supercapacitor / LTO)"),
    # TI TPS61299, WCSP-6 (YBH): A1 VIN, A2 GND, B1 SW, B2 VOUT, C1 EN, C2 VSEL
    ic("TPS61299_YBH",
       [("A1", "VIN", PI), ("B1", "SW", P), ("C1", "EN", I)],
       [("B2", "VOUT", PO), ("C2", "VSEL", P), ("A2", "GND", PI)],
       footprint="OCTAVE:TI_YBH0006_WCSP-6_0.8x1.2mm", desc="95 nA quiescent boost converter, 1.2 A input limit"),
    # TI TPS3840 (DBV): 1 RESET, 2 VDD, 3 GND, 4 MR, 5 CT. PL = push-pull, active low
    ic("TPS3840PL",
       [(2, "VDD", P), (4, "~{MR}", I), (5, "CT", P)],
       [(1, "~{RESET}", O), None, (3, "GND", PI)],
       footprint="Package_TO_SOT_SMD:SOT-23-5", desc="Nanopower voltage supervisor, push-pull, active low"),
    # the open-drain version (DL): RESET needs a pull-up
    ic("TPS3840DL",
       [(2, "VDD", PI), (4, "~{MR}", I), (5, "CT", P)],
       [(1, "~{RESET}", OC), None, (3, "GND", PI)],
       footprint="Package_TO_SOT_SMD:SOT-23-5", desc="Nanopower voltage supervisor, open drain, active low"),
    # TI TPS22919 (DCK): 1 IN, 2 GND, 3 ON, 4 NC, 5 QOD, 6 OUT. QOD is passive here: it is tied to OUT
    ic("TPS22919",
       [(1, "IN", PI), (3, "ON", I), (4, "NC", NC)],
       [(6, "OUT", PO), (5, "QOD", P), (2, "GND", PI)],
       footprint="Package_TO_SOT_SMD:SOT-363_SC-70-6", desc="5.5 V 1.5 A load switch, 1.1 ms rise"),
]

if __name__ == "__main__":
    write(SYMBOLS)
