"""OCTAVE LoRa pod, Rev 0 (bring-up board): the netlist.

The dash-top half of the LoRa car finder (TODO/lora-car-finder.md, step 4): a LoRa + Bluetooth MCU
module, a GPS module with its own patch antenna and a 6-axis IMU. It is a pod on the OCTAVE pod bus
(../../pod-bus/README.md): four wires, battery voltage and RS-485, in on one connector and out on
the other to the next pod. On an OBD power lead alone (two wires) it runs standalone. Run build.py to regenerate the schematic and the board.
Pin plan and design notes: ../README.md.
"""

from design import Design, Part
from pod_parts import LCSC

d = Design("lora-pod")

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"

_n = {}


def _ref(prefix):
    _n[prefix] = _n.get(prefix, 0) + 1
    return f"{prefix}{_n[prefix]}"


def R(block, value, a, b, note=""):
    return d.add(block, Part(_ref("R"), "Device:R", value, R0603, {"1": a, "2": b},
                             lcsc=LCSC["R" + value], note=note))


def C(block, value, a, b="GND", fp=C0603, lcsc_key=None):
    return d.add(block, Part(_ref("C"), "Device:C", value, fp, {"1": a, "2": b},
                             lcsc=LCSC[lcsc_key or ("C" + value)]))


def D(block, value, a, k, lib="Device:D_Schottky", fp="Diode_SMD:D_SOD-123", note=""):
    return d.add(block, Part(_ref("D"), lib, value, fp, {"A": a, "K": k}, lcsc=LCSC[value], note=note))


# ---------------------------------------------------------------- supply and the cable
pw = d.block("power", "Pod bus: battery voltage and RS-485 in and out, input protection, 3.3 V LDO")
pw.notes = [
    "J1 and J7 (JST GH) are the same four nets, pin for pin: 1 VIN, 2 RS-485 A, 3 RS-485 B, 4 GND. Either is in or out.",
    "VIN is the battery (OBD pin 16), switched and fused by the adapter, or a 5 V bench supply. This pod's own load",
    "goes through F1 (0.5 A); the chain's current passes J1 to J7 on the board, ahead of F1.",
    "D1 clamps a surge at 38.9 V and trips F1 on a reversed lead, D2 blocks reverse current. U1 takes 40 V, 2.7 uA.",
    "VBAT_SENSE = VIN_F x 0.180 (1M / 220k, 10 uA at 12.6 V): the battery, and the alternator charging (engine on).",
    "U7 THVD2450: +-70 V on A / B (a cable pinched against VIN), IEC ESD and idle-bus failsafe inside, so no TVS",
    "and no bias resistors on the bus. RS485_DE (100k down) and RS485_RE (100k up, active low) are separate:",
    "both idle = shutdown, under 1 uA; listening costs 1.8 mA. POD_RX has a 100k pull-up for when RO floats.",
    "SW3 puts the 120R terminator across A / B (both poles ON): on the last pod of the chain only.",
]
d.add(pw, Part(_ref("J"), "Connector_Generic_MountingPin:Conn_01x04_MountingPin", "POD BUS",
               "Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal",
               {"1": "VIN", "2": "RS485_A", "3": "RS485_B", "4": "GND", "MP": "GND"}, lcsc=LCSC["GH4"]))
d.add(pw, Part(_ref("F"), "Device:Polyfuse", "0.5A 30V", "Fuse:Fuse_1206_3216Metric",
               {"1": "VIN", "2": "VIN_F"}, lcsc=LCSC["PTC"]))
D(pw, "SMBJ24A", "GND", "VIN_F", lib="Device:D_Zener", fp="Diode_SMD:D_SMB", note="unidirectional 24 V TVS")
D(pw, "B5819W", "VIN_F", "VSYS")
C(pw, "10u", "VSYS", fp=C1206, lcsc_key="C10u50")
C(pw, "100n", "VSYS")
d.add(pw, Part(_ref("U"), "OCTAVE:TPS7B82_DGN", "TPS7B8233", "Package_SO:HVSSOP-8-1EP_3x3mm_P0.65mm_EP1.57x1.89mm",
               {"IN": "VSYS", "EN": "VSYS", "OUT": "3V3", "GND": "GND", "NC": None}, lcsc=LCSC["TPS7B8233"]))
