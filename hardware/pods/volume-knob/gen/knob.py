"""OCTAVE wireless volume knob, Rev 0: the netlist.

Run build.py to regenerate the schematic, the board and the fab outputs.
Design notes: ../README.md.
"""

from design import Design, Part
from parts import LCSC

d = Design("volume-knob")

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"

N_LEDS = 12

_n = {}


def _ref(prefix):
    _n[prefix] = _n.get(prefix, 0) + 1
    return f"{prefix}{_n[prefix]}"


def R(block, value, a, b, note=""):
    return d.add(block, Part(_ref("R"), "Device:R", value, R0402, {"1": a, "2": b},
                             lcsc=LCSC["R" + value], note=note))


def C(block, value, a, b="GND", fp=C0402):
    return d.add(block, Part(_ref("C"), "Device:C", value, fp, {"1": a, "2": b}, lcsc=LCSC["C" + value]))


def U(block, lib_id, value, fp, nets, lcsc_key=None, ref="U", **kw):
    return d.add(block, Part(_ref(ref), lib_id, value, fp, nets, lcsc=LCSC.get(lcsc_key or value, ""), **kw))


def TP(block, net, fp="TestPoint:TestPoint_Pad_D1.0mm"):
    """Test point: a bare 1 mm pad on the back for a probe."""
    return d.add(block, Part(_ref("TP"), "Connector:TestPoint", net, fp, {"1": net}))


# ---------------------------------------------------------------- USB-C and the charger
chg = d.block("charge", "USB-C in, 2.40 V / 150 mA charger, the cell")
chg.notes = [
    "Cell: Nichicon SLB12400H131 lithium titanate, 2.1 V nominal, 2.5 V maximum charge, 1.5 V discharge end, -30..80 C.",
    "Charge voltage 0.8 V x (1 + 200k / 100k) = 2.40 V (2.34..2.46 V with 1 % parts): under the cell's 2.5 V at every tolerance.",
    "The feedback divider returns through /PG, which is open with no USB: the divider draws nothing from the cell.",
    "Charge current 300 / R_ISET: 2.0k = 150 mA (about an hour). The charger is linear: 0.45 W into a 2 V cell, more into a flat one.",
    "CHG_CE high stops charging (firmware: cell full, or too hot). GPIO7 floats at reset and the pin pulls itself low: charging on.",
    "BQ25173 never terminates by itself: it holds 2.40 V while USB is in. STAT is open drain, low above 98 % of 2.40 V.",
    "The USB ESD array's rail pin has its own 100n, not VBUS: on VBUS the D+ pull-up would lift VBUS_SENSE with no cable in.",
    "VBUS_SENSE = 0.6 x VBUS: 2.64 V at 4.4 V (a logic high), 3.3 V at 5.5 V.",
    "F1 is in series with the cell (it can give tens of amps into a short). Lift it to put a meter in series.",
]
U(chg, "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", {
    "VBUS": "VBUS", "GND": "GND", "SHIELD": "GND", "CC1": "USB_CC1", "CC2": "USB_CC2",
    "D+": "USB_DP", "D-": "USB_DM", "SBU1": None, "SBU2": None}, lcsc_key="USBC", ref="J")
