"""Demand-driven OBD polling: consumer demand -> poll set mapping.

compute_poll_params() is the Python mirror of C++ OBDManager::buildPidsToWatch();
these pin the rules both backends implement (union of consumers, IMU ids
ignored, supported-set filter only once a scan produced a result, RPM
heartbeat with ELM_VOLTAGE fallback, multi-value PIDs polled when any of
their signals is wanted).
"""

from backend.elm327_protocol import EXTENDED_PID_TABLE
from backend.obd_manager import IMU_PARAM_IDS, compute_poll_params

BASE = ["SPEED", "RPM", "COOLANT_TEMP", "ELM_VOLTAGE", "FUEL_LEVEL"]
EXT_KEY, EXT_SIGNALS = next(iter(EXTENDED_PID_TABLE.items()))
EXT_PARAM = EXT_SIGNALS[0][0]


def test_no_demand_polls_only_the_heartbeat():
    assert compute_poll_params({}, None, BASE) == (["RPM"], [])


def test_union_of_consumers():
    base, ext = compute_poll_params(
        {"dashboard": ["SPEED"], "cards": ["COOLANT_TEMP", "SPEED"]}, None, BASE)
    assert base == ["COOLANT_TEMP", "RPM", "SPEED"]
    assert ext == []


def test_imu_and_unknown_ids_are_ignored():
    demand = {"dashboard": list(IMU_PARAM_IDS) + ["NOT_A_PID"]}
    assert compute_poll_params(demand, None, BASE) == (["RPM"], [])


def test_supported_set_filters_demand():
    base, _ = compute_poll_params(
        {"cards": ["SPEED", "FUEL_LEVEL"]}, ["SPEED", "RPM"], BASE)
    assert base == ["RPM", "SPEED"]


def test_empty_scan_result_filters_nothing():
    base, _ = compute_poll_params({"cards": ["FUEL_LEVEL"]}, [], BASE)
    assert base == ["FUEL_LEVEL", "RPM"]


def test_heartbeat_falls_back_to_elm_voltage_without_rpm():
    base, _ = compute_poll_params({}, ["SPEED", "ELM_VOLTAGE"], BASE)
    assert base == ["ELM_VOLTAGE"]


def test_extended_pid_polled_when_any_signal_wanted():
    _, ext = compute_poll_params({"dashboard": [EXT_PARAM]}, None, BASE)
    assert ext == [EXT_KEY]
    _, ext = compute_poll_params({"dashboard": [EXT_PARAM]}, ["RPM"], BASE)
    assert ext == []  # vehicle didn't report it
