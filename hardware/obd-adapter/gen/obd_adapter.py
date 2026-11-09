"""OCTAVE OBD adapter, Rev 0 (bring-up board): the netlist.

Run build.py to regenerate the schematic, the board and the fab outputs.
Pin plan and design notes: ../README.md.
"""

from design import Design, Part
from parts import LCSC

d = Design("obd-adapter")

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"

_n = {}


def _ref(prefix):
    _n[prefix] = _n.get(prefix, 0) + 1
    return f"{prefix}{_n[prefix]}"


def R(block, value, a, b, fp=R0603, lcsc_key=None, dnp=False, note=""):
    return d.add(block, Part(_ref("R"), "Device:R", value, fp, {"1": a, "2": b},
                             lcsc=LCSC.get(lcsc_key or ("R" + value), ""), dnp=dnp, note=note))


def C(block, value, a, b="GND", fp=C0603, lcsc_key=None, dnp=False):
    return d.add(block, Part(_ref("C"), "Device:C", value, fp, {"1": a, "2": b},
                             lcsc=LCSC.get(lcsc_key or ("C" + value), ""), dnp=dnp))


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
    "HS-CAN (J1962 6/14) = FDCAN2 PB12/PB13. MS-CAN (3/11) = FDCAN3 PB3/PB4.",
    "J1850 on TIM3: CH1 VPW RX, CH2 PWM RX (input capture), CH3/CH4 TX Bus+/Bus- (output compare).",
    "K-line on LPUART1 (PC0 RX / PC1 TX, TXINV: the NPN inverts); 5-baud init bit-bangs PC1. ESP32 on USART2 + RTS/CTS.",
    "PC3 switches DLC_SW (battery to the J1850 supply, comparators, K/L pull-ups); PB2 picks 7.7 V VPW / 5.8 V PWM.",
    "PB4/PB6 carry UCPD dead-battery pull-downs until firmware sets PWR_CR3.UCPD_DBDIS: both are safe low at reset.",
    "DLC_SW_EN is on PC3, not PC10: the system bootloader may drive USART3 TX (PC10) and would switch DLC_SW on.",
]
U(mcu, "MCU_ST_STM32G4:STM32G474RETx", "STM32G474RET6", "Package_QFP:LQFP-64_10x10mm_P0.5mm", {
    "VBAT": "3V3", "VDD": "3V3", "VSS": "GND", "VSSA": "GND", "VDDA": "VDDA", "VREF+": "VDDA",
    "PG10": "NRST", "PB8": "BOOT0",
    "PF0": "HSE_IN", "PF1": "HSE_OUT", "PC14": "ACC_INT1", "PC15": "ACC_INT2",
    "PA0": "ESP_RTS", "PA1": "ESP_CTS_M", "PA2": "MCU_TX_ESP_M", "PA3": "ESP_TX_MCU",
    "PA4": "VBAT_SENSE", "PA5": "EXP_PA5",
    "PA6": "QSPI_IO3", "PA7": "QSPI_IO2", "PB0": "QSPI_IO1", "PB1": "QSPI_IO0",
    "PB10": "QSPI_CLK", "PB11": "QSPI_NCS",
    "PA8": "I2C_SDA", "PA9": "I2C_SCL", "PA10": "EXP_PA10",
    "PA11": "USB_DM", "PA12": "USB_DP", "PA13": "SWDIO", "PA14": "SWCLK",
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
d.add(mcu, Part(_ref("FB"), "Device:FerriteBead_Small", "600R@100MHz", "Inductor_SMD:L_0603_1608Metric",
                {"1": "3V3", "2": "VDDA"}, lcsc=LCSC.get("FB600")))
C(mcu, "100n", "VDDA")
C(mcu, "1u", "VDDA")
C(mcu, "10n", "VDDA", lcsc_key="C10n")
C(mcu, "100n", "NRST")
R(mcu, "10k", "BOOT0", "GND")
d.add(mcu, Part(_ref("Y"), "Device:Crystal_GND24", "16MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
                {"1": "HSE_IN", "3": "HSE_OUT", "2": "GND", "4": "GND"}, lcsc=LCSC.get("XTAL16M")))
C(mcu, "10p", "HSE_IN", lcsc_key="CXTAL")
C(mcu, "10p", "HSE_OUT", lcsc_key="CXTAL")

# ---------------------------------------------------------------- debug, buttons, LEDs
ui = d.block("ui", "Debug, buttons, LEDs")
ui.notes = ["SWD: 2x5 1.27 mm Cortex debug header (ST-Link V3 / J-Link)."]
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
R(ui, "10k", "3V3", "BTN_USER")
for color in "RGB":
    led_a = f"LED_{color}_A"
    R(ui, "1k" if color == "R" else "330", f"LED_{color}", led_a)
    d.add(ui, Part(_ref("D"), "Device:LED", {"R": "red", "G": "green", "B": "blue"}[color], LED0603,
                   {"A": led_a, "K": "GND"}, lcsc=LCSC.get("LED" + color)))

# ---------------------------------------------------------------- USB-C
usb = d.block("usb", "USB-C (device, USB 2.0 FS): firmware DFU, CDC serial to the Orange Pi, bench power")
usb.notes = ["5.1k on CC1/CC2 = sink. VBUS feeds the 5 V rail through a Schottky (bench power without a car)."]
d.add(usb, Part(_ref("J"), "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
                "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
                {"GND": "GND", "VBUS": "VBUS", "CC1": "USB_CC1", "CC2": "USB_CC2", "D+": "USB_DP",
                 "D-": "USB_DM", "SBU1": None, "SBU2": None, "SHIELD": "GND"}, lcsc=LCSC.get("USBC")))
R(usb, "5.1k", "USB_CC1", "GND")
R(usb, "5.1k", "USB_CC2", "GND")
U(usb, "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "USB_DP", "6": "USB_DP", "3": "USB_DM", "4": "USB_DM", "2": "GND", "5": "VBUS"})
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
    "UART0 goes to its own USB-C (CH340C, auto-reset); the STM32 can reset (PB7) and strap IO0 (PB5) itself.",
    "Power-gated by a TPS22918 on PB6 (off at reset), placed at the module's 3V3 pin with its caps.",
    "CONFIG switch 1 (ESP_ON) powers the ESP32 without STM32 firmware; plugging in its USB-C does too.",
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
R(esp, "10k", "3V3_ESP", "ESP_IO0")
for mcu_net, esp_net in (("ESP_CTS_M", "ESP_CTS"), ("MCU_TX_ESP_M", "MCU_TX_ESP"),
                         ("ESP_IO0_M", "ESP_IO0"), ("ESP_EN_M", "ESP_EN")):
    R(esp, "1k", mcu_net, esp_net)
U(esp, "OCTAVE:TPS22918", "TPS22918", "Package_TO_SOT_SMD:SOT-23-6",
  {"VIN": "3V3", "ON": "ESP_SW_ON", "CT": "ESP_PWR_CT", "VOUT": "3V3_ESP", "QOD": "3V3_ESP", "GND": "GND"})
R(esp, "100k", "ESP_SW_ON", "GND")
C(esp, "1n", "ESP_PWR_CT", lcsc_key="C1n")
C(esp, "1u", "3V3")
_ref("J")   # J3 was the ESP_PROG header: the ESP32's USB-C (J7) does its job; the number stays free
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
    "5.0SMDJ26CA (5 kW) is bidirectional on purpose: a unidirectional TVS would short a reversed battery through the PTC.",
    "Everything on VBAT_P must survive the ~42 V TVS clamp: LM74700 65 V, AO4264E 60 V, TPS54360B 60 V, caps 50 V.",
    "TPS54360B: datasheet 600 kHz design (Fig. 32) with FB 57.6k/10.2k = 5.32 V, so 5V is ~4.9 V after the SS34 OR.",
    "EN left open (internal pull-up): the internal UVLO (~4.3 V) keeps the buck running through a 6 V cold crank.",
    "5V_BUCK (not the OR'd 5V) feeds everything on the vehicle side: CAN VCC, the VPW reference, the Bus- pull-up.",
]
d.add(pw, Part(_ref("F"), "Device:Polyfuse", "2A 60V", "Fuse:Fuse_2920_7451Metric",
               {"1": "VBAT_IN", "2": "VBAT_F"}, lcsc=LCSC["PTC"]))
d.add(pw, Part(_ref("D"), "Device:D_TVS", "5.0SMDJ26CA", "Diode_SMD:D_SMC",
               {"A1": "VBAT_F", "A2": "GND"}, lcsc=LCSC["5.0SMDJ26CA"]))
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
R(pw, "162k", "BUCK_RT", "GND", lcsc_key="R162k")
C(pw, "100n", "BUCK_BOOT", "BUCK_SW")
d.add(pw, Part(_ref("L"), "Device:L", "8.2u", "Inductor_SMD:L_Changjiang_FNR8065S",
               {"1": "BUCK_SW", "2": "5V_BUCK"}, lcsc=LCSC["L8u2"],
               note="Sunlord SWPA8065S8R2MT; FNR8065S footprint is the same 8x8 land, check before fab"))
d.add(pw, Part(_ref("D"), "Device:D_Schottky", "B560C", "Diode_SMD:D_SMC",
               {"K": "BUCK_SW", "A": "GND"}, lcsc=LCSC["B560C"]))
R(pw, "57.6k", "5V_BUCK", "BUCK_FB")
R(pw, "10.2k", "BUCK_FB", "GND")
R(pw, "13k", "BUCK_COMP", "BUCK_COMP_RC")
C(pw, "6.8n", "BUCK_COMP_RC")
C(pw, "39p", "BUCK_COMP", lcsc_key="C39p")
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
car = d.block("car", "Car connector: SAE J1962 (OBD-II) male plug on the board, straight PCB pins")
car.notes = [
    "Plugs straight into the car's diagnostic port; mounted on the bottom side so the board lies flat",
    "under the dash with the parts facing out. Pins 4 (chassis) and 5 (signal) ground both go to GND.",
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
    "STB pulled high: transceivers sit in standby (receive-only wake) until firmware drives STB low.",
    "VCC from 5V_BUCK: CAN needs car / 12 V bench power (on USB alone the OR'd 5V is under the 4.5 V minimum).",
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
    "Off in sleep: the J1850 supply, the LM2901 and the K/L 510R pull-ups. STN's note: pull-ups left on DLC",
    "draw current and can disturb the bus (engine stall). SI2309CDS 60 V P-FET high side, 12 V zener gate clamp,",
    "2N7002 level shift: Vgs = -7.4 V at 9 V battery, clamped at -12 V up to the 42 V TVS clamp.",
]
d.add(sw, Part(_ref("Q"), "Transistor_FET:Q_PMOS_GSD", "SI2309CDS", SOT23,
               {"G": "DLC_SW_G", "S": "VBAT_P", "D": "DLC_SW"}, lcsc=LCSC["SI2309CDS"]))
R(sw, "47k", "VBAT_P", "DLC_SW_G")
d.add(sw, Part(_ref("D"), "Device:D_Zener", "BZT52C12", "Diode_SMD:D_SOD-123",
               {"K": "VBAT_P", "A": "DLC_SW_G"}, lcsc=LCSC["BZT52C12"]))
R(sw, "10k", "DLC_SW_G", "DLC_SW_D", fp="Resistor_SMD:R_1206_3216Metric", lcsc_key="R10k_1206")  # 0.09 W at 42 V
d.add(sw, Part(_ref("Q"), "Transistor_FET:2N7002", "2N7002", SOT23,
               {"G": "DLC_SW_EN", "S": "GND", "D": "DLC_SW_D"}, lcsc=LCSC["2N7002"]))
R(sw, "100k", "DLC_SW_EN", "GND")
C(sw, "1u", "DLC_SW")
TP(sw, "DLC_SW")

# ---------------------------------------------------------------- J1850
jb = d.block("j1850", "SAE J1850 VPW (Bus+, J1962 pin 2) and PWM (Bus+ / Bus-, pins 2 / 10): STN-style discrete")
jb.notes = [
    "Supply: LM317L 240/680/470R = 7.24 V (VPW); J1850_VSEL high shorts the 470R = 4.79 V (PWM).",
    "Simulated over LM317L tolerance: Bus+ 6.42-7.54 V (VPW, spec 6.25-8) and 4.26-5.00 V (PWM, 3.80-5.25 assumed).",
    "Bus+: 2N7002 -> AO3401A -> Schottky; 10k + Schottky bus load. Bus-: MMBT3904 sink, 10k + Schottky pull-up to 5 V.",
    "RX: LM2901B on CMP_VCC (DLC_SW via 470R, 33 V zener; 38 V abs max). VPW: Bus+ vs 5V_BUCK x 10/12.7, active high.",
    "PWM: Bus+ vs Bus-. Q7 base has 2.2k to GND: the bootloader's PC9 pull-up must not turn it on (Bus- low).",
    "Input clamps are 1N4148W (low leakage): Schottkys leak enough when hot to shift the thresholds behind 10k.",
    "No wave shaping (none of the ELM/STN/OpenDiag designs do); a Rev A option is an RC on the P-FET gate.",
]
U(jb, "Regulator_Linear:LM317L_SO8", "LM317LIDR", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"VI": "DLC_SW", "VO": "VJ1850", "ADJ": "VJ_ADJ", "NC": None}, lcsc_key="LM317LIDR")
C(jb, "100n", "DLC_SW", lcsc_key="C100n")
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
R(jb, "470", "DLC_SW", "CMP_VCC", fp="Resistor_SMD:R_1206_3216Metric", lcsc_key="R470_1206")
d.add(jb, Part(_ref("D"), "Device:D_Zener", "BZT52C33", "Diode_SMD:D_SOD-123",
               {"K": "CMP_VCC", "A": "GND"}, lcsc=LCSC["BZT52C33"]))
C(jb, "100n", "CMP_VCC")
R(jb, "10k", "J1850_P", "VPW_IN")
# VPW threshold 5.32 V x 47 / 60 = 4.17 V; 13k / 47k (was 2.7k / 10k, same ratio within 0.5 %) so the
# divider draws 88 uA instead of 420 uA: it is on 5V_BUCK, which stays up while the car is parked
R(jb, "13k", "5V_BUCK", "VPW_REF")
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
    "510R 2 W pull-ups (1.1 W with a line held low at a 24 V jump start) fed from DLC_SW through 1N4148W.",
    "The diodes stop the car's own pull-up back-feeding the sleeping DLC_SW loads (that held K/L low with DLC_SW off).",
    "MMBT3904 sinks, 470R base: TX high = line low.",
    "RX: LM2901 unit C, K vs DLC_SW/2 (10k/10k), K high -> KLINE_RX high (10k pull-up to 3.3 V).",
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
R(iso, "10k", "3V3", "KLINE_RX")

# ---------------------------------------------------------------- ESP32 USB
# (last in the file so the parts added in Rev 0 round 3 don't renumber the rest)
eu = d.block("espusb", "USB-C for the ESP32 (CH340C on UART0, auto-reset): esptool / console without the STM32")
eu.notes = [
    "Second USB-C (sink, 5.1k CC). VBUS2 powers the board through its own Schottky and turns the ESP32's",
    "load switch on through a diode, so esptool works on a board whose STM32 is blank.",
    "CH340C runs from 3V3_ESP: it is off whenever the ESP32 is, so it can't back-power the module through RXD0.",
    "Auto-reset: the usual two-NPN cross pair on DTR/RTS drives EN and IO0 (esptool's default reset).",
    "1k in series with the CH340C TXD, so an adapter on J3 can still drive RXD0.",
]
d.add(eu, Part(_ref("J"), "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C ESP32",
               "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
               {"GND": "GND", "VBUS": "VBUS2", "CC1": "USB2_CC1", "CC2": "USB2_CC2", "D+": "USB2_DP",
                "D-": "USB2_DM", "SBU1": None, "SBU2": None, "SHIELD": "GND"}, lcsc=LCSC.get("USBC")))
R(eu, "5.1k", "USB2_CC1", "GND")
R(eu, "5.1k", "USB2_CC2", "GND")
U(eu, "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "USB2_DP", "6": "USB2_DP", "3": "USB2_DM", "4": "USB2_DM", "2": "GND", "5": "VBUS2"})
C(eu, "10u", "VBUS2")
d.add(eu, Part(_ref("D"), "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA",
               {"A": "VBUS2", "K": "5V"}, lcsc=LCSC["SS34"]))
U(eu, "OCTAVE:CH340C", "CH340C", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
  {"1": "GND", "2": "U2S_TXD", "3": "ESP_TXD0", "4": "3V3_ESP", "5": "USB2_DP", "6": "USB2_DM",
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
# ON = PB6 (ESP_PWR_EN, or CONFIG switch 1) OR VBUS2 (the ESP32 USB port): diodes keep 5 V off PB6, and
# PB6's UCPD dead-battery pull-down (5.1k until PWR_CR3.UCPD_DBDIS) can't hold the switch off
for src in ("ESP_PWR_EN", "VBUS2"):
    d.add(eu, Part(_ref("D"), "Device:D", "1N4148W", "Diode_SMD:D_SOD-123",
                    {"A": src, "K": "ESP_SW_ON"}, lcsc=LCSC["1N4148W"]))

# ---------------------------------------------------------------- CONFIG switch
# One 4-way SMD DIP switch for the bench settings (it replaced three 2-pin jumpers and the BOOT0
# button): 1 ESP_ON (3V3 -> PB6 / the ESP32 load switch), 2 / 3 the 120R CAN1 / CAN2 terminations,
# 4 BOOT0 (STM32 ROM bootloader on the next reset). Off = car use. Created last so the references
# before it keep their numbers (SW6).
ui.notes.append("CONFIG (SW6): 1 ESP_ON, 2 CAN1 120R, 3 CAN2 120R, 4 BOOT0. All off in the car.")
d.add(ui, Part(_ref("SW"), "Switch:SW_DIP_x04", "CONFIG",
               "Button_Switch_SMD:SW_DIP_SPSTx04_Slide_KingTek_DSHP04TS_W7.62mm_P1.27mm",
               {"1": "3V3", "8": "ESP_PWR_EN", "2": "CAN1_TERM", "7": "CAN1_L",
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
    "J9 panic button (hidden, on a cable): ESD diode at the connector, 1k series, 10k pull-up, 100n;",
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
d.add(sn, Part(_ref("D"), "Device:D_TVS", "ESD9B3.3ST5G", "Diode_SMD:D_SOD-923",
               {"1": "PANIC_IN", "2": "GND"}, lcsc=LCSC["ESD9B3.3"]))
R(sn, "1k", "PANIC_IN", "PANIC_BTN")
R(sn, "10k", "3V3", "PANIC_BTN")
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