C(pw, "10u", "3V3")
R(pw, "1M", "VIN_F", "VBAT_SENSE")
R(pw, "220k", "VBAT_SENSE", "GND")
C(pw, "100n", "VBAT_SENSE")
for _gone in ("R", "R"):       # R3 / R4, D3 / D4 were the plain-UART cable's series resistors and ESD diodes:
    _ref(_gone)                 # their numbers stay unused so that every other reference keeps its own
R(pw, "100k", "3V3", "POD_RX")
for _gone in ("D", "D"):
    _ref(_gone)

# ---------------------------------------------------------------- USB-C
usb = d.block("usb", "USB-C: the module's own USB (firmware update, console) and 5 V on the bench")
usb.notes = ["5.1k on CC1 / CC2 = sink. VBUS feeds the module's USB detect and, through D5, the regulator."]
d.add(usb, Part(_ref("J"), "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
                "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
                {"GND": "GND", "VBUS": "VBUS", "CC1": "USB_CC1", "CC2": "USB_CC2", "D+": "USB_DP",
                 "D-": "USB_DM", "SBU1": None, "SBU2": None, "SHIELD": "GND"}, lcsc=LCSC["USBC"]))
R(usb, "5.1k", "USB_CC1", "GND")
R(usb, "5.1k", "USB_CC2", "GND")
d.add(usb, Part(_ref("U"), "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
                {"1": "USB_DP", "6": "USB_DP", "3": "USB_DM", "4": "USB_DM", "2": "GND", "5": "VBUS"},
                lcsc=LCSC["USBLC6-2SC6"]))
C(usb, "4.7u", "VBUS")
D(usb, "B5819W", "VBUS", "VSYS")

# ---------------------------------------------------------------- LoRa + Bluetooth MCU module
mcu = d.block("mcu", "RAK4630 (nRF52840 + SX1262): MCU, Bluetooth LE and 915 MHz LoRa")
mcu.notes = [
    "Antennas: the two IPEX sockets on the module (LoRa bottom right, Bluetooth top left). RF pins not connected.",
    "One 3.3 V supply: VDD_NRF, VBAT_NRF, VBAT_SX and VBAT_IO_SX together, as the datasheet's note for 3.3 V.",
    "Fast signals on the pins Nordic allows above 10 kHz: I2C P0.13 / P0.14, GPS UART P0.15 / P0.16,",
    "pod bus UART P0.19 (RX) / P0.20 (TX), SPI (ranging radio header) P0.17 / P0.21 / P0.24 / P0.25.",
    "Slow ones on the low-frequency pins: PAIR button P1.01, GNSS_EN P1.02 (off at reset: 100k to GND),",
    "LEDs P1.03 / P1.04, GPS 1PPS P0.03, IMU INT1 / INT2 P0.02 / P0.28, VBAT_SENSE P0.31 (AIN7),",
    "RS-485 driver enable P0.30 and receiver enable (active low) P0.26.",
    "The NFC pins (P0.09 / P0.10) are not connected.",
]
d.add(mcu, Part(_ref("U"), "OCTAVE:RAK4630", "RAK4630-9-SM-I", "OCTAVE:RAK4630", {
    "VBUS": "VBUS", "USB-": "USB_DM", "USB+": "USB_DP",
    "4": "I2C_SDA", "5": "I2C_SCL", "6": "GNSS_TXD_M", "7": "GNSS_RXD_M", "8": "SPI_SCK",
    "9": "POD_RX", "10": "POD_TX", "11": "SPI_MOSI", "12": None, "13": None,
    "GND": "GND", "RF_BT": None, "RF_LoRa": None, "17": "NRST", "SWDCLK": "SWCLK", "SWDIO": "SWDIO",
    "VBAT_SX": "3V3", "VBAT_IO_SX": "3V3", "VDD_NRF": "3V3", "VBAT_NRF": "3V3",
    "23": "SPI_MISO", "24": "SPI_NSS", "25": "BTN_PAIR", "26": "GNSS_EN", "27": "LED_G", "28": "LED_R",
    "29": "GNSS_PPS", "30": "IMU_INT1", "31": "IMU_INT2", "32": "RANGE_IRQ", "33": "RS485_DE", "34": "RS485_RE",
    "39": "VBAT_SENSE", "40": "EXP_AIN3", "41": "EXP_AIN2",
}, lcsc=LCSC["RAK4630"]))
C(mcu, "100n", "3V3")
C(mcu, "10u", "3V3")
C(mcu, "100n", "3V3")
C(mcu, "100n", "NRST")
R(mcu, "4.7k", "3V3", "I2C_SDA")
R(mcu, "4.7k", "3V3", "I2C_SCL")

