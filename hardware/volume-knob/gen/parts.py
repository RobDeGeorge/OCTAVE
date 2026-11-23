"""LCSC part numbers for JLCPCB assembly. Checked against JLCPCB's parts library on 2026-11-22
(stock in comments). The cell is not an LCSC part: it is fitted by hand."""

LCSC = {
    # ICs and modules
    "ESP32-C3-MINI-1-H4X": "C41349510",  # 105 C, chip rev v1.1, Ext, 4.4k
    "BQ25173QWDRCRQ1": "C20345235",      # charger, VSON-10 3x3, Ext, 2.8k
    "TPS61299YBHR": "C20882919",         # boost, WCSP-6, Ext, 2.6k. SOT-563 version C41689719: 19
    "TPS3840PL16DBVR": "C1852124",       # cut-off: 1.6 V push-pull supervisor, Ext, 179. PL18 (1.8 V) C1852123 was the
                                         # first choice: 0 in stock. PL20 (2.0 V) C2066382: 42, trips on load sag in the
                                         # cell's flat bottom 20 %
    "TPS3840DL28DBVR": "C2066416",       # ESP32 reset: 2.8 V open-drain supervisor, Ext, 557
    "TPS22919DCKR": "C2149796",          # LED rail load switch, Ext, 61k
    "USBLC6-2SC6": "C7519",              # ST, Ext, 42k
    "WS2812B-2020": "C52917434",         # WS2812B-2020-V6 (3.3 V, 1 uA dark), Ext, 707k
    "EC11J1525402": "C209762",           # Alps reflow encoder with switch, Ext, 1.4k
    "USBC": "C165948",                   # HRO TYPE-C-31-M-12, Ext, 482k
    "L1u": "C98348",                     # Sunlord WPN252012H1R0MT 1 uH 2520, Ext, 20k
    "F3A": "C99558",                     # Littelfuse 0467003.NRHF 3 A 0603 fast fuse, 21 mohm, Ext, 20k
    # passives, 0402 unless noted
    "C100n": "C1525", "C1u": "C52923", "C10n": "C15195", "C220p": "C1530",     # 220 pF 50 V X7R, Basic
    "C10u": "C19702",                    # 10 uF 10 V 0603
    "C22u": "C45783",                    # 22 uF 25 V 0805
    "C4.7u": "C1779",                    # 4.7 uF 25 V 0805 (charger input: 1 uF needed at 5 V bias)
    "R22": "C25092", "R330": "C25104", "R1k": "C11702", "R2k": "C4109",
    "R5.1k": "C25905", "R10k": "C25744", "R100k": "C25741", "R200k": "C25764", "R1M": "C26083",
    "R33k": "C25779", "R470k": "C25790",
    "R150k": "C25755",
}
