"""OCTAVE OBD adapter (compact two-sided board, not yet built): the netlist.

Run build.py to regenerate the schematic, the board and the fab outputs.
Pin plan and design notes: ../README.md.
"""

from design import Design, Part
from parts import LCSC

d = Design("obd-adapter")

R0603 = "Resistor_SMD:R_0603_1608Metric"
R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"

_n = {}


def _ref(prefix):
    _n[prefix] = _n.get(prefix, 0) + 1
    return f"{prefix}{_n[prefix]}"


# 0402 for the resistors and small capacitors on logic-level nets (2026-11-24, the 80 x 55 mm board); 0603
# stays on every net that can see the battery or a bus line (HV_NETS: voltage rating, pulse power), on the
# buck's own parts (their hand-laid copper, and 13k / 160k / 68k are no-fee parts only in 0603), on the
# 0.1 % divider, and on 4.7u / 10u (a 0402 of those keeps too little of its value at 3.3-5 V).
HV_NETS = {"VBAT_IN", "VBAT_F", "VBAT_P", "IDEAL_GATE", "IDEAL_VCAP", "DLC_SW", "DLC_SW_G", "DLC_SW_D", "CMP_VCC",
           "KLINE", "LLINE", "KLINE_PU", "LLINE_PU", "K_IN", "K_REF", "J1850_P", "J1850_N", "J1850_PLOAD",
           "J1850_NPU", "J1850_PG", "VJ1850", "VJ_ADJ", "VJ_N1", "VPW_IN", "PWM_INP", "PWM_INN", "VJ_IN", "VJ_B",
           "POD_F", "POD_VIN", "CAN1_H", "CAN1_L", "CAN2_H", "CAN2_L", "CAN1_TERM", "CAN2_TERM",
           "RS485_A", "RS485_B", "PANIC_IN", "VBUS",
           "BUCK_RT", "BUCK_FB", "BUCK_COMP", "BUCK_COMP_RC", "BUCK_BOOT", "BUCK_SW"}
SMALL_R = {"10k", "1k", "330", "5.1k", "4.7k", "100k", "2.2k", "18k", "47k", "470"}
SMALL_C = {"100n", "1u", "10p", "1n"}


def _small(value, a, b, keys):
    return value in keys and a not in HV_NETS and b not in HV_NETS


def R(block, value, a, b, fp=None, lcsc_key=None, dnp=False, note=""):
    small = fp is None and lcsc_key is None and _small(value, a, b, SMALL_R)
    key = lcsc_key or ("R" + value + ("_0402" if small else ""))
    return d.add(block, Part(_ref("R"), "Device:R", value, fp or (R0402 if small else R0603), {"1": a, "2": b},
                             lcsc=LCSC.get(key, ""), dnp=dnp, note=note))


def C(block, value, a, b="GND", fp=None, lcsc_key=None, dnp=False):
    small = fp is None and _small(value, a, b, SMALL_C)
    key = {"CXTAL": "C10p"}.get(lcsc_key, lcsc_key) if small else lcsc_key
    key = (key or ("C" + value)) + ("_0402" if small else "")
    return d.add(block, Part(_ref("C"), "Device:C", value, fp or (C0402 if small else C0603), {"1": a, "2": b},
                             lcsc=LCSC.get(key, ""), dnp=dnp))


def U(block, lib_id, value, fp, nets, lcsc_key=None, ref="U", **kw):
    return d.add(block, Part(_ref(ref), lib_id, value, fp, nets, lcsc=LCSC.get(lcsc_key or value, ""), **kw))


def TP(block, net):
    """Test point: a bare 1.5 mm pad for a probe or a hook."""
    return d.add(block, Part(_ref("TP"), "Connector:TestPoint", net, "TestPoint:TestPoint_Pad_D1.5mm",
                             {"1": net}))


# ---------------------------------------------------------------- MCU
mcu = d.block("mcu", "STM32G474RET6: vehicle-side real-time engine")
mcu.notes = [
    "FDCAN1 is not used: its pins clash with USB (PA11/PA12) and BOOT0 (PB8).",
    "HS-CAN (J1962 6/14) = FDCAN2 PB12/PB13, nets CAN1_*. MS-CAN (3/11) = FDCAN3 PB3/PB4, nets CAN2_*.",
    "J1850 on TIM3: CH1 VPW RX, CH2 PWM RX (input capture), CH3/CH4 TX Bus+/Bus- (output compare).",
    "K-line on LPUART1 (PC0 RX / PC1 TX, TXINV: the NPN inverts); 5-baud init bit-bangs PC1. ESP32 on USART2 + RTS/CTS.",
    "PC3 switches DLC_SW (battery to the J1850 supply, comparators, K/L pull-ups); PB2 picks 7.24 V VPW / 4.79 V PWM.",
    "PB6 (ESP_PWR_EN) has the UCPD dead-battery pull-down until firmware sets PWR_CR3.UCPD_DBDIS: ESP32 off at reset.",
    "PB4's (CAN2_TXD, where low is dominant) is kept off by R18, 10k from PA10 to GND (USB page).",
    "DLC_SW_EN is on PC3; PC10 was avoided because the system bootloader may drive USART3 TX there.",
    "Firmware turns on the internal pull-ups of PA15 (USER button) and PC0 (KLINE_RX, an open-collector output).",
    "VDDA / VREF+ are on 3V3 (100n + 1u at the pins): the ADC only reads the battery divider.",
    "Names: _M = the STM32 side of a 1k series resistor (ESP32 page); ESP_RTS / ESP_TX_MCU are named from the ESP32's side;",
    "U2S = USB-to-serial (CH340C). EXP_PA5 / EXP_PC10 / EXP_PD2 also work the pod bus (RS-485 DE, /RE, POD_EN: USB page).",
]
U(mcu, "MCU_ST_STM32G4:STM32G474RETx", "STM32G474RET6", "Package_QFP:LQFP-64_10x10mm_P0.5mm", {
    "VBAT": "3V3", "VDD": "3V3", "VSS": "GND", "VSSA": "GND", "VDDA": "3V3", "VREF+": "3V3",
    "PG10": "NRST", "PB8": "BOOT0",
    "PF0": "HSE_IN", "PF1": "HSE_OUT", "PC14": "ACC_INT1", "PC15": "ACC_INT2",
    "PA0": "ESP_RTS", "PA1": "ESP_CTS_M", "PA2": "MCU_TX_ESP_M", "PA3": "ESP_TX_MCU",
    "PA4": "VBAT_SENSE", "PA5": "EXP_PA5",
    "PA6": "QSPI_IO3", "PA7": "QSPI_IO2", "PB0": "QSPI_IO1", "PB1": "QSPI_IO0",
    "PB10": "QSPI_CLK", "PB11": "QSPI_NCS",
    "PA8": "I2C_SDA", "PA9": "I2C_SCL", "PA10": "EXP_PA10",
    "PA11": "MCU_DM", "PA12": "MCU_DP", "PA13": "SWDIO", "PA14": "SWCLK",
    "PA15": "BTN_USER", "PB2": "J1850_VSEL",
    "PB3": "CAN2_RXD", "PB4": "CAN2_TXD",
    "PB5": "ESP_IO0_M", "PB6": "ESP_PWR_EN", "PB7": "ESP_EN_M",
    "PB9": "LED_R", "PC11": "LED_G", "PC12": "LED_B",
    "PB12": "CAN1_RXD", "PB13": "CAN1_TXD", "PB14": "CAN1_STB", "PB15": "CAN2_STB",
    "PC0": "KLINE_RX", "PC1": "KLINE_TX", "PC2": "LLINE_TX", "PC3": "DLC_SW_EN",
    "PC4": "EXP_TX", "PC5": "EXP_RX",
    "PC6": "VPW_RX", "PC7": "PWM_RX", "PC8": "J1850_TXP", "PC9": "J1850_TXN",
    "PC10": "EXP_PC10", "PC13": "PANIC_BTN", "PD2": "EXP_PD2",
}, lcsc_key="STM32G474RET6")
for _ in range(4):
    C(mcu, "100n", "3V3")