# ---------------------------------------------------------------- debug, buttons, LEDs, headers
ui = d.block("ui", "Debug, buttons, LEDs, bench and ranging-radio headers")
ui.notes = [
    "SWD: 2x5 1.27 mm Cortex debug header (ST-Link V3 / J-Link).",
    "PAIR: P1.01 to GND, internal pull-up (no resistor on the board). It opens the pairing window.",
    "J4 (DNP, 2.54 mm): the bus's four nets, 3V3, the UART behind the RS-485 driver (a 3.3 V USB-UART dongle",
    "can drive POD_RX while RS485_RE is high) and a spare pin.",
    "J5 (DNP, 2.54 mm): SPI, an interrupt and a spare pin for a ranging radio module (UWB or 2.4 GHz LoRa); 3.3 V only.",
]
d.add(ui, Part(_ref("J"), "Connector:Conn_ARM_JTAG_SWD_10", "SWD",
               "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical_SMD",
               {"1": "3V3", "2": "SWDIO", "3": "GND", "4": "SWCLK", "5": "GND", "6": None, "7": None,
                "8": None, "9": "GND", "10": "NRST"}, lcsc=LCSC["SWD"]))
for name, net in (("RESET", "NRST"), ("PAIR", "BTN_PAIR")):
    d.add(ui, Part(_ref("SW"), "Switch:SW_Push", name, "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A",
                   {"1": net, "2": "GND"}, lcsc=LCSC["SW"]))
for color, value in (("G", "330"), ("R", "1k")):
    led_a = f"LED_{color}_A"
    R(ui, value, f"LED_{color}", led_a)
    d.add(ui, Part(_ref("D"), "Device:LED", {"R": "red", "G": "green"}[color], LED0603,
                   {"A": led_a, "K": "GND"}, lcsc=LCSC["LED" + color]))
d.add(ui, Part(_ref("J"), "Connector_Generic:Conn_01x08", "BENCH",
               "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical",
               {"1": "GND", "2": "VIN", "3": "RS485_A", "4": "RS485_B", "5": "3V3", "6": "POD_TX",
                "7": "POD_RX", "8": "EXP_AIN2"}, dnp=True))
d.add(ui, Part(_ref("J"), "Connector_Generic:Conn_01x08", "RANGE",
               "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical",
               {"1": "GND", "2": "3V3", "3": "SPI_SCK", "4": "SPI_MISO", "5": "SPI_MOSI", "6": "SPI_NSS",
                "7": "RANGE_IRQ", "8": "EXP_AIN3"}, dnp=True))

# ---------------------------------------------------------------- GPS
gn = d.block("gnss", "GPS: Quectel L80-R with its patch antenna, switched supply, backup supply")
gn.notes = [
    "U5 switches the GPS supply (GNSS_EN high = on). QOD is open: Quectel asks for a supply without fast discharge.",
    "V_BCKP (7 uA) keeps the RTC and the ephemeris for a hot start: from 3V3 through the 1N4148W and 1k, held by",
    "the 0.1 F 5.5 V supercap (fitted by hand) while the pod has no supply.",
    "Firmware: drive GNSS_RXD_M low (or disconnect it) while GNSS_EN is low, or P0.16 feeds the module through RXD.",
    "RESET (pin 10) is left open: switching the supply restarts the module.",
]
d.add(gn, Part(_ref("U"), "RF_GPS:L80-R", "L80RE-M37", "RF_GPS:Quectel_L80-R",
               {"1": "GNSS_RXD", "2": "GNSS_TXD", "3": "GND", "4": "3V3_GNSS", "5": "V_BCKP", "6": "GNSS_PPS",
                "7": None, "8": None, "9": None, "10": None, "11": None, "12": "GND"}, lcsc=LCSC["L80RE-M37"]))
