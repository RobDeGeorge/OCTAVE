#!/usr/bin/env python3
"""Write src/board_pins.h from the board generator, so firmware and copper cannot drift apart.

Sources, both read and never written:
  ../gen/obd_adapter.py        the netlist: which net is on which STM32 pin, the battery divider, the crystal
  ../verify/pins/pincheck.md   the record of the pin check against ST's open pin data: the alternate
                               function number of every pin that needs one

No alternate function number is typed in here. If the design moved a net and the pin check was not
re-run, this script stops instead of emitting a number nobody checked.

    python3 tools/gen_pins.py            rewrite src/board_pins.h
    python3 tools/gen_pins.py --check    exit 1 if src/board_pins.h is stale (ctest runs this)

Run it with the system python3 (the generator imports KiCad's libraries). Exit code 77 means the
generator could not be imported at all, which ctest reports as "skipped".
"""
import contextlib
import difflib
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)
BOARD = os.path.dirname(FIRMWARE)
GEN = os.path.join(BOARD, "gen")
PINCHECK = os.path.join(BOARD, "verify", "pins", "pincheck.md")
HEADER = os.path.join(FIRMWARE, "src", "board_pins.h")

MCU_VALUE = "STM32G474RET6"
SKIP = 77

# "OK    8 PC0              KLINE_RX     needs LPUART1_RX           AFAF8"   (AF- = no AF number: analog / system)
PINCHECK_LINE = re.compile(r"^(\S+)\s+(\d+)\s+(P[A-G]\d+)\S*\s+(\S+)\s+needs\s+(\S+)\s+AF(?:AF(\d+)|-)\s*$")
PIN_NAME = re.compile(r"^P([A-G])(\d+)")
MULTIPLIER = {"": 1, "R": 1, "k": 1_000, "M": 1_000_000}


class Stale(Exception):
    pass


def load_design():
    """[(LQFP pin number, port letter, bit, net)] for every GPIO of the STM32, plus the design object."""
    sys.path.insert(0, GEN)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            import obd_adapter
    except ImportError as exc:
        print(f"gen_pins: cannot import the board generator ({exc}); use the system python3", file=sys.stderr)
        sys.exit(SKIP)
    design = obd_adapter.d
    mcus = [p for p in design.parts if p.value == MCU_VALUE]
    if len(mcus) != 1:
        raise Stale(f"expected one {MCU_VALUE} in the design, found {len(mcus)}")
    mcu = mcus[0]
    nets = mcu.pin_net()
    pins = []
    for pin in mcu.sym.pins:
        m = PIN_NAME.match(pin.name)
        net = nets.get(pin.number)
        if m and net:
            pins.append((int(pin.number), m.group(1), int(m.group(2)), net))
    pins.sort()
    return design, pins


def load_pincheck():
    """{net: (pin name, signal, AF number or None)} from the pin check record."""
    out = {}
    with open(PINCHECK, encoding="utf-8") as f:
        for line in f:
            m = PINCHECK_LINE.match(line.rstrip("\n"))
            if not m:
                continue
            status, _number, pin, net, signal, af = m.groups()
            if status != "OK":
                raise Stale(f"pincheck.md: {pin} {net} is {status!r}, not OK")
            out[net] = (pin, signal, None if af is None else int(af))
    if not out:
        raise Stale("pincheck.md: no checked pins found (format changed?)")
    return out


def ohms(value):
    """'100k 0.1%' -> 100000, '4.7k' -> 4700, '470' -> 470."""
    m = re.match(r"^(\d+(?:\.\d+)?)([RkM]?)(?:\s|$)", value)
    if not m:
        raise Stale(f"cannot read a resistance from {value!r}")
    return round(float(m.group(1)) * MULTIPLIER[m.group(2)])


def two_pin_between(design, a, b, prefix):
    """The one part with reference prefix `prefix` whose two pins sit on nets a and b."""
    found = []
    for part in design.parts:
        if not part.ref.startswith(prefix) or part.dnp:
            continue
        nets = sorted(n for n in part.pin_net().values() if n)
        if nets == sorted((a, b)):
            found.append(part)
    if len(found) != 1:
        raise Stale(f"expected one {prefix} between {a} and {b}, found {[p.ref for p in found]}")
    return found[0]


def crystal_hz(design):
    for part in design.parts:
        nets = set(part.pin_net().values())
        if {"HSE_IN", "HSE_OUT"} <= nets and part.ref.startswith("Y"):
            m = re.match(r"^(\d+(?:\.\d+)?)\s*MHz$", part.value)
            if not m:
                raise Stale(f"{part.ref}: cannot read a frequency from {part.value!r}")
            return part.ref, round(float(m.group(1)) * 1_000_000)
    raise Stale("no crystal between HSE_IN and HSE_OUT")