C(mcu, "4.7u", "3V3")
# VDDA / VREF+ (pins 29 / 28) are on 3V3 with their own 100n + 1u at the pins. FB1 (600R bead) and C8
# (10n) between 3V3 and a separate VDDA net are gone: the only analogue input is the battery divider on
# PA4 (10k source, 100n at the pin, read against the LDO's own +-1.5 %), which a bead does not improve.
_ref("FB")
C(mcu, "100n", "3V3")
C(mcu, "1u", "3V3")
_ref("C")   # C8
C(mcu, "100n", "NRST")
R(mcu, "10k", "BOOT0", "GND")
d.add(mcu, Part(_ref("Y"), "Device:Crystal_GND24", "16MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
                {"1": "HSE_OUT", "3": "HSE_IN", "2": "GND", "4": "GND"}, lcsc=LCSC.get("XTAL16M")))
# a crystal has no polarity: pad 3 (the far one, top left) is on PF0 and pad 1 on PF1, so that the two tracks
# from U1 pins 5 / 6 do not cross (REVIEW round 8: HSE_OUT ran 18 mm through two vias under the STM32)
C(mcu, "10p", "HSE_OUT", lcsc_key="CXTAL")
C(mcu, "10p", "HSE_IN", lcsc_key="CXTAL")

# ---------------------------------------------------------------- debug, buttons, LEDs
ui = d.block("ui", "Debug, buttons, LEDs")
ui.notes = ["SWD: 2x5 1.27 mm Cortex debug header (ST-Link V3 / J-Link).",
            "USER button: PA15 to GND, internal pull-up (no resistor on the board)."]
d.add(ui, Part(_ref("J"), "Connector:Conn_ARM_JTAG_SWD_10", "SWD",
               "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical_SMD",
               {"1": "3V3", "2": "SWDIO", "3": "GND", "4": "SWCLK", "5": "GND", "6": None, "7": None,
                "8": None, "9": "GND", "10": "NRST"}, lcsc=LCSC.get("SWD")))
for name, net, other in (("RESET", "NRST", "GND"), (None, None, None), ("USER", "BTN_USER", "GND")):
    ref = _ref("SW")
    if name is None:        # SW2 was the BOOT0 button: BOOT0 is switch 4 of the CONFIG DIP switch now
        continue
    d.add(ui, Part(ref, "Switch:SW_Push", name, "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A",
                   {"1": net, "2": other}, lcsc=LCSC.get("SW")))
_ref("R")   # R2 was the USER button's 10k pull-up: PA15 has one (on from reset, as JTDI; firmware keeps it on)
# One RGB LED (D1, XL-1615RGBC: 1 red, 2 common anode, 3 green, 4 blue) in place of Rev 0's three:
# the anode on 3V3, each colour sunk by its pin through its resistor, so the LED pins are ACTIVE LOW.
# D2 / D3 (the green and blue LEDs) are burned.
for color in "RGB":
    R(ui, "1k" if color == "R" else "330", f"LED_{color}", f"LED_{color}_K")
d.add(ui, Part(_ref("D"), "Device:LED_RAGB", "RGB", "OCTAVE:LED_RGB_1615",
               {"1": "LED_R_K", "2": "3V3", "3": "LED_G_K", "4": "LED_B_K"}, lcsc=LCSC["LEDRGB"]))
_ref("D")
_ref("D")

# ---------------------------------------------------------------- USB-C
usb = d.block("usb", "USB-C behind a hub: the STM32 (DFU, CDC serial to the Orange Pi) and the ESP32's serial port; bench power")
usb.notes = ["5.1k on CC1/CC2 = sink. VBUS feeds the 5 V rail through a Schottky (bench power without a car).",
             "U18 CH334P (USB 2.0 hub, 4 ports, 2 used): port 3 is the STM32 (PA11 / PA12), port 2 the CH340C on the",
             "ESP32's UART0. Both show up on the computer at once, with no firmware on either chip.",
             "U2's clamp rail (pin 5) is on 3V3, not VBUS, as the hub's datasheet asks: on VBUS the hub's D+ pull-up would feed",
             "a cable-less VBUS through the clamp.",
             "The hub runs from 3V3 (V5 and VDD33 both on it, as its datasheet advises for a hub on the board), so it is",
             "powered whenever the STM32 and the CH340C are: their D+ pull-ups never drive an unpowered chip. With no",
             "cable in it sleeps: 0.12 mA typical, 0.4 mA at most, added to what the board draws while parked.",
             "RESET# (about 25k pull-up inside) and PGANG (pull-up inside: ganged over-current mode) are left open.",
             "Y2 12 MHz: the chip has load capacitors inside. (Crystal-free is an order option of the CH334, not the default.)"]
d.add(usb, Part(_ref("J"), "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
                "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
                {"GND": "GND", "VBUS": "VBUS", "CC1": "USB_CC1", "CC2": "USB_CC2", "D+": "USB_DP",
                 "D-": "USB_DM", "SBU1": None, "SBU2": None, "SHIELD": "GND"}, lcsc=LCSC.get("USBC")))
