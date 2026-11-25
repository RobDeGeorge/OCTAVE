"""LCSC part numbers for JLCPCB assembly of the LoRa pod.

Checked against JLCPCB's parts library on 2026-11-23 (stock in comments). Recheck on the day you
order: RAK4630-9-SM-I (44: the one that decides whether the order can be placed), L80RE-M37 (634),
TPS7B8233QDGNRQ1 (2.5k), ASM330LHHTR (4.5k).
"""

LCSC = {
    # modules and ICs
    "RAK4630": "C33033533",       # RAK4630-9-SM-I: nRF52840 + SX1262, 902-928 MHz, IPEX sockets for LoRa and BLE,
                                  # Ext, 44. Europe: RAK4630-8-SM-I (C32315253, 253). No socket: -9-SM-NI C42388202 (66)
    "L80RE-M37": "C3029061",      # Quectel L80-R (order code L80RE-M37): GPS, 15 mm patch on top, Ext, 634
    "ASM330LHHTR": "C459826",     # ST automotive 6-axis IMU, LGA-14 2.5x3, -40..105 C, Ext, 4.5k
    "TPS7B8233": "C5220182",      # TI TPS7B8233QDGNRQ1: 40 V in, 3.3 V 300 mA, 2.7 uA quiescent, HVSSOP-8, Ext, 2.5k
    "THVD2450": "C2672621",       # TI THVD2450DR: 3.3 V half-duplex RS-485, +-70 V bus fault, 1.8 mA listening,
                                  # under 1 uA shut down, SOIC-8, -40..125 C, Ext, 23.8k (2026-11-23)
    "TPS22918": "C131941",        # TI load switch (GPS supply), SOT-23-6, Ext, 50k
    "USBLC6-2SC6": "C2687116",    # USB ESD, Ext, 161k
    # discretes
    "B5819W": "C8598",            # 40 V 1 A Schottky SOD-123 (reverse polarity; USB / cable OR), Basic
    "SMBJ24A": "C123819",         # 24 V stand-off TVS, 600 W, SMB: clamps at 38.9 V, under U1's 40 V, Ext, 47k
    "1N4148W": "C81598",          # SOD-123 (GPS backup supply), Basic
    "PTC": "C7542947",            # 1206L050/30NR: 0.5 A hold, 30 V, 1206, Ext, 113k
    # connectors, UI
    "GH4": "C189895",             # JST SM04B-GHS-TB (cable to the adapter, or to an OBD power lead), Ext, 16k
    "QWIIC": "C160404",           # JST SM04B-SRSS-TB, Ext, 55k
    "SWD": "C41376037",           # 2x5 1.27 mm SMD header, Ext, 31k
    "USBC": "C165948",            # HRO TYPE-C-31-M-12, Ext, 464k
    "DIP2": "C40735",             # KINGTEK DSHP02TSGET 2-way SMD DIP, 1.27 mm, -40..85 C, 24 V 25 mA switching, Ext, 9.9k
    "SW": "C318884",              # XKB TS-1187A 5.1 mm tactile, Basic
    "LEDR": "C2286", "LEDG": "C12624",
    # passives (0603 Basic unless noted)
    "C100n": "C14663",            # 50 V
    "C1u": "C15849", "C4.7u": "C19666", "C10u": "C96446", "C1n": "C1588",
    "C10u50": "C13585",           # 10 uF 50 V 1206 (regulator input)
    "R330": "C23138", "R1k": "C21190", "R4.7k": "C23162", "R5.1k": "C23186",
    "R100k": "C25803", "R220k": "C22961", "R1M": "C22935",
    "R120_1206": "C17909",        # 120R 1 % 0.25 W 1206 (bus terminator), Basic
}
