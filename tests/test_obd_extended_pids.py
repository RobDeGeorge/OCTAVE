"""Multi-value PIDs above 0x5E and VIN reading (backend/elm327_protocol.py,
parity twin of the C++ ELM327Protocol). Test vectors are ELM327 replies with
headers and spaces off; expected values follow SAE J1979 scaling."""

import re
from pathlib import Path

import pytest

from backend.elm327_protocol import (EXTENDED_PID_TABLE, decode_extended_pid, decode_vin,
                                     parse_response_lines, parse_vin)

REPO = Path(__file__).resolve().parents[1]


def _dec(pid, hexdata):
    return dict(decode_extended_pid(1, pid, list(bytes.fromhex(hexdata))))


@pytest.mark.parametrize("pid, data, expected", [
    (0x70, "030C800FA0000000000000", {"BOOST_PRESSURE_A_COMMANDED": 100.0, "BOOST_PRESSURE_A": 125.0}),
    (0x66, "0103200000", {"MAF_SENSOR_A": 25.0}),                      # sensor B not supported
    (0xA6, "0001E240", {"ODOMETER": 12345.6}),
    (0x7F, "0300000E100000012C00000000", {"ENGINE_RUN_TIME_TOTAL": 3600.0, "ENGINE_IDLE_TIME_TOTAL": 300.0}),
    (0x9A, "06005A00FF38", {"HYBRID_BATTERY_VOLTAGE": 360.0, "HYBRID_BATTERY_CURRENT": -20.0}),
    (0x61, "AF", {"DEMAND_ENGINE_TORQUE": 50.0}),
    (0x67, "015A00", {"COOLANT_TEMP_SENSOR_1": 50.0}),
    (0xA4, "02000DAC", {"TRANSMISSION_GEAR_RATIO": 3.5}),
    (0x69, "0780C064", {"EGR_A_COMMANDED": 50.2, "EGR_A_ACTUAL": 75.29, "EGR_A_ERROR": -21.88}),
    (0x70, "03", {}),                                                    # short reply
])
def test_decode_extended_pid(pid, data, expected):
    assert _dec(pid, data) == pytest.approx(expected)


def test_multi_frame_reply_is_joined():
    mode, pid, data = parse_response_lines(["00C", "0:41700301F4", "1:0FA00000000000"], 1, 0x70)
    assert (mode, pid) == (1, 0x70)
    assert _dec(0x70, bytes(data).hex()) == pytest.approx(
        {"BOOST_PRESSURE_A_COMMANDED": 15.63, "BOOST_PRESSURE_A": 125.0})


@pytest.mark.parametrize("lines", [
    ["014", "0:490201314A34", "1:46413439533033", "2:50313233343536"],                  # CAN
    ["49020100000031", "4902024A344641", "49020334395330", "49020433503132", "49020533343536"],  # pre-CAN
])
def test_parse_vin(lines):
    assert parse_vin(lines) == "1J4FA49S03P123456"


def test_parse_vin_no_data():
    assert parse_vin(["NO DATA"]) == ""


@pytest.mark.parametrize("vin, year, make", [
    ("1J4FA49S03P123456", 2003, "Jeep"),
    ("5YJ3E1EA7KF317000", 2019, "Tesla"),
    ("1HGCM82633A004352", 2003, "Honda"),
    ("BADVIN", 0, ""),
])
def test_decode_vin(vin, year, make):
    info = decode_vin(vin)
    assert (info["modelYear"], info["make"]) == (year, make)


def test_every_extended_parameter_is_wired_everywhere():
    """Each id needs a settings default (off) in both backends, QML metadata,
    and the same decoder in the C++ table."""
    ids = {sig[0] for sigs in EXTENDED_PID_TABLE.values() for sig in sigs}
    cpp = (REPO / "src/managers/elm327protocol.cpp").read_text()
    cpp_ids = set(re.findall(r'sig\("([A-Z0-9_]+)"', cpp))
    assert ids == cpp_ids

    cpp_settings = (REPO / "src/managers/settingsmanager.cpp").read_text()
    py_settings = (REPO / "backend/settings_manager.py").read_text()
    qml = (REPO / "frontend/OBDParameterModel.qml").read_text()
    for i in ids:
        assert f'"{i}"' in cpp_settings, i
        assert f'"{i}": False' in py_settings, i
        assert f'id: "{i}"' in qml, i
