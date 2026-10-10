"""OBDAdapterFinder logic that needs no Bluetooth hardware: which devices
count as OBD adapters, list order, and the hand-off to OBDManager after
pairing (connect_direct + a named saved-adapter chip, then the outcome read
from connectionStatusChanged)."""

from PySide6.QtCore import QObject, Signal

from backend import obd_adapter_finder as finder_mod
from backend.obd_adapter_finder import OBDAdapterFinder, looks_like_obd

MAC = "88:1B:99:66:DD:5F"


class FakeObd(QObject):
    connectionStatusChanged = Signal(str)

    def __init__(self):
        super().__init__()
        self.direct = []

    def connect_direct(self, mac, channel=1):
        self.direct.append((mac, channel))


class FakeSettings:
    def __init__(self):
        self.saved = []

    def add_obd_saved_adapter(self, name, mac):
        self.saved.append((name, mac))


def _finder(monkeypatch, obd=None, settings=None):
    # No QBluetoothLocalDevice / discovery agent / D-Bus in tests
    monkeypatch.setattr(OBDAdapterFinder, "_init_bluetooth", lambda self: None)
    return OBDAdapterFinder(obd, settings)


def test_common_adapter_names_are_recognised():
    for name in ("OBDII", "OBD2", "V-LINK", "Android-Vlink", "OBDLink MX+", "OBDLink CX",
                 "Vgate iCar Pro", "VEEPEAK", "BAFX Products", "KONNWEI", "NEXAS23082749",
                 "LELink", "ELM327 v1.5", "Viecar", "KW902"):
        assert looks_like_obd(name), name
    for name in ("Washer", "Xbox Wireless Controller", "MX Master 2S", "VIZIO V21", "Pixel 8"):
        assert not looks_like_obd(name), name


def test_ble_service_uuid_marks_an_unrecognised_name():
    assert looks_like_obd("Y1csie", ["{0000FFF0-0000-1000-8000-00805F9B34FB}"])
    assert looks_like_obd("HM10", ["49535343-fe7d-4ae5-8fa9-9fafd205e455"])
    assert not looks_like_obd("Y1csie", ["0000180A-0000-1000-8000-00805F9B34FB"])


def test_list_sorts_adapters_first_then_by_signal():
    rows = [
        {"name": "Dryer", "rssi": -50, "likelyObd": False},
        {"name": "OBDII", "rssi": 0, "likelyObd": True},          # cached, not heard
        {"name": "NEXAS", "rssi": -88, "likelyObd": True},
        {"name": "Washer", "rssi": -70, "likelyObd": False},
    ]
    ordered = [r["name"] for r in sorted(rows, key=finder_mod._sort_key)]
    assert ordered == ["NEXAS", "OBDII", "Dryer", "Washer"]


def test_hand_off_connects_direct_and_names_the_saved_chip(qapp, monkeypatch):
    obd, settings = FakeObd(), FakeSettings()
    f = _finder(monkeypatch, obd, settings)
    f._busy_address, f._busy_name = MAC, "NEXAS23082749"
    f._hand_off(2)
    assert obd.direct == [(MAC, 2)]
    assert settings.saved == [("NEXAS23082749", f"rfcomm://{MAC}/2")]
    assert f.busyAddress == MAC
    assert f.status == "Connecting to the vehicle…"


def test_outcome_waits_for_the_attempt_to_start(qapp, monkeypatch):
    obd = FakeObd()
    f = _finder(monkeypatch, obd, FakeSettings())
    f._busy_address, f._busy_name = MAC, "NEXAS"
    f._hand_off(1)

    # force_connect() tears the old link down first; that isn't the result
    obd.connectionStatusChanged.emit("Disconnected")
    assert f.busyAddress == MAC

    obd.connectionStatusChanged.emit("Connecting")
    obd.connectionStatusChanged.emit("Connected")
    assert f.busyAddress == ""
    assert f.status == "Connected to NEXAS"
    assert not f.statusIsError


def test_failed_connection_is_reported_as_error(qapp, monkeypatch):
    obd = FakeObd()
    f = _finder(monkeypatch, obd, FakeSettings())
    f._busy_address, f._busy_name = MAC, "NEXAS"
    f._hand_off(1)
    obd.connectionStatusChanged.emit("Connecting")
    obd.connectionStatusChanged.emit("Connection Failed")
    assert f.busyAddress == ""
    assert f.statusIsError
    assert "Connection Failed" in f.status


def test_unsupported_platform_is_inert(qapp, monkeypatch):
    f = _finder(monkeypatch)
    assert not f.supported           # no discovery agent
    f.startScan()
    f.connectAdapter(MAC)
    assert f.devices == [] and f.busyAddress == "" and not f.scanning


def test_pairing_failure_still_tries_a_trusted_connection(qapp, monkeypatch):
    obd = FakeObd()
    f = _finder(monkeypatch, obd, FakeSettings())
    monkeypatch.setattr(f, "_look_up_channel", lambda: f._hand_off(1))
    f._busy_address, f._busy_name = MAC, "NEXAS"
    f._on_paired(False, True, "Pairing timed out.")
    assert obd.direct == [(MAC, 1)]
    assert f.status == "Couldn't pair; trying to connect anyway…"

    # If that connection fails too, the pairing error is the useful message
    obd.connectionStatusChanged.emit("Connecting")
    obd.connectionStatusChanged.emit("Connection Failed")
    assert f.status == "Pairing timed out." and f.statusIsError


def test_unreachable_adapter_stops_at_pairing(qapp, monkeypatch):
    obd = FakeObd()
    f = _finder(monkeypatch, obd, FakeSettings())
    f._busy_address, f._busy_name = MAC, "NEXAS"
    f._on_paired(False, False, "The adapter didn't answer.")
    assert obd.direct == [] and f.busyAddress == "" and f.statusIsError
