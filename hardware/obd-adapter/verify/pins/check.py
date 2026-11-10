import re, sys, xml.etree.ElementTree as ET
ns = {"s": "http://dummy.com"}
root = ET.parse("g474r.xml").getroot()
st = {}
for p in root.findall("s:Pin", ns):
    st[p.get("Position")] = (p.get("Name"), p.get("Type"), [s.get("Name") for s in p.findall("s:Signal", ns)])
# AF numbers
g = ET.parse("gpio.xml").getroot()
af = {}
for gp in g.findall("s:GPIO_Pin", ns):
    pn = gp.get("Name")
    for sig in gp.findall("s:PinSignal", ns):
        for v in sig.iter("{http://dummy.com}PossibleValue"):
            if v.text and v.text.startswith("GPIO_AF"):
                af[(re.sub(r"[-_ ].*", "", pn), sig.get("Name"))] = v.text.split("_")[1]
# our netlist: U1 pins
ours = {}
txt = open(sys.argv[1]).read()
blk = txt.split("\nU1  ")[1].split("\n\n")[0]
for m in re.finditer(r"pin\s+(\d+)\s+(\S+)\s+\(\S+\)\s+->\s+(\S+)", blk):
    ours[m.group(1)] = (m.group(2), m.group(3))
# 1. symbol pin numbering vs ST
bad = 0
for pos, (name, typ, sigs) in sorted(st.items(), key=lambda x: int(x[0])):
    sname = ours[pos][0]
    stbase = re.sub(r"[-_ ].*", "", name) if typ == "I/O" else name
    if sname != stbase and not (name.startswith("PG10") and sname == "PG10"):
        print(f"PIN NAME MISMATCH pos {pos}: KiCad {sname} vs ST {name}"); bad += 1
print("symbol pin names vs ST:", "OK" if not bad else f"{bad} mismatches")
# 2. function per net
need = {
 "HSE_IN": "RCC_OSC_IN", "HSE_OUT": "RCC_OSC_OUT",
 "KLINE_RX": "LPUART1_RX", "KLINE_TX": "LPUART1_TX",
 "ESP_RTS": "USART2_CTS", "ESP_CTS_M": "USART2_RTS", "MCU_TX_ESP_M": "USART2_TX", "ESP_TX_MCU": "USART2_RX",
 "VBAT_SENSE": "ADC2_IN17",
 "QSPI_IO3": "QUADSPI1_BK1_IO3", "QSPI_IO2": "QUADSPI1_BK1_IO2", "QSPI_IO1": "QUADSPI1_BK1_IO1", "QSPI_IO0": "QUADSPI1_BK1_IO0",
 "QSPI_CLK": "QUADSPI1_CLK", "QSPI_NCS": "QUADSPI1_BK1_NCS",
 "EXP_TX": "USART1_TX", "EXP_RX": "USART1_RX",
 "CAN1_RXD": "FDCAN2_RX", "CAN1_TXD": "FDCAN2_TX", "CAN2_RXD": "FDCAN3_RX", "CAN2_TXD": "FDCAN3_TX",
 "VPW_RX": "TIM3_CH1", "PWM_RX": "TIM3_CH2", "J1850_TXP": "TIM3_CH3", "J1850_TXN": "TIM3_CH4",
 "I2C_SDA": "I2C2_SDA", "I2C_SCL": "I2C2_SCL",
 "USB_DM": "USB_DM", "USB_DP": "USB_DP", "SWDIO": "SYS_JTMS-SWDIO", "SWCLK": "SYS_JTCK-SWCLK",
}
print("\nnet -> pin -> required signal")
for pos, (sname, net) in sorted(ours.items(), key=lambda x: int(x[0])):
    name, typ, sigs = st[pos]
    if net in need:
        req = need[net]
        ok = req in sigs
        a = af.get((sname, req), "-")
        print(f"{'OK  ' if ok else 'FAIL'} {pos:>2} {name:<16} {net:<12} needs {req:<20} AF{a}")
        if not ok: print("      available:", sigs)
    else:
        print(f"     {pos:>2} {name:<16} {net}")
