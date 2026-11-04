"""LCSC part numbers for JLCPCB assembly.

Checked against JLCPCB's parts library on 2026-11-02 (stock in comments).
Recheck the low-stock ones on the day you order: STM32G474RET6 (186 on
2026-11-02, 20 earlier the same day), AP2114H (2.9k), 5.0SMDJ26CA (4k).
"""

LCSC = {
    # ICs and modules
    "STM32G474RET6": "C521608",   # Ext, 186 in stock. Fallback STM32G474RBT6 C529413 (128 KB)
    "ESP32": "C701342",           # ESP32-WROOM-32E-N8, Ext, 25k
    "TCAN1044VDRQ1": "C1852061",  # CAN FD 8 Mbit/s with VIO, Ext, 15.5k
    "LM74700": "C2941042",        # LM74700QDBVRQ1, Ext, 6.9k
    "AO4264E": "C334232",         # 60 V N-FET for the ideal diode, Ext, 39k
    "TPS54360B": "C524806",       # 60 V 3.5 A buck, Ext, 59k
    "B560C": "C85100",            # 60 V 5 A Schottky (buck catch diode), Ext, 109k
    "L8u2": "C96978",             # Sunlord SWPA8065S8R2MT 8.2 uH, Ext, 625
    "AP2114H-3.3": "C150716",     # 1 A LDO, SOT-223 (1 GND, 2 VOUT + tab, 3 VIN), ceramic-stable, Ext, 2.9k
    "TPS22918": "C131941",        # ESP32 load switch, Ext, 55k
    "W25Q128JVSIQ": "C97521",     # Basic, 77k
    "USBLC6-2SC6": "C2687116",    # UMW, Ext, 112k
    "LM2901BIDR": "C22427890",    # TI, LM339 pinout, -40..125 C, 38 V abs max (LM2901 non-B: 36 V), Ext, 1.7k
    "LM317LIDR": "C183332",       # Ext, 95k
    # discretes
    "MMBT3904": "C20526",         # Basic
    "MMBT3906": "C2143",          # Ext
    "2N7002": "C8545",            # Basic
    "SI2309CDS": "C10493",        # Vishay 60 V P-FET SOT-23 (1 G, 2 S, 3 D), DLC_SW high side, Ext, 23k
    "BZT52C12": "C173429",        # 12 V zener SOD-123 (P-FET gate clamp), Ext, 291k
    "AO3401A": "C15127",          # Basic, P-FET (J1850 Bus+)
    "B5819W": "C8598",            # Basic, 40 V 1 A Schottky SOD-123
    "1N4148W": "C81598",          # Basic, SOD-123
    "SS34": "C8678",              # Basic, 40 V 3 A
    "5.0SMDJ26CA": "C444401",     # 5 kW bidirectional input TVS (SMC), Ext, 4k
    "BZT52C33": "C173417",        # 33 V zener SOD-123 (comparator supply clamp), Ext, 3k
    "SMF24A": "C123810",          # bus-line TVS, Ext
    "NUP2105L": "C233494",        # SZNUP2105LT1G, CAN ESD, Ext
    "PTC": "C49326266",           # 2920L200/60GR 2 A hold (~0.9 A at 85 C) 60 V, Ext, 20k
    "XTAL16M": "C13738",          # 16 MHz 3225 9 pF, Basic
    "FB600": "C1002",             # GZ1608D601TF 600R 0603, Basic
    # connectors, UI
    "MICROFIT10": "C122421",      # Molex 43045-1000 2x5 right angle, Ext
    "USBC": "C165948",            # HRO TYPE-C-31-M-12, Ext
    "SWD": "C41376037",           # 2x5 1.27 mm SMD header, Ext
    "QWIIC": "C160404",           # JST SM04B-SRSS-TB, Ext
    "SW": "C318884",              # XKB TS-1187A 5.1 mm tactile, Basic
    "LEDR": "C2286", "LEDG": "C12624", "LEDB": "C2288",
    # passives (0603 Basic unless noted)
    "C100n": "C14663", "C1u": "C15849", "C10u": "C96446", "C4.7u": "C19666",
    "C22u": "C12891",             # 22 uF 25 V 1206
    "C2.2u": "C50254",            # 2.2 uF 50 V 1206 (buck input)
    "C47u": "C96123",             # 47 uF 10 V 1206 (buck output)
    "C10u50": "C13585",           # 10 uF 50 V 1206
    "C47u50": "C2887275",         # 47 uF 50 V aluminium electrolytic 6.3x7.7 (VBAT_P bulk), Ext, 102k
    "CXTAL": "C1634",             # 10 pF C0G: crystal CL 9 pF, 10 + 10 in series + ~4 pF stray
    "C6.8n": "C1631", "C1n": "C1588", "C39p": "C1666", "C12p": "C38523", "C10p": "C1634", "C10n": "C57112",
    "R4.7k": "C23162", "R10k": "C25804", "R100k": "C25803", "R1k": "C21190",
    "R510": "C23193", "R5.1k": "C23186", "R13k": "C22797", "R510k": "C23192",
    "R57.6k": "C23082", "R330": "C23138", "R4.7k_1206": "C17936", "R10k_1206": "C17902", "R470_1206": "C4502", "R47k": "C25819",
    "R2.2k": "C4190", "R680": "C23228", "R470": "C23179", "R10.2k": "C22772", "R84.5k": "C23118",
    "R162k": "C22815", "R120": "C22787", "R240": "C23350", "R787": "C23111", "R453": "C48136",
    "R2.7k": "C13167", "R1.5k": "C22843", "R510_2512": "C5843118",  # Yageo 2 W 2512
    "R100k_0.1%": "C122538", "R10k_0.1%": "C95204",
}
