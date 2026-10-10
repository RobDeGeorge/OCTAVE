"""Phone mirror over Wi-Fi: serial classification, device choice, messages.

No phone or adb needed: the adb device list is stubbed.
"""

from backend.phone_mirror.manager import PhoneMirrorManager, host_of
from backend.phone_mirror.scrcpy_client import disconnected_message, is_network_serial

USB = "R5CT1234ABC"
TCP = "192.168.1.5:37199"
MDNS = "adb-R5CT1234ABC-AbCdEf._adb-tls-connect._tcp"


def _manager(devices):
    m = PhoneMirrorManager()
    m._devices = lambda: list(devices)
    m._maybe_reconnect_wireless = lambda: None
    return m


def test_network_serials():
    assert not is_network_serial(USB)
    assert not is_network_serial("emulator-5554")
    assert is_network_serial(TCP)
    assert is_network_serial("[fe80::1]:5555")
    assert is_network_serial(MDNS)


def test_host_of():
    assert host_of(TCP) == "192.168.1.5"
    assert host_of("[fe80::1]:5555") == "fe80::1"
    assert host_of("192.168.1.5") == "192.168.1.5"


def test_usb_preferred_over_wifi():
    assert _manager([(TCP, "device"), (USB, "device")]).getDeviceSerial() == USB
    assert _manager([(TCP, "device"), (USB, "unauthorized")]).getDeviceSerial() == TCP
    assert _manager([(MDNS, "device")]).getDeviceSerial() == MDNS
    assert _manager([]).getDeviceSerial() == ""


def test_wifi_messages_keep_retry_phrases():
    # PhoneMirrorView auto-retries on /disconnected|No Android device|not authorized|offline/
    m = _manager([(TCP, "offline")])
    assert "Wi-Fi" in m._describe_problem("offline") and "offline" in m._describe_problem("offline")
    m = _manager([(TCP, "unauthorized")])
    assert "not authorized" in m._describe_problem("unauthorized")
    m = _manager([(USB, "offline")])
    assert "cable" in m._describe_problem("offline")
    m = _manager([])
    m._wireless_address = TCP
    msg = m._describe_problem("none")
    assert msg.startswith("No Android device") and TCP in msg
    assert "Wi-Fi" in disconnected_message(TCP) and "disconnected" in disconnected_message(TCP)
    assert "USB" in disconnected_message(USB)


def test_qr_pairing_payload_and_cancel():
    from backend.phone_mirror.manager import qr_pairing_payload

    assert qr_pairing_payload("octave-abc", "pw123") == "WIFI:T:ADB;S:octave-abc;P:pw123;;"
    m = _manager([])
    m._adb_path = "adb"                       # no adb is run: the wait job is not started
    m._run_wireless_job = lambda job, ui: None
    m.startQrPairing()
    rows = m.qrPairingRows
    assert m.qrPairingActive and len(rows) >= 21 and all(len(r) == len(rows) for r in rows)
    assert set("".join(rows)) <= {"0", "1"}
    # Finder pattern in the top-left corner: a 7-module dark ring
    assert rows[0][:7] == "1111111" and rows[6][:7] == "1111111"
    m.cancelQrPairing()
    assert not m.qrPairingActive and m.qrPairingRows == []