R(usb, "5.1k", "USB_CC1", "GND")
R(usb, "5.1k", "USB_CC2", "GND")
# Pin 5 (the clamp rail) is on 3V3, not VBUS, as the CH334 datasheet (6.5) asks: on VBUS, the hub's
# 1.5k D+ pull-up fed the cable-less VBUS through the array's diode: 1 mA while parked, and enough on the
# ESP32 load switch's ON pin to turn it on in a warm car (review 2026-11-24)
U(usb, "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "USB_DP", "6": "USB_DP", "3": "USB_DM", "4": "USB_DM", "2": "GND", "5": "3V3"})
C(usb, "10u", "VBUS")

# ---------------------------------------------------------------- QSPI flash
fl = d.block("flash", "16 MB QSPI NOR: store-and-forward buffer")
U(fl, "Memory_Flash:W25Q128JVS", "W25Q128JVSIQ", "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm",
  {"1": "QSPI_NCS", "2": "QSPI_IO1", "3": "QSPI_IO2", "4": "GND", "5": "QSPI_IO0", "6": "QSPI_CLK",
   "7": "QSPI_IO3", "8": "3V3"})
R(fl, "10k", "3V3", "QSPI_NCS")
C(fl, "100n", "3V3")

# ---------------------------------------------------------------- ESP32
esp = d.block("esp32", "ESP32-WROOM-32E: Bluetooth Classic SPP + BLE (FFE0/FFE1) + Wi-Fi bridge")
esp.notes = [
    "STM32 link: ESP32 UART2 on IO16 (RX) / IO17 (TX), CTS IO18 (from PA1 RTS), RTS IO19 (to PA0 CTS), up to 3 Mbaud.",
    "UART0 goes to the CH340C on the USB hub's port 2 (auto-reset); the STM32 can reset (PB7) and strap IO0 (PB5) itself.",
    "Power-gated by a TPS22918 on PB6 (off at reset), placed at the module's 3V3 pin with its caps.",
    "CONFIG switch 1 (ESP_ON) powers the ESP32 without STM32 firmware; plugging in the USB-C does too.",
    "1k in series with PA1/PA2/PB5/PB7 limits back-powering through the ESP32's I/O while it is off;",
    "firmware still keeps them low or input until ESP_PWR_EN is on, and drives EN open-drain.",
]
# microSD in SPI mode on pins that are not boot straps except IO5, which only needs to be high at reset
# (its internal pull-up; the card doesn't drive CLK); in module-pin order along the side facing the
# card, matching the socket's contact order, so the lines don't cross
SD_PINS = {"IO4": "MISO", "IO5": "CLK", "IO21": "MOSI", "IO22": "CS", "IO23": "CD"}
ESP_IO = ("IO14", "IO27", "IO26", "IO25", "IO33", "IO32", "IO35", "IO34")   # module pins 13..6, the side facing J8
U(esp, "RF_Module:ESP32-WROOM-32E", "ESP32-WROOM-32E-N8", "RF_Module:ESP32-WROOM-32E", {
    "GND": "GND", "VDD": "3V3_ESP", "EN": "ESP_EN", "IO0": "ESP_IO0",
    "IO16": "MCU_TX_ESP", "IO17": "ESP_TX_MCU", "IO18": "ESP_CTS", "IO19": "ESP_RTS",
    "RXD0/IO3": "ESP_RXD0", "TXD0/IO1": "ESP_TXD0",
    **{n: "ESP_" + n for n in ESP_IO},
    **{n: None for n in ("SENSOR_VP", "SENSOR_VN", "IO12", "IO13", "NC", "IO15", "IO2")},
    **{n: "ESP_SD_" + f for n, f in SD_PINS.items()},
}, lcsc_key="ESP32")
C(esp, "22u", "3V3_ESP", fp=C1206)
C(esp, "100n", "3V3_ESP")
R(esp, "10k", "3V3_ESP", "ESP_EN")
C(esp, "1u", "ESP_EN")
_ref("R")   # R10 was a 10k pull-up on IO0: the pin has its own (45k, on at reset); BOOT (SW5) / Q11 / PB5 still pull it low
for mcu_net, esp_net in (("ESP_CTS_M", "ESP_CTS"), ("MCU_TX_ESP_M", "MCU_TX_ESP"),
                         ("ESP_IO0_M", "ESP_IO0"), ("ESP_EN_M", "ESP_EN")):
    R(esp, "1k", mcu_net, esp_net)
U(esp, "OCTAVE:TPS22918", "TPS22918", "Package_TO_SOT_SMD:SOT-23-6",
  {"VIN": "3V3", "ON": "ESP_PWR_ON", "CT": "ESP_PWR_CT", "VOUT": "3V3_ESP", "QOD": "3V3_ESP", "GND": "GND"})
R(esp, "100k", "ESP_PWR_ON", "GND")
C(esp, "1n", "ESP_PWR_CT", lcsc_key="C1n")
C(esp, "1u", "3V3")
_ref("J")   # J3 was the ESP_PROG header: the CH340C on the USB hub does its job; the number stays free
# ---------------------------------------------------------------- expansion
ex = d.block("exp", "Expansion: Qwiic I2C + header (sensor pod, temp probes, logic analyzer)")
d.add(ex, Part(_ref("J"), "Connector_Generic:Conn_01x04", "Qwiic",
               "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
               {"1": "GND", "2": "3V3", "3": "I2C_SDA", "4": "I2C_SCL"}, lcsc=LCSC.get("QWIIC")))
R(ex, "4.7k", "3V3", "I2C_SDA")
R(ex, "4.7k", "3V3", "I2C_SCL")
R(ex, "10k", "EXP_PA10", "GND")  # bootloader pulls PA10 up, which turns on PB4's UCPD pull-down (CAN2 TXD dominant)
# Footprint only (DNP): solder a 2x7 header in if the prototype needs it
d.add(ex, Part(_ref("J"), "Connector_Generic:Conn_02x07_Odd_Even", "EXP",
               "Connector_PinHeader_2.54mm:PinHeader_2x07_P2.54mm_Vertical",
               {"1": "3V3", "2": "5V", "3": "GND", "4": "GND", "5": "EXP_TX", "6": "EXP_RX",
                "7": "EXP_PA5", "8": "EXP_PA10", "9": "PANIC_BTN", "10": "EXP_PD2",
                "11": "I2C_SDA", "12": "I2C_SCL", "13": "EXP_PC10", "14": "GND"}, dnp=True))

# ---------------------------------------------------------------- power
pw = d.block("power", "Power: J1962 pin 16 -> PTC -> TVS -> ideal diode -> 5.3 V buck; USB OR'd in; 3.3 V LDO")
pw.notes = [
    "5.0SMDJ33CA (5 kW) is bidirectional on purpose: a unidirectional TVS would short a reversed battery through the PTC.",
    "VBAT_IN = raw pin 16, VBAT_F = after the fuse, VBAT_P = after the ideal diode (reverse-protected). VBAT_SENSE = VBAT_F / 11.",
    "Everything on VBAT_P must survive the TVS clamp, 47 V at 25 C and about 50 V hot at worst: LM74700 65 V,",
    "AO4264E, TPS54360B, Q2 and F2 60 V, caps 50 V (no margin), the pod switch U19 (USB page) 48 V for 0.4 s (1-2 V short",
    "in the worst hot pulse: accepted). The J1850 regulator U11 has its own 30 V clamp (Q15, J1850 page).",
    "TPS54360B: datasheet 600 kHz design (Fig. 32) with FB 68k/12k = 5.33 V, so 5V is ~4.9 V after the SS34 OR.",
    "EN left open (internal pull-up): the internal UVLO (~4.3 V) keeps the buck running through a 6 V cold crank.",
    "5V_BUCK (not the OR'd 5V) feeds everything on the vehicle side: CAN VCC, the VPW reference, the Bus- pull-up.",
]
d.add(pw, Part(_ref("F"), "Device:Polyfuse", "2A 60V", "Fuse:Fuse_2920_7451Metric",
               {"1": "VBAT_IN", "2": "VBAT_F"}, lcsc=LCSC["PTC"]))
# 33 V stand-off (it was 26 V): a 26CA breaks down at 28.9-31.9 V and so conducts through the whole of a
# clamped load dump (35 V for up to 0.4 s, ISO 16750-2 test B), tens of joules into a part rated about 7.
# The 33CA starts at 36.7 V; it clamps higher: about 42 V at 10 A and 46 V at 43 A at worst.
d.add(pw, Part(_ref("D"), "Device:D_TVS", "5.0SMDJ33CA", "Diode_SMD:D_SMC",
               {"A1": "VBAT_F", "A2": "GND"}, lcsc=LCSC["5.0SMDJ33CA"]))
# The ideal diode stays (REVIEW 54): a Schottky here (tried on 2026-11-09, simulated in
# verify/sim/sim_input_d29.py) saves three parts and 80 uA parked, but it leaks with the battery reversed
# (VBAT_P to -0.7 V at 100 C, the buck's VIN is rated to -0.3 V) and costs 0.3 V of cranking headroom.
C(pw, "100n", "VBAT_F")  # LM74700 input cap (datasheet: >= 22 nF required)
U(pw, "Power_Management:LM74700", "LM74700QDBVRQ1", "Package_TO_SOT_SMD:SOT-23-6",
  {"ANODE": "VBAT_F", "CATHODE": "VBAT_P", "GATE": "IDEAL_GATE", "VCAP": "IDEAL_VCAP",
   "EN": "VBAT_F", "GND": "GND"}, lcsc_key="LM74700")
C(pw, "100n", "IDEAL_VCAP", "VBAT_F")
U(pw, "OCTAVE:NMOS_SO8", "AO4264E", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"G": "IDEAL_GATE", "S": "VBAT_F", "D": "VBAT_P"}, lcsc_key="AO4264E", ref="Q")
C(pw, "10u", "VBAT_P", fp=C1206, lcsc_key="C10u50")
d.add(pw, Part(_ref("C"), "Device:C_Polarized", "47u 50V", "Capacitor_SMD:CP_Elec_6.3x7.7",
               {"1": "VBAT_P", "2": "GND"}, lcsc=LCSC["C47u50"],
               note="bulk: rides through ISO 7637 pulse 1 / hot-plug ringing"))