d.add(gn, Part(_ref("U"), "OCTAVE:TPS22918", "TPS22918", "Package_TO_SOT_SMD:SOT-23-6",
               {"VIN": "3V3", "ON": "GNSS_EN", "CT": "GNSS_CT", "QOD": None, "VOUT": "3V3_GNSS", "GND": "GND"},
               lcsc=LCSC["TPS22918"]))
C(gn, "1u", "3V3")
C(gn, "1n", "GNSS_CT")
R(gn, "100k", "GNSS_EN", "GND")
C(gn, "100n", "3V3_GNSS")
C(gn, "10u", "3V3_GNSS")
R(gn, "330", "GNSS_RXD_M", "GNSS_RXD")
R(gn, "330", "GNSS_TXD", "GNSS_TXD_M")
D(gn, "1N4148W", "3V3", "VBCKP_D", lib="Device:D")
R(gn, "1k", "VBCKP_D", "V_BCKP")
C(gn, "1u", "V_BCKP")
d.add(gn, Part(_ref("C"), "Device:C_Polarized", "0.1F 5.5V", "Capacitor_THT:C_Disc_D11.0mm_W5.0mm_P5.00mm",
               {"1": "V_BCKP", "2": "GND"}, note="supercap, 5 mm pitch coin on edge (e.g. Eaton KR-5R5V104-R); "
               "check its temperature rating for a dash top; fitted by hand"))

# ---------------------------------------------------------------- IMU, Qwiic
im = d.block("imu", "IMU (moved while parked = tow) and Qwiic expansion")
im.notes = [
    "ASM330LHH on I2C, address 0x6A (SA0 low).",
    "Qwiic: a magnetometer or a barometer later, without a new board.",
]
d.add(im, Part(_ref("U"), "OCTAVE:ASM330LHH", "ASM330LHHTR",
               "Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y",
               {"1": "GND", "2": "GND", "3": "GND", "4": "IMU_INT1", "5": "3V3", "6": "GND", "7": "GND",
                "8": "3V3", "9": "IMU_INT2", "10": None, "11": None, "12": "3V3", "13": "I2C_SCL",
                "14": "I2C_SDA"}, lcsc=LCSC["ASM330LHHTR"]))
C(im, "100n", "3V3")
C(im, "100n", "3V3")
d.add(im, Part(_ref("J"), "Connector_Generic:Conn_01x04", "Qwiic",
               "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
               {"1": "GND", "2": "3V3", "3": "I2C_SDA", "4": "I2C_SCL"}, lcsc=LCSC["QWIIC"]))

# ---------------------------------------------------------------- pod bus: second connector, RS-485
# (in the power block; added last so that the references above keep their numbers)
d.add(pw, Part(_ref("J"), "Connector_Generic_MountingPin:Conn_01x04_MountingPin", "POD BUS",
               "Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal",
               {"1": "VIN", "2": "RS485_A", "3": "RS485_B", "4": "GND", "MP": "GND"}, lcsc=LCSC["GH4"]))
d.add(pw, Part(_ref("U"), "OCTAVE:RS485_HD", "THVD2450DR",
               "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
               {"RO": "POD_RX", "RE": "RS485_RE", "DE": "RS485_DE", "DI": "POD_TX", "GND": "GND",
                "A": "RS485_A", "B": "RS485_B", "VCC": "3V3"}, lcsc=LCSC["THVD2450"]))
C(pw, "100n", "3V3")
R(pw, "100k", "RS485_DE", "GND")
R(pw, "100k", "3V3", "RS485_RE")
d.add(pw, Part(_ref("R"), "Device:R", "120", "Resistor_SMD:R_1206_3216Metric",
               {"1": "RS485_A", "2": "RS485_TERM"}, lcsc=LCSC["R120_1206"]))
d.add(pw, Part(_ref("SW"), "Switch:SW_DIP_x02", "END", "Button_Switch_SMD:SW_DIP_SPSTx02_Slide_KingTek_DSHP02TS_W7.62mm_P1.27mm",
               {"1": "RS485_TERM", "2": "RS485_TERM", "3": "RS485_B", "4": "RS485_B"}, lcsc=LCSC["DIP2"],
               note="both ON = 120R across the bus (last pod of the chain)"))