R(chg, "5.1k", "USB_CC1", "GND")
R(chg, "5.1k", "USB_CC2", "GND")
U(chg, "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "USB_DP", "6": "USB_DP", "3": "USB_DM", "4": "USB_DM", "2": "GND", "5": "ESD_RAIL"})
C(chg, "100n", "ESD_RAIL")
R(chg, "22", "USB_DP", "ESP_DP")
R(chg, "22", "USB_DM", "ESP_DM")
R(chg, "100k", "VBUS", "VBUS_SENSE", note="USB present: wakes the knob")
R(chg, "150k", "VBUS_SENSE", "GND")
U(chg, "OCTAVE:BQ25173-Q1_DRC", "BQ25173QWDRCRQ1", "Package_SON:Texas_DRC0010J_ThermalVias", {
    "IN": "VBUS", "ISET": "CHG_ISET", "~{CE}": "CHG_CE", "STAT": "CHG_STAT", "~{PG}": "CHG_PG",
    "FB": "CHG_FB", "OUT": "VBAT", "GND": "GND", "EP": "GND", "NC": None})
C(chg, "4.7u", "VBUS", fp=C0805)
C(chg, "1u", "VBAT")
R(chg, "2k", "CHG_ISET", "GND", note="150 mA. 3.0k = 100 mA, 1.2k = 250 mA")
R(chg, "100k", "3V3", "CHG_STAT")
R(chg, "200k", "VBAT", "CHG_FB")
R(chg, "100k", "CHG_FB", "CHG_PG")
d.add(chg, Part(_ref("BT"), "Device:Battery_Cell", "SLB12400H1311CX",
                "OCTAVE:Battery_Nichicon_SLB12400_Horizontal", {"+": "VCELL", "-": "GND"},
                mpn="SLB12400H1311CX", note="Nichicon lithium titanate 2.1 V 130 mAh. Fit by hand"))

d.add(chg, Part(_ref("F"), "Device:Fuse", "3A", "Fuse:Fuse_0603_1608Metric", {"1": "VCELL", "2": "VBAT"},
                lcsc=LCSC["F3A"], mpn="0467003.NRHF"))

# ---------------------------------------------------------------- 3.3 V
pwr = d.block("power", "Cut-off at 1.6 V, 3.3 V boost, LED rail switch")
pwr.notes = [
    "TPS61299: 95 nA quiescent, 1.2 A input limit: about 470 mA at 3.3 V from a 1.9 V cell. Firmware caps the LED current.",
    "VSEL straight to GND = 3.3 V. Nothing higher: 3.5 V +2 % and the light-load offset is over the ESP32's 3.6 V maximum.",
    "TPS3840PL16 holds the boost off below 1.6 V and lets it back on at 1.7 V. The cell rests at 1.98 V when empty and is rated to 1.5 V:",
    "a 2.0 V part sits inside the flat bottom of its curve and trips on load sag from 10 to 30 % charge (layout review).",
    "Its supply is filtered (10k, 1u) so a transmit burst's sag does not trip it. PL18 (1.8 V, C1852123) fits the same pads: none in stock.",
    "Firmware reads VBAT before the radio starts and sleeps below 2.05 V: the cut-off is under the empty cell's rest voltage, so this is what keeps it from boot-looping.",
    "VBAT_SENSE goes straight to the ADC (0..2.5 V range): 470k in series (pin leakage of 50 nA is then 24 mV, not 50), and a dead 3V3 rail draws about 3 uA through the pin.",
    "First power-up: no cell, BOOST_EN's pad held to GND, USB in, VBAT must read 2.40 V. An open joint on the 200k feedback resistor sends VBAT to 5 V,",
    "and the boost passes anything over 3.4 V straight to the module.",
    "LED rail: TPS22919, 1.1 ms rise, discharges VLED when off. LED_EN floats at reset: the 100k keeps it off.",
]
U(pwr, "OCTAVE:TPS3840PL", "TPS3840PL16DBVR", "Package_TO_SOT_SMD:SOT-23-5", {
    "VDD": "CUT_VDD", "GND": "GND", "~{RESET}": "BOOST_EN", "~{MR}": None, "CT": None})
R(pwr, "10k", "VBAT", "CUT_VDD")
C(pwr, "1u", "CUT_VDD")
U(pwr, "OCTAVE:TPS61299_YBH", "TPS61299YBHR", "OCTAVE:TI_YBH0006_WCSP-6_0.8x1.2mm", {
    "VIN": "VBAT", "SW": "BOOST_SW", "EN": "BOOST_EN", "VSEL": "GND", "VOUT": "3V3", "GND": "GND"})
d.add(pwr, Part(_ref("L"), "Device:L", "1u", "Inductor_SMD:L_Sunlord_SWPA252012S",
                {"1": "VBAT", "2": "BOOST_SW"}, lcsc=LCSC["L1u"], mpn="WPN252012H1R0MT"))
C(pwr, "10u", "VBAT", fp=C0603)
C(pwr, "22u", "VBAT", fp=C0805)
C(pwr, "22u", "3V3", fp=C0805)
C(pwr, "22u", "3V3", fp=C0805)
C(pwr, "22u", "3V3", fp=C0805)
R(pwr, "470k", "VBAT", "VBAT_SENSE")
C(pwr, "100n", "VBAT_SENSE")
U(pwr, "OCTAVE:TPS22919", "TPS22919DCKR", "Package_TO_SOT_SMD:SOT-363_SC-70-6", {
    "IN": "3V3", "GND": "GND", "ON": "LED_EN", "NC": None, "QOD": "VLED", "OUT": "VLED"})
R(pwr, "100k", "LED_EN", "GND")
C(pwr, "10u", "VLED", fp=C0603)

# ---------------------------------------------------------------- MCU
mcu = d.block("mcu", "ESP32-C3-MINI-1-H4X")
mcu.notes = [
    "Deep-sleep wake is GPIO0..5 only: ENC_A (0), ENC_SW (3) and VBUS_SENSE (5) wake it; ENC_B (1) is read on wake.",
    "Strapping pins GPIO2, 8, 9: pulled up, nothing on them that can be low at reset. BOOT (GPIO9) low at reset = download mode.",
    "Flashing is over USB (GPIO18 D-, GPIO19 D+), no buttons. Recovery: short the BOOT pad to GND and tap EN to GND.",
    "Reset states: GPIO7 and GPIO10 float (charger on, LED rail off), GPIO6 and GPIO20 have a weak pull-up, GPIO21 prints the boot log.",
    "VBAT_SENSE on GPIO4 = ADC1_CH4, 12 dB attenuation (0..2.5 V).",
    "EN: a TPS3840DL28 holds the module in reset below 2.8 V on 3V3 (the rail rises and falls slowly with the cell).",
    "CHG_STAT is on U0RXD: check once that the ROM downloader does not mind it low while a full cell is on USB.",
    "LED_DATA has a 10k to GND: GPIO6's reset pull-up must not feed the first LED's input while VLED is off.",
]
U(mcu, "OCTAVE:ESP32-C3-MINI-1", "ESP32-C3-MINI-1-H4X", "OCTAVE:ESP32-C3-MINI-1", {
    "3V3": "3V3", "GND": "GND", "EN": "EN", "NC": None,
    "IO0": "ENC_A", "IO1": "ENC_B", "IO2": "IO2", "IO3": "ENC_SW", "IO4": "VBAT_SENSE", "IO5": "VBUS_SENSE",
    "IO6": "LED_DATA", "IO7": "CHG_CE", "IO8": "IO8", "IO9": "BOOT", "IO10": "LED_EN",
    "IO18": "ESP_DM", "IO19": "ESP_DP", "RXD0": "CHG_STAT", "TXD0": "UART_TX"})
C(mcu, "10u", "3V3", fp=C0603)
C(mcu, "100n", "3V3")
R(mcu, "10k", "3V3", "EN")
C(mcu, "100n", "EN")
U(mcu, "OCTAVE:TPS3840DL", "TPS3840DL28DBVR", "Package_TO_SOT_SMD:SOT-23-5", {
    "VDD": "3V3", "GND": "GND", "~{RESET}": "EN", "~{MR}": None, "CT": "EN_CT"})
C(mcu, "10n", "EN_CT")
R(mcu, "10k", "LED_DATA", "GND")
R(mcu, "10k", "3V3", "IO2")
R(mcu, "10k", "3V3", "IO8")
R(mcu, "10k", "3V3", "BOOT")
for _net in ("3V3", "GND", "VBAT", "VBUS", "VLED", "EN", "BOOT", "UART_TX", "IO2", "BOOST_EN", "LED_D1"):
    TP(mcu, _net)
# the expansion row: six plated holes at 2.54 mm on the rim beside the USB socket, for wires or a header.
# There is no unused GPIO: these are the three strapping pins and the boot-log pin, free once the module has started
IO_ROW = [TP(mcu, _net, "OCTAVE:IO_Hole_D1.7mm_Drill1.0mm" + ("_Square" if _net == "GND" else "")).ref
          for _net in ("GND", "3V3", "IO8", "BOOT", "IO2", "UART_TX")]

# ---------------------------------------------------------------- encoder
enc = d.block("encoder", "Alps EC11J encoder with push switch")
enc.notes = [
    "30 detents, 15 pulses: A rests closed at every second detent, B's edges fall on the detents (Alps: B is not defined at rest).",
    "A, B and the switch all wake the knob (GPIO0, 1, 3). Firmware keeps the last A and B in RTC memory and decodes the quadrature in the wake stub:",
    "whichever of A and B moved first gives the direction. Waking on A alone and reading B afterwards is a race with the detent (B switches there).",
    "1M pull-ups: a closed contact costs 3.3 uA asleep. Awake, firmware adds the internal 45k pull-ups to wet the contacts.",
    "220 pF on A and B: with the 1M an opening contact reaches a logic high in 0.3 ms (2.2 nF took 3 ms, as long as the wake itself). Debounce in firmware.",
    "Switch pull-up 33k: 100 uA through the closed switch, Alps' minimum for it. It is open at rest.",
]
U(enc, "Device:RotaryEncoder_Switch_MP", "EC11J1525402", "OCTAVE:RotaryEncoder_Alps_EC11J15_SMD", {
    "A": "ENC_A_SW", "B": "ENC_B_SW", "C": "GND", "S1": "ENC_SW_SW", "S2": "GND", "MP": "GND"}, ref="SW")
for _s in ("A", "B"):
    R(enc, "1k", f"ENC_{_s}_SW", f"ENC_{_s}")
    R(enc, "1M", "3V3", f"ENC_{_s}")
    C(enc, "220p", f"ENC_{_s}")
R(enc, "1k", "ENC_SW_SW", "ENC_SW")
R(enc, "33k", "3V3", "ENC_SW")
C(enc, "10n", "ENC_SW")

# ---------------------------------------------------------------- LED ring
led = d.block("leds", f"LED ring: {N_LEDS} x WS2812B-2020-V6")
led.notes = [
    "The V6 part only: 3.3 V supply, 1.8 V input threshold, under 1 uA dark. The older WS2812B-2020 needs 3.7 V.",
    "12 mA a colour: 432 mA for the ring at full white, more than the boost has. Firmware keeps the ring under 100 mA.",
    "Hold LED_DATA low before LED_EN goes low: a LED's input must not sit above its supply.",
]
R(led, "330", "LED_DATA", "LED_D1")
for _i in range(1, N_LEDS + 1):
    U(led, "LED:WS2812B-2020", "WS2812B-2020", "LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm", {
        "VDD": "VLED", "VSS": "GND", "DIN": f"LED_D{_i}",
        "DOUT": f"LED_D{_i + 1}" if _i < N_LEDS else None}, ref="D")
    C(led, "100n", "VLED")