C(pw, "100n", "VBAT_P")
# 5 V buck
U(pw, "Regulator_Switching:TPS54360DDA", "TPS54360BDDAR",
  "Package_SO:TI_SO-PowerPAD-8_ThermalVias",
  {"VIN": "VBAT_P", "EN": None, "RT/CLK": "BUCK_RT", "FB": "BUCK_FB", "COMP": "BUCK_COMP",
   "GND": "GND", "GNDPAD": "GND", "BOOT": "BUCK_BOOT", "SW": "BUCK_SW"}, lcsc_key="TPS54360B")
C(pw, "2.2u", "VBAT_P", fp=C1206)
C(pw, "2.2u", "VBAT_P", fp=C1206)
# RT, the FB divider and the COMP pole capacitor are values from JLCPCB's no-fee libraries (REVIEW round 8):
# they were 162k, 57.6k / 10.2k and 39p (597 kHz, 5.32 V, pole at 314 kHz) and give 605 kHz, 5.33 V, 260 kHz
R(pw, "160k", "BUCK_RT", "GND")
C(pw, "100n", "BUCK_BOOT", "BUCK_SW")
d.add(pw, Part(_ref("L"), "Device:L", "8.2u", "Inductor_SMD:L_Changjiang_FNR8065S",
               {"1": "BUCK_SW", "2": "5V_BUCK"}, lcsc=LCSC["L8u2"],
               note="Sunlord SWPA8065S8R2MT; FNR8065S footprint is the same 8x8 land, check before fab"))
d.add(pw, Part(_ref("D"), "Device:D_Schottky", "B560C", "Diode_SMD:D_SMC",
               {"K": "BUCK_SW", "A": "GND"}, lcsc=LCSC["B560C"]))
R(pw, "68k", "5V_BUCK", "BUCK_FB")
R(pw, "12k", "BUCK_FB", "GND")
R(pw, "13k", "BUCK_COMP", "BUCK_COMP_RC")
C(pw, "6.8n", "BUCK_COMP_RC")
C(pw, "47p", "BUCK_COMP")
for _ in range(3):  # 47 uF 10 V 1206 is ~15-20 uF at 5 V; three restore TI's ~58 uF design
    C(pw, "47u", "5V_BUCK", fp=C1206)
# 5 V diode-OR: car or USB
d.add(pw, Part(_ref("D"), "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA",
               {"A": "5V_BUCK", "K": "5V"}, lcsc=LCSC["SS34"]))
d.add(pw, Part(_ref("D"), "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA",
               {"A": "VBUS", "K": "5V"}, lcsc=LCSC["SS34"]))
C(pw, "10u", "5V")
# 3.3 V
# AP2114H-3.3 (SOT-223: 1 GND, 2 VOUT + tab, 3 VIN; same pinout as the AMS1117 symbol). 1 A, ceramic-stable,
# 0.45 W worst case (ESP32 Wi-Fi) in SOT-223 instead of the AP2112K's SOT-23-5 at 184 C/W.
U(pw, "Regulator_Linear:AMS1117-3.3", "AP2114H-3.3", "Package_TO_SOT_SMD:SOT-223-3_TabPin2",
  {"VI": "5V", "GND": "GND", "VO": "3V3"}, lcsc_key="AP2114H-3.3")
C(pw, "10u", "5V")
C(pw, "10u", "3V3")
# battery voltage (ATRV): 100k / 10k 0.1 % -> 36 V full scale
R(pw, "100k 0.1%", "VBAT_F", "VBAT_SENSE", lcsc_key="R100k_0.1%")
R(pw, "10k 0.1%", "VBAT_SENSE", "GND", lcsc_key="R10k_0.1%")
C(pw, "100n", "VBAT_SENSE")
for n in ("VBAT_P", "5V", "3V3", "GND"):
    TP(pw, n)

# ---------------------------------------------------------------- car connector
car = d.block("car", "Car connector: SAE J1962 (OBD-II) male plug on an adapter board across the left edge")
car.notes = [
    "The plug sits on the adapter board (obd-adapter-plug); the main board's tab goes through its slot and",
    "the 16 edge pads (9-16 on top, 1-8 underneath) are soldered to it. The adapter's 9-16 row (the plug's",
    "latch side) goes on the main board's top. Pins 4 (chassis) and 5 (signal) ground both go to GND.",
    "1, 8, 9, 12, 13 are not used (soldered for strength). Bench: an OBD-II female breakout or extension.",
]
d.add(car, Part(_ref("J"), "Connector_Generic:Conn_02x08_Top_Bottom", "OBD-II plug (J1962 male)",
                "OCTAVE:J1962_Edge_Joint",
                {"16": "VBAT_IN", "4": "GND", "5": "GND", "2": "J1850_P", "10": "J1850_N", "7": "KLINE",
                 "15": "LLINE", "6": "CAN1_H", "14": "CAN1_L", "3": "CAN2_H", "11": "CAN2_L",
                 "1": None, "8": None, "9": None, "12": None, "13": None},
                note="Hand-fit: Jiatel CR-PG001 J1962 male plug on the obd-adapter-plug board, soldered across the board edge"))

# ---------------------------------------------------------------- CAN
can = d.block("can", "CAN FD: HS-CAN (J1962 6/14) and MS-CAN (3/11), TCAN1044V")
can.notes = [
    "STB (pin 8, drawn S) pulled high: transceivers sit in standby (receive-only wake) until firmware drives STB low.",
    "VCC from 5V_BUCK: CAN needs car / 12 V bench power (on USB alone 5V_BUCK is off).",
    "No termination by default (the car has its own 2x120R). CONFIG switch 2 / 3 adds it, on a bench bus only.",
]
for i, (h, l) in ((1, ("CAN1_H", "CAN1_L")), (2, ("CAN2_H", "CAN2_L"))):
    U(can, "Interface_CAN_LIN:TJA1051T-3", "TCAN1044VDRQ1", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
      {"TXD": f"CAN{i}_TXD", "RXD": f"CAN{i}_RXD", "GND": "GND", "VCC": "5V_BUCK", "VIO": "3V3",
       "CANH": h, "CANL": l, "S": f"CAN{i}_STB"}, lcsc_key="TCAN1044VDRQ1")
    C(can, "100n", "5V_BUCK")
    C(can, "100n", "3V3")
    R(can, "4.7k", "3V3", f"CAN{i}_STB")  # 4.7k: stays standby against the bootloader's PB15 pull-down
    U(can, "Power_Protection:NUP2105L", "SZNUP2105L", SOT23, {"1": h, "2": l, "3": "GND"},
      lcsc_key="NUP2105L", ref="D")
    R(can, "120", h, f"CAN{i}_TERM", lcsc_key="R120")     # to CAN_L through CONFIG switch 1 + i

# ---------------------------------------------------------------- DLC_SW
sw = d.block("dlcsw", "DLC_SW: switched battery for the legacy-bus front ends")
sw.notes = [
    "Off in sleep: the J1850 supply, the LM2901 and the K/L 510R pull-ups. The STN11xx reference design notes that pull-ups left on",
    "the bus draw current and can disturb it (engine stall). SI2309CDS 60 V P-FET high side, 12 V zener gate clamp,",
    "2N7002 level shift: Vgs = -7.4 V at 9 V battery, clamped at -12 V up to the TVS clamp (47-50 V).",
]
d.add(sw, Part(_ref("Q"), "Transistor_FET:Q_PMOS_GSD", "SI2309CDS", SOT23,
               {"G": "DLC_SW_G", "S": "VBAT_P", "D": "DLC_SW"}, lcsc=LCSC["SI2309CDS"]))
R(sw, "47k", "VBAT_P", "DLC_SW_G")
d.add(sw, Part(_ref("D"), "Device:D_Zener", "BZT52C12", "Diode_SMD:D_SOD-123",
               {"K": "VBAT_P", "A": "DLC_SW_G"}, lcsc=LCSC["BZT52C12"]))
R(sw, "10k", "DLC_SW_G", "DLC_SW_D")  # 0603 (battery side): 0.09 W for the 0.4 s of a 42 V load dump, 26 mW at a 28 V jump start
d.add(sw, Part(_ref("Q"), "Transistor_FET:2N7002", "2N7002", SOT23,
               {"G": "DLC_SW_EN", "S": "GND", "D": "DLC_SW_D"}, lcsc=LCSC["2N7002"]))
