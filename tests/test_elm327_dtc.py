"""DTC reply parsing (backend/elm327_protocol.py, parity twin of the C++
ELM327Protocol). Replies are ELM327 output with headers and spaces off."""

import pytest

from backend.elm327_protocol import decode_pid, parse_dtc_response, parse_freeze_frame_dtc


@pytest.mark.parametrize("lines, mode, expected", [
    # Pre-CAN: always 6 data bytes, zero padded, no count byte
    (["43000000000000"], 0x03, []),
    (["43013304200000"], 0x03, ["P0133", "P0420"]),
    (["43030100000000"], 0x03, ["P0301"]),
    # CAN: count byte after the mode byte
    (["43010133"], 0x03, ["P0133"]),
    (["4300"], 0x03, []),
    (["4700"], 0x07, []),             # used to decode as a bogus C0700
    (["47010171"], 0x07, ["P0171"]),
    # CAN multi-frame: byte count, then numbered frames
    (["00A", "0:430401330171", "1:03000420000000"], 0x03, ["P0133", "P0171", "P0300", "P0420"]),
    # Two ECUs answering, duplicate code reported once
    (["43010133", "430201330442"], 0x03, ["P0133", "P0442"]),
    (["SEARCHING...", "NO DATA"], 0x03, []),
    (["4303412381A2C0FF"], 0x03, ["C0123", "B01A2", "U00FF"]),
])
def test_parse_dtc_response(lines, mode, expected):
    assert parse_dtc_response(lines, mode) == expected


@pytest.mark.parametrize("lines, expected", [
    (["4202000133"], ["P0133"]),
    (["4202000000"], []),
    (["NO DATA"], []),
])
def test_parse_freeze_frame_dtc(lines, expected):
    assert parse_freeze_frame_dtc(lines) == expected


def test_evap_vapor_pressure_alt_is_offset_not_scaled():
    # PID 0x54: (256A + B) - 32767 Pa
    assert decode_pid(1, 0x54, [0x80, 0x00])[1] == 1.0
    assert decode_pid(1, 0x54, [0x00, 0x00])[1] == -32767.0