def render():
    design, pins = load_design()
    checked = load_pincheck()
    by_net = {net: (number, port, bit) for number, port, bit, net in pins}

    # every checked line must still describe the design
    for net, (pin, signal, _af) in checked.items():
        if net not in by_net:
            raise Stale(f"pincheck.md checks {net} on {pin} ({signal}) but the design has no such net on the MCU")
        _number, port, bit = by_net[net]
        if f"P{port}{bit}" != pin:
            raise Stale(f"{net}: design has it on P{port}{bit}, pincheck.md checked {pin}; re-run verify/pins/check.py")

    top = two_pin_between(design, "VBAT_F", "VBAT_SENSE", "R")
    bottom = two_pin_between(design, "VBAT_SENSE", "GND", "R")
    xtal_ref, hse_hz = crystal_hz(design)
    adc = re.match(r"^ADC(\d)_IN(\d+)$", checked["VBAT_SENSE"][1])
    if not adc:
        raise Stale(f"VBAT_SENSE: expected an ADCx_INy signal in pincheck.md, got {checked['VBAT_SENSE'][1]}")

    width = max(len(net) for _n, _p, _b, net in pins)
    out = [
        "/* Generated by tools/gen_pins.py. Do not edit: change the board generator and run the script.",
        " * Sources: ../gen/obd_adapter.py (pin -> net, divider, crystal),",
        " *          ../verify/pins/pincheck.md (signal and alternate function, checked against ST's pin data).",
        " * `ctest` fails when this file no longer matches them. */",
        "#ifndef BOARD_PINS_H",
        "#define BOARD_PINS_H",
        "",
        '#include "pin.h"',
        "",
        f"/* {MCU_VALUE}, LQFP-64: one PIN_<net> per GPIO, in package pin order */",
    ]
    for number, port, bit, net in pins:
        signal = f", {checked[net][1]}" if net in checked else ""
        out.append(f"#define PIN_{net:<{width}} GPIO_PIN({port}, {bit:>2})  /* pin {number:>2}{signal} */")
    out += ["", "/* alternate function numbers (AF0..AF15) of the pins that have a peripheral signal */"]
    for _number, _port, _bit, net in pins:
        if net in checked and checked[net][2] is not None:
            out.append(f"#define AF_{net:<{width}} {checked[net][2]:>2}u  /* {checked[net][1]} */")
    out += [
        "",
        f"/* battery sense: {top.ref} {top.value} from VBAT_F, {bottom.ref} {bottom.value} to GND, on {checked['VBAT_SENSE'][1]} */",
        f"#define VBAT_ADC_UNIT       {adc.group(1)}",
        f"#define VBAT_ADC_CHANNEL    {adc.group(2)}u",
        f"#define VBAT_DIV_TOP_OHM    {ohms(top.value)}u",
        f"#define VBAT_DIV_BOTTOM_OHM {ohms(bottom.value)}u",
        "",
        f"/* {xtal_ref}: the HSE crystal */",
        f"#define BOARD_HSE_HZ        {hse_hz}u",
        "",
        "/* X(net, port letter, bit) for every pin above: tables, the `pins` command */",
        "#define BOARD_PIN_TABLE(X) \\",
    ]
    rows = [f"    X({net}, {port}, {bit})" for _number, port, bit, net in pins]
    out += [row + " \\" for row in rows[:-1]] + [rows[-1], "", "#endif /* BOARD_PINS_H */", ""]
    return "\n".join(out)


def main(argv):
    check = "--check" in argv[1:]
    unknown = [a for a in argv[1:] if a != "--check"]
    if unknown:
        print(__doc__)
        return 2
    try:
        text = render()
    except Stale as exc:
        print(f"gen_pins: {exc}", file=sys.stderr)
        return 1
    if check:
        try:
            with open(HEADER, encoding="utf-8") as f:
                current = f.read()
        except FileNotFoundError:
            current = ""
        if current == text:
            print("board_pins.h is current")
            return 0
        sys.stderr.writelines(difflib.unified_diff(current.splitlines(True), text.splitlines(True),
                                                   "src/board_pins.h", "generated"))
        print("gen_pins: src/board_pins.h is stale; run python3 tools/gen_pins.py", file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(HEADER), exist_ok=True)
    with open(HEADER, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(HEADER, FIRMWARE)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