R(sw, "100k", "DLC_SW_EN", "GND")
C(sw, "1u", "DLC_SW")
TP(sw, "DLC_SW")

# ---------------------------------------------------------------- J1850
jb = d.block("j1850", "SAE J1850 VPW (Bus+, J1962 pin 2) and PWM (Bus+ / Bus-, pins 2 / 10): STN-style discrete")
jb.notes = [
    "Supply: LM317L 240/680/470R = 7.24 V (VPW); J1850_VSEL high shorts the 470R = 4.79 V (PWM).",
    "Simulated over LM317L tolerance: Bus+ 6.42-7.54 V (VPW, spec 6.25-8) and 4.26-5.00 V (PWM, 3.80-5.25 assumed),",
    "with the plug above about 9 V (10.8 V worst case, cold): below that the Q15 follower's drop takes Bus+ under 6.25 V.",
    "Bus+: 2N7002 -> AO3401A -> Schottky; 10k + Schottky bus load. Bus-: MMBT3904 sink, 10k + Schottky pull-up to 5V_BUCK.",
    "RX: LM2901B on CMP_VCC (DLC_SW via 1k, 33 V zener; 38 V abs max). VPW: Bus+ vs 5V_BUCK x 47/65 = 3.86 V, active high.",
    "PWM: Bus+ vs Bus-. Q7 base has 2.2k to GND: the bootloader's PC9 pull-up must not turn it on (Bus- low).",
    "Input clamps are 1N4148W (low leakage): Schottkys leak enough when hot to shift the thresholds behind 10k.",
    "No wave shaping (the published ELM / STN reference designs have none); a later option is an RC on Q6's gate.",
]
# U11's input is VJ_IN: DLC_SW through an emitter follower clamped at 30 V (Q15, D31, R80, at the end of
# this file), as the LM317L is rated 35 to 45 V input-to-output depending on the die and the TVS lets 47 V by
U(jb, "Regulator_Linear:LM317L_SO8", "LM317LIDR", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"VI": "VJ_IN", "VO": "VJ1850", "ADJ": "VJ_ADJ", "NC": None}, lcsc_key="LM317LIDR")
C(jb, "100n", "VJ_IN", lcsc_key="C100n")
C(jb, "1u", "VJ1850")
R(jb, "240", "VJ1850", "VJ_ADJ")
R(jb, "680", "VJ_ADJ", "VJ_N1")
R(jb, "470", "VJ_N1", "GND")
d.add(jb, Part(_ref("Q"), "Transistor_FET:2N7002", "2N7002", SOT23,
               {"G": "J1850_VSEL", "S": "GND", "D": "VJ_N1"}, lcsc=LCSC["2N7002"]))
R(jb, "10k", "J1850_VSEL", "GND")
# Bus+ driver
d.add(jb, Part(_ref("Q"), "Transistor_FET:2N7002", "2N7002", SOT23,
               {"G": "J1850_TXP", "S": "GND", "D": "J1850_PG"}, lcsc=LCSC["2N7002"]))
R(jb, "10k", "J1850_TXP", "GND")
R(jb, "1.5k", "VJ1850", "J1850_PG")
d.add(jb, Part(_ref("Q"), "Transistor_FET:AO3401A", "AO3401A", SOT23,
               {"G": "J1850_PG", "S": "VJ1850", "D": "J1850_PD"}, lcsc=LCSC["AO3401A"]))
d.add(jb, Part(_ref("D"), "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
               {"A": "J1850_PD", "K": "J1850_P"}, lcsc=LCSC["B5819W"]))
R(jb, "10k", "J1850_P", "J1850_PLOAD")
d.add(jb, Part(_ref("D"), "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
               {"A": "J1850_PLOAD", "K": "GND"}, lcsc=LCSC["B5819W"]))
# Bus- driver
d.add(jb, Part(_ref("Q"), "Transistor_BJT:MMBT3904", "MMBT3904", SOT23,
               {"B": "J1850_NB", "E": "GND", "C": "J1850_N"}, lcsc=LCSC["MMBT3904"]))
R(jb, "1k", "J1850_TXN", "J1850_NB")
R(jb, "100k", "J1850_TXN", "GND")
R(jb, "2.2k", "J1850_NB", "GND")
d.add(jb, Part(_ref("D"), "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123",
               {"A": "5V_BUCK", "K": "J1850_NPU"}, lcsc=LCSC["B5819W"]))
R(jb, "10k", "J1850_NPU", "J1850_N")
# receivers
U(jb, "Comparator:LM2901", "LM2901BIDR", "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm", {
    "3": "CMP_VCC", "12": "GND",
    "5": "VPW_IN", "4": "VPW_REF", "2": "VPW_RX",          # A: VPW
    "7": "PWM_INP", "6": "PWM_INN", "1": "PWM_RX",         # B: PWM
    "11": "K_IN", "10": "K_REF", "13": "KLINE_RX",         # C: K-line
    "9": "GND", "8": "CMP_VCC", "14": None,                # D: spare, output low
}, lcsc_key="LM2901BIDR")
# 1k (it was 470R): at about 41 V held for 0.4 s, the most the TVS lets by for that long, the 33 V zener
# takes 10 mA (0.3 W), not 20 (0.7 W in a 0.35 W part); at 9 V the LM2901's 2 mA still leaves it 7 V
R(jb, "1k", "DLC_SW", "CMP_VCC", fp="Resistor_SMD:R_1206_3216Metric", lcsc_key="R1k_1206")
d.add(jb, Part(_ref("D"), "Device:D_Zener", "BZT52C33", "Diode_SMD:D_SOD-123",
               {"K": "CMP_VCC", "A": "GND"}, lcsc=LCSC["BZT52C33"]))
C(jb, "100n", "CMP_VCC")
R(jb, "10k", "J1850_P", "VPW_IN")
# VPW threshold 5.33 V x 47 / 65 = 3.86 V (3.73-3.98 V over the buck's and the resistors' tolerance), the
# middle of J1850's 3.5-4.25 V receive window. STN's 10 / 12.7 ratio is for a 5.0 V reference: on 5.33 V
# it gave 4.17 V nominal and up to 4.30 V (REVIEW 45). 18k / 47k draws 82 uA: the divider is on 5V_BUCK,
# which stays up while the car is parked
R(jb, "18k", "5V_BUCK", "VPW_REF")
R(jb, "47k", "VPW_REF", "GND")
R(jb, "10k", "3V3", "VPW_RX")
R(jb, "10k", "J1850_P", "PWM_INP")
R(jb, "10k", "J1850_N", "PWM_INN")
R(jb, "10k", "3V3", "PWM_RX")
for n in ("VPW_IN", "PWM_INP", "PWM_INN", "K_IN"):
    d.add(jb, Part(_ref("D"), "Device:D", "1N4148W", "Diode_SMD:D_SOD-123",
                   {"A": "GND", "K": n}, lcsc=LCSC["1N4148W"]))
for n in ("J1850_P", "J1850_N"):
    d.add(jb, Part(_ref("D"), "Device:D_TVS", "SMF24A", "Diode_SMD:D_SOD-123F",
                   {"A1": n, "A2": "GND"}, lcsc=LCSC["SMF24A"]))
    TP(jb, n)
TP(jb, "VJ1850")

# ---------------------------------------------------------------- ISO 9141 / KWP2000
iso = d.block("iso", "ISO 9141-2 / ISO 14230 K-line (J1962 pin 7) and L-line (pin 15): STN-style discrete")
iso.notes = [
    "510R 2 W pull-ups (1.5 W with a line held low at a 28 V jump start) fed from DLC_SW through 1N4148W.",
    "The diodes stop the car's own pull-up back-feeding the sleeping DLC_SW loads (which would otherwise hold K/L low).",
    "MMBT3904 sinks, 470R base: TX high = line low.",
    "RX: LM2901 unit C, K vs DLC_SW/2 (10k/10k), K high -> KLINE_RX high (PC0's internal pull-up, set by firmware).",
]
for line, tx in (("KLINE", "KLINE_TX"), ("LLINE", "LLINE_TX")):
    d.add(iso, Part(_ref("D"), "Device:D", "1N4148W", "Diode_SMD:D_SOD-123",
                    {"A": "DLC_SW", "K": f"{line}_PU"}, lcsc=LCSC["1N4148W"]))
    R(iso, "510 2W", f"{line}_PU", line, fp="Resistor_SMD:R_2512_6332Metric", lcsc_key="R510_2512")
    d.add(iso, Part(_ref("Q"), "Transistor_BJT:MMBT3904", "MMBT3904", SOT23,
                    {"B": f"{line}_B", "E": "GND", "C": line}, lcsc=LCSC["MMBT3904"]))
    R(iso, "470", tx, f"{line}_B")
    R(iso, "100k", tx, "GND")
    d.add(iso, Part(_ref("D"), "Device:D_TVS", "SMF24A", "Diode_SMD:D_SOD-123F",
                    {"A1": line, "A2": "GND"}, lcsc=LCSC["SMF24A"]))
    TP(iso, line)
R(iso, "10k", "KLINE", "K_IN")
R(iso, "10k", "DLC_SW", "K_REF")
R(iso, "10k", "K_REF", "GND")
_ref("R")   # R60 was KLINE_RX's 10k pull-up: PC0's internal pull-up (~40k, firmware) does it at 10.4 kbaud

# ---------------------------------------------------------------- ESP32 USB
# (last in the file so the parts added in Rev 0 round 3 don't renumber the rest)
eu = d.block("espusb", "ESP32 serial port on the USB hub (CH340C on UART0, auto-reset), reset / boot buttons")
eu.notes = [
    "The ESP32's serial port is port 2 of the USB hub (U18, USB page): the one USB-C reaches both chips.",
    "U2S_DTR high with U2S_RTS low pulls EN low (reset); U2S_RTS high with U2S_DTR low pulls IO0 low (boot); equal: neither.",
    "VBUS turns the ESP32's load switch on through a diode, so esptool works on a board whose STM32 is blank.",
    "CH340C runs from 3V3_ESP: it is off whenever the ESP32 is, so it can't back-power the module through RXD0.",
    "Auto-reset: the usual two-NPN cross pair on DTR/RTS drives EN and IO0 (esptool's default reset).",
    "1k in series with the CH340C TXD: it limits the current into RXD0, and a probe on the net can still drive it.",
    "SW4 / SW5 pull EN / IO0 low by hand.",
]
for _gone in ("J", "R", "R", "U", "C", "D"):      # J7; R61, R62; U13; C44; D25
    _ref(_gone)
U(eu, "OCTAVE:CH340C", "CH340C", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
  {"1": "GND", "2": "U2S_TXD", "3": "ESP_TXD0", "4": "3V3_ESP", "5": "ESP_DP", "6": "ESP_DM",
   "7": None, "8": None, "9": None, "10": None, "11": None, "12": None,
   "13": "U2S_DTR", "14": "U2S_RTS", "15": None, "16": "3V3_ESP"})
C(eu, "100n", "3V3_ESP")
R(eu, "1k", "U2S_TXD", "ESP_RXD0")
# DTR low + RTS high -> EN low (reset); RTS low + DTR high -> IO0 low (download strap); both equal -> neither
for base, emit, coll in (("U2S_DTR", "U2S_RTS", "ESP_EN"), ("U2S_RTS", "U2S_DTR", "ESP_IO0")):
    R(eu, "10k", base, f"{base}_B")
    d.add(eu, Part(_ref("Q"), "Transistor_BJT:MMBT3904", "MMBT3904", SOT23,
                   {"B": f"{base}_B", "E": emit, "C": coll}, lcsc=LCSC["MMBT3904"]))
for name, net in (("ESP_RST", "ESP_EN"), ("ESP_BOOT", "ESP_IO0")):
    d.add(eu, Part(_ref("SW"), "Switch:SW_Push", name, "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A",
                    {"1": net, "2": "GND"}, lcsc=LCSC.get("SW")))
# ON = PB6 (ESP_PWR_EN, or CONFIG switch 1) OR VBUS (a USB cable): diodes keep 5 V off PB6, and
# PB6's UCPD dead-battery pull-down (5.1k until PWR_CR3.UCPD_DBDIS) can't hold the switch off
for src in ("ESP_PWR_EN", "VBUS"):
    d.add(eu, Part(_ref("D"), "Device:D", "1N4148W", "Diode_SMD:D_SOD-123",
                    {"A": src, "K": "ESP_PWR_ON"}, lcsc=LCSC["1N4148W"]))

# ---------------------------------------------------------------- CONFIG switch
# One 4-way SMD DIP switch for the bench settings (it replaced three 2-pin jumpers and the BOOT0
# button): 1 ESP_ON (3V3 -> 1k -> PB6 / the ESP32 load switch), 2 / 3 the 120R CAN1 / CAN2 terminations,
# 4 BOOT0 (STM32 ROM bootloader on the next reset). Off = car use. Created last so the references
# before it keep their numbers (SW6).
ui.notes.append("CONFIG (SW6): 1 ESP_ON, 2 CAN1 120R, 3 CAN2 120R, 4 BOOT0. All off in the car.")
d.add(ui, Part(_ref("SW"), "Switch:SW_DIP_x04", "CONFIG",
               "Button_Switch_SMD:SW_DIP_SPSTx04_Slide_KingTek_DSHP04TS_W7.62mm_P1.27mm",
               {"1": "CFG1_3V3", "8": "ESP_PWR_EN", "2": "CAN1_TERM", "7": "CAN1_L",
                "3": "CAN2_TERM", "6": "CAN2_L", "4": "3V3", "5": "BOOT0"}, lcsc=LCSC.get("DIP4")))

# ---------------------------------------------------------------- ESP32 IO header
# Footprint only (DNP), like J5: eight spare ESP32 pins on the module side that faces it (IO34 / IO35
# are input-only; IO25-27 / IO14 lose their ADC while Wi-Fi is on). Powered from 3V3_ESP, so whatever
# is plugged in turns off with the ESP32 and can't back-power it. Created last so it is J8.
esp.notes.append("J8 (DNP): ESP32 IO14 27 26 25 33 32 35 34 + 3V3_ESP / GND; 3.3 V logic, IO34/35 input-only.")
d.add(esp, Part(_ref("J"), "Connector_Generic:Conn_01x10", "ESP_IO",
                "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical",
                {"1": "GND", **{str(i + 2): "ESP_" + n for i, n in enumerate(ESP_IO)}, "10": "3V3_ESP"},
                dnp=True))

# ---------------------------------------------------------------- parked watch, evidence, panic button
# Added for traffic stop mode and the companion app (TODO/traffic-stop-mode.md, TODO/octave-companion-app.md).
# Created last so every earlier reference keeps its number.
sn = d.block("sense", "Parked watch and evidence: accelerometer, secure element, panic button")
sn.notes = [
    "U15 LIS2DH12 on the STM32 I2C bus (0x19, SA0 high): wake-on-motion while parked (tow / break-in),",
    "crash spikes while driving. INT1 -> PC14, INT2 -> PC15 (EXTI, wake from Stop).",
    "U16 ATECC608B (0x60) on the same bus: signs the frozen OBD speed window for traffic stop evidence.",
    "J9 panic button (hidden, on a cable): 2k series at the connector, then the ESD diode, a 5.1 V zener,",
    "100k pull-up and 100n at the pin, so a cable shorted to a 14.4 V battery is limited to 4.5 mA into the zener.",
    "PANIC_BTN = PC13 (WKUP2, wakes from Standby too). Closed = low.",
]
U(sn, "OCTAVE:LIS2DH12", "LIS2DH12TR", "Package_LGA:LGA-12_2x2mm_P0.5mm",
  {"VDD": "3V3", "VDD_IO": "3V3", "~{CS}": "3V3", "SDO/SA0": "3V3", "RES": "GND", "GND": "GND",
   "SCL/SPC": "I2C_SCL", "SDA/SDI/SDO": "I2C_SDA", "INT1": "ACC_INT1", "INT2": "ACC_INT2"}, lcsc_key="LIS2DH12TR")
C(sn, "100n", "3V3")
C(sn, "10u", "3V3")
U(sn, "Security:ATECC608B-SSHDA", "ATECC608B-SSHDA-T", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"VCC": "3V3", "GND": "GND", "SDA": "I2C_SDA", "SCL": "I2C_SCL", "NC": None}, lcsc_key="ATECC608B")
C(sn, "100n", "3V3")
d.add(sn, Part(_ref("J"), "Connector_Generic:Conn_01x02", "PANIC",
               "Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal",
               {"1": "PANIC_IN", "2": "GND"}, lcsc=LCSC.get("SH2")))
# The clamp sits behind the series resistor (REVIEW 48): ahead of it, a cable shorted to +12 V put the battery
# straight across a 3.3 V diode
d.add(sn, Part(_ref("D"), "Device:D_TVS", "ESD9B3.3ST5G", "Diode_SMD:D_SOD-923",
               {"1": "PANIC_BTN", "2": "GND"}, lcsc=LCSC["ESD9B3.3"]))
# 2k, 1206 (0.25 W): with the cable shorted to the battery it carries 4.5 mA (0.04 W) for as long as that
# lasts, under the 5 mA at which the zener D30 is specified (5.36 V at most); at a 28 V jump start, 0.26 W.
# A closed button reads 0.06 V against the 100k pull-up.
R(sn, "2k", "PANIC_IN", "PANIC_BTN", fp="Resistor_SMD:R_1206_3216Metric", lcsc_key="R2k_1206")
R(sn, "100k", "3V3", "PANIC_BTN")     # 100k: with the cable shorted to the battery D30 sits at 5.4 V; 10k would feed 0.2 mA into a parked 3V3 rail
C(sn, "100n", "PANIC_BTN")

# ---------------------------------------------------------------- microSD (ESP32)
sd = d.block("sd", "microSD on the ESP32 (SPI): backup store for the phone's traffic stop recordings")
sd.notes = [
    "J10 microSD (push-pull) in SPI mode: MISO IO4, CLK IO5, MOSI IO21, CS IO22, card detect IO23",
    "(switch to GND, ESP32 internal pull-up). Powered from 3V3_ESP: off with the ESP32.",
    "10k pull-ups (RN1 array + R68) on CS / MOSI / MISO and the unused DAT1 / DAT2 (SD spec). The card slides in from the",
    "bottom edge and sticks out about 3 mm, so it can be pulled.",
]
d.add(sd, Part(_ref("J"), "Connector:Micro_SD_Card_Det2", "microSD",
               "Connector_Card:microSD_HC_Molex_104031-0811",
               {"DAT2": "SD_DAT2", "DAT3/CD": "ESP_SD_CS", "CMD": "ESP_SD_MOSI", "VDD": "3V3_ESP",
                "CLK": "ESP_SD_CLK", "VSS": "GND", "DAT0": "ESP_SD_MISO", "DAT1": "SD_DAT1",
                "DET_A": "GND", "DET_B": "ESP_SD_CD", "SHIELD": "GND"}, lcsc=LCSC.get("MICROSD")))
C(sd, "10u", "3V3_ESP")
C(sd, "100n", "3V3_ESP")
# pull-ups: one 4 x 10k array (CS, MOSI, MISO, DAT1) + DAT2's own
sd_pu = ("ESP_SD_CS", "ESP_SD_MOSI", "ESP_SD_MISO", "SD_DAT1")
d.add(sd, Part(_ref("RN"), "Device:R_Pack04", "4x10k", "Resistor_SMD:R_Array_Convex_4x0603",
               {**{str(i + 1): n for i, n in enumerate(sd_pu)}, **{str(8 - i): "3V3_ESP" for i in range(4)}},
               lcsc=LCSC["RN10k"]))
R(sd, "10k", "3V3_ESP", "SD_DAT2")

# ---------------------------------------------------------------- audit round 5 (REVIEW 46, 47)
# Created last so every earlier reference keeps its number (R69, R70).
# R69: VBUS has no other path to ground with the cable out, so the reverse leakage of the SS34 between it
# and 5V (uA at room temperature, 0.3-0.4 mA in a hot car) would go through the ON diode into the load
# switch's 100k pull-down: 10 uA is 1 V, the TPS22918's VIH, and the ESP32 would turn itself on while parked.
# 1k holds VBUS under 0.4 V at 0.4 mA (VIL 0.5 V, less the diode's drop); 5 mA while a cable is in.
R(eu, "1k", "VBUS", "GND")
# R70: CONFIG switch 1 used to tie 3V3 straight to PB6; firmware driving PB6 low (ESP32 off) with the switch
# on would short the rail through the pin. Through 1k that is 3.3 mA. With PB6's 5.1k dead-battery
# pull-down still on (blank chip) PB6 sits at 2.76 V and ON at ~2.2 V, above the 1 V VIH.
R(ui, "1k", "3V3", "CFG1_3V3")

# ---------------------------------------------------------------- pod bus (2026-11-23)
# Created last so every earlier reference keeps its number. The adapter's end of the OCTAVE pod bus
# (../../pods/pod-bus/README.md): battery voltage and RS-485 on a JST GH, for a chain of pods. Everything
# but the fuse sits in the column over J11, on both sides of the board.
pod = d.block("pod", "Pod bus: switched and fused battery voltage + RS-485 for a chain of pods")
pod.notes = [
    "Pods are small sensor boards daisy-chained on one four-wire cable (battery voltage + RS-485).",
    "J11 (JST GH): 1 POD_VIN, 2 RS-485 A, 3 RS-485 B, 4 GND. The adapter is one end of the chain: R77 terminates it.",
    "VBAT_P -> F2 (0.5 A, at the buck's input: the run across the board is fused) -> U19 -> POD_VIN.",
    "U19 TPS27S100A: a high-side switch that limits the current (R72 3k: 0.82 A, 0.59-1.05 A over tolerance and",
    "temperature) and shuts down hot, then retries: a pod cable shorted to ground is survived, at 0.14-0.24 A",
    "for as long as POD_EN is on (firmware: off when the pods stop answering).",
    "POD_EN is PD2 (EXP_PD2 on J5) through 4.7k, off at reset (100k). Off, U19 draws under 2 uA. Its fault",
    "flag is not connected, and the adapter cannot measure the rail. U19 is rated 40 V, 48 V for 0.4 s.",
    "U17 THVD2450: +-70 V on A / B (a cable pinched against POD_VIN), IEC ESD and idle-bus failsafe inside:",
    "no TVS, no bias resistors. It shares USART1 with J5: DI is PC4 (EXP_TX), RO reaches PC5 (EXP_RX) through 1k,",
    "so a USB-UART adapter on J5 still wins. DE is PA5 (100k down), RE (active low) is PC10 (100k up):",
    "both idle = shutdown, under 1 uA, and RO floats (firmware: pull PC5 up). Listening costs 1.8 mA.",
    "PC10 is USART3 TX to the system bootloader, which may toggle RE: that only switches the receiver.",
]
d.add(pod, Part(_ref("J"), "Connector_Generic_MountingPin:Conn_01x04_MountingPin", "POD BUS",
                "Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal",
                {"1": "POD_VIN", "2": "RS485_A", "3": "RS485_B", "4": "GND", "MP": "GND"}, lcsc=LCSC["GH4"]))
d.add(pod, Part(_ref("F"), "Device:Polyfuse", "0.5A 60V", "Fuse:Fuse_1812_4532Metric",
                {"1": "VBAT_P", "2": "POD_F"}, lcsc=LCSC["PTC05"]))
# Q12, R71, D29 and Q13 were the discrete high-side switch (SI2309CDS, 47k, 12 V zener, 2N7002): U19, at the
# end of this block, replaced it. C52 and R72 keep their numbers with new jobs: U19's input cap and its
# current-limit resistor (R_ILIM = 2466 / I: 3k is 0.82 A)
_ref("Q")
_ref("R")
_ref("D")
C(pod, "1u", "POD_F")
R(pod, "3k", "POD_ILIM", "GND")
_ref("Q")
R(pod, "100k", "EXP_PD2", "GND")
C(pod, "1u", "POD_VIN")
U(pod, "OCTAVE:RS485_HD", "THVD2450DR", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"RO": "RS485_RO", "RE": "EXP_PC10", "DE": "EXP_PA5", "DI": "EXP_TX", "GND": "GND",
   "A": "RS485_A", "B": "RS485_B", "VCC": "3V3"}, lcsc_key="THVD2450")
C(pod, "100n", "3V3")
R(pod, "100k", "EXP_PA5", "GND")
R(pod, "100k", "3V3", "EXP_PC10")
R(pod, "1k", "RS485_RO", "EXP_RX")
R(pod, "120", "RS485_A", "RS485_B", fp="Resistor_SMD:R_1206_3216Metric", lcsc_key="R120_1206")

# ---------------------------------------------------------------- USB hub (2026-11-24)
# Created last (U18, Y2, C55, C56). WCH CH334P, QFN-16 3x3 (datasheet V2.4, figure 1-1): 1 XO, 2 XI,
# 3 / 4 DM4 / DP4, 5 / 6 DM3 / DP3, 7 / 8 DM2 / DP2, 9 / 10 DM1 / DP1, 11 DMU, 12 DPU, 13 RESET# (pull-up
# inside: left open), 14 PGANG (pull-up inside, an output after reset: left open), 15 V5 and 16 VDD33 (both
# on 3V3, 1u at each: audit 2026-11-24, the hub must not be the one USB chip without power), pad GND. Pull-ups, pull-downs and the crystal's capacitors are inside.
U(usb, "OCTAVE:CH334P", "CH334P", "Package_DFN_QFN:WCH_QFN-16-1EP_3x3mm_P0.5mm_EP1.8x1.8mm",
  {"1": "HUB_XO", "2": "HUB_XI", "3": None, "4": None, "5": "MCU_DM", "6": "MCU_DP", "7": "ESP_DM", "8": "ESP_DP",
   "9": None, "10": None, "11": "USB_DM", "12": "USB_DP", "13": None, "14": None, "15": "3V3",
   "16": "3V3", "17": "GND"})
d.add(usb, Part(_ref("Y"), "Device:Crystal_GND24", "12MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
                {"1": "HUB_XI", "3": "HUB_XO", "2": "GND", "4": "GND"}, lcsc=LCSC["XTAL12M"]))
C(usb, "10u", "3V3")     # (10u: the hub's low-voltage reset is 3.2 V at worst, the LDO 3.25 V before a load step)
C(usb, "1u", "3V3")

# ---------------------------------------------------------------- review of 2026-11-24
# Created last so every earlier reference keeps its number.
# Q14: CONFIG switch 4 (BOOT0) also holds the ESP32 in reset. A USB cable powers the ESP32 by itself now,
# and one byte from it on PA3 while the STM32's ROM bootloader is choosing its interface (it listens on
# USART2 too) keeps DFU from ever enumerating. R78 holds that line idle while the module is in reset.
d.add(mcu, Part(_ref("Q"), "Transistor_FET:2N7002", "2N7002", SOT23,
                {"G": "BOOT0", "S": "GND", "D": "ESP_EN"}, lcsc=LCSC["2N7002"]))
R(esp, "100k", "3V3_ESP", "ESP_TX_MCU")

# U19, the pod bus's supply switch (TPS27S100A, HTSSOP-14: datasheet SLVSE42B p3): 1 / 4 / 11 NC, 2 GND,
# 3 EN (500k pull-down inside), 5-7 OUT, 8-10 IN, 12 DIAG_EN (GND: diagnostics off, lowest standby),
# 13 ILIM, 14 FLT (open drain, not used), pad GND. No ground network: the ideal diode ahead of VBAT_P
# already blocks a reversed battery.
U(pod, "OCTAVE:TPS27S100A", "TPS27S100APWPR", "Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask2.94x3.34mm",
  {"IN": "POD_F", "OUT": "POD_VIN", "EN": "POD_EN", "DIAG_EN": "GND", "ILIM": "POD_ILIM", "GND": "GND",
   "~{FLT}": None, "NC": None}, lcsc_key="TPS27S100A")
R(pod, "4.7k", "EXP_PD2", "POD_EN")
# D30: a 5.1 V zener on the panic input. D28 (the ESD diode) breaks down anywhere from 5 to 7 V, so a cable
# shorted to the battery could hold PC13 above its 5.5 V limit; the zener holds it under 5.4 V at 14.4 V
# and under 5.8 V at a 28 V jump start (the pin's absolute maximum is 7.3 V).
d.add(sn, Part(_ref("D"), "Device:D_Zener", "BZT52C5V1", "Diode_SMD:D_SOD-123",
               {"K": "PANIC_BTN", "A": "GND"}, lcsc=LCSC["BZT52C5V1"]))

# ---------------------------------------------------------------- second review of 2026-11-24
# Q15 / D31 / R80: the LM317L's input clamp. An emitter follower from DLC_SW with its base held at 30 V:
# VJ_IN follows DLC_SW less about 1.1 V (0.7 V + the base current in 1k) and stops at 29 V, whatever the
# TVS lets through. A firmware rule cannot do this: pulse 2a rises in a microsecond. Cost: Bus+ falls under
# VPW's 6.25 V when the plug is under about 9.0 V (10.8 V at worst, cold); without the follower 8.2 / 9.3 V.
# Q15 is a BCX56-16 in SOT-89 (80 V, hFE 100-250), not a SOT-23: with Bus+ shorted while the adapter
# transmits, the LM317L's current limit flows through it: 0.42 W typical (0.2 A, Vce 2.1 V), 1.4 W at
# worst (0.29 A, hFE 70, Vce 4.9 V). The part takes 0.5 W on its bare pad, 0.95 W with 1 cm2 of collector
# copper, 1.35 W with 6 cm2; this board gives it four vias in the tab to about 0.25 cm2 on the top side
# (prewire.py), so the worst case is not covered by copper: firmware must stop transmitting when VPW_RX
# does not echo the bus going high. (R80 stays 1k: at 470 ohm D31 would take 0.76 W in a 41 V load dump.)
# Nexperia numbers the SOT-89's pins 1 = E, 2 = C, 3 = B on an outline drawn 3-2-1: the same physical
# part as KiCad's B-C-E on pads 1-2-3. Nothing to correct.
d.add(jb, Part(_ref("Q"), "Transistor_BJT:BCX56", "BCX56-16", "Package_TO_SOT_SMD:SOT-89-3",
               {"B": "VJ_B", "E": "VJ_IN", "C": "DLC_SW"}, lcsc=LCSC["BCX56-16"]))
d.add(jb, Part(_ref("D"), "Device:D_Zener", "BZT52C30", "Diode_SMD:D_SOD-123",
               {"K": "VJ_B", "A": "GND"}, lcsc=LCSC["BZT52C30"]))
R(jb, "1k", "DLC_SW", "VJ_B")
C(jb, "100n", "VPW_REF")     # the VPW threshold: 13k of source impedance on a comparator input
# C58: 100n at the hub's supply pins (it had 10u and 1u only)
C(usb, "100n", "3V3")
