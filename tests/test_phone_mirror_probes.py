"""Phone mirror adb probes and server-log rules, as tables: `adb devices`
parsing, the state the view shows, which phone a session picks, and how
phone-server log lines are classified. Pure functions; no adb, no threads.
Mirrors the C++ statics in src/managers/phonemirrormanager.cpp."""

import pytest

from backend.phone_mirror.manager import PhoneMirrorManager as M

USB = "R5CT1234ABC"
USB2 = "1A2B3C4D"
TCP = "192.168.1.5:37199"
MDNS = "adb-R5CT1234ABC-AbCdEf._adb-tls-connect._tcp"


@pytest.mark.parametrize("output, rows", [
    ("List of devices attached\n", []),
    ("", []),
    (f"List of devices attached\n{USB}\tdevice\n", [(USB, "device")]),
    (f"List of devices attached\n{USB}\tunauthorized\n", [(USB, "unauthorized")]),
    (f"List of devices attached\n{USB}\toffline\n", [(USB, "offline")]),
    # adb starting its server first
    ("* daemon not running; starting now at tcp:5037\n* daemon started successfully\n"
     f"List of devices attached\n{USB}\tdevice\n", [(USB, "device")]),
    # Linux without udev rules
    ("List of devices attached\n"
     f"{USB}\tno permissions (user in plugdev group; are your udev rules wrong?); "
     "see [http://developer.android.com/tools/device.html]\n", [(USB, "no-permissions")]),
    # mDNS auto-connected phone, plus `adb connect` ip:port
    (f"List of devices attached\n{MDNS}\tdevice\n{TCP}\toffline\n", [(MDNS, "device"), (TCP, "offline")]),
    # `adb devices -l` style extra columns, CRLF from Windows adb
    (f"List of devices attached\r\n{USB}       device usb:1-1 product:x model:SM_S918B\r\n",
     [(USB, "device")]),
    # adb server version clash noise
    ("adb server version (41) doesn't match this client (39); killing...\n"
     "* daemon started successfully\nList of devices attached\n"
     f"{USB}\tdevice\n", [(USB, "device")]),
    ("adb: failed to check server version: cannot connect to daemon\n", []),
])
def test_parse_devices(output, rows):
    assert M.parseDevices(output) == rows


@pytest.mark.parametrize("rows, state", [
    ([], "none"),
    ([(USB, "device")], "device"),
    ([(USB, "offline"), (TCP, "device")], "device"),
    ([(USB, "offline"), (USB2, "unauthorized")], "unauthorized"),
    ([(USB, "offline"), (USB2, "no-permissions")], "no-permissions"),
    ([(USB, "no-permissions"), (USB2, "unauthorized")], "unauthorized"),
    ([(TCP, "offline")], "offline"),
    ([(USB, "recovery")], "none"),
])
def test_summarize_state(rows, state):
    assert M.summarizeState(rows) == state


@pytest.mark.parametrize("rows, preferred, serial", [
    ([], "", ""),
    ([(USB, "device")], "", USB),
    ([(TCP, "device"), (USB, "device")], "", USB),            # USB first
    ([(TCP, "device"), (USB, "unauthorized")], "", TCP),
    ([(MDNS, "device"), (TCP, "device")], "", MDNS),          # first network phone
    ([(USB, "device"), (USB2, "device")], "", USB),
    ([(USB, "device"), (USB2, "device")], USB2, USB2),        # picked in the UI
    ([(USB, "device"), (TCP, "device")], TCP, TCP),
    ([(USB, "device"), (USB2, "offline")], USB2, USB),        # picked phone not usable
    ([(USB, "offline")], USB, ""),
])
def test_pick_serial(rows, preferred, serial):
    assert M.pickSerial(rows, preferred) == serial


@pytest.mark.parametrize("line, kind", [
    ("[server] ERROR: Could not inject event: java.lang.SecurityException: Injecting input events requires "
     "the caller (or the source of the instrumentation, if any) to have the INJECT_EVENTS permission.", "inject"),
    ("[server] WARN: Audio disabled: it is not supported before Android 11", "no-audio"),
    ("[server] ERROR: Capture/encoding error: android.media.MediaCodec$CodecException: Error 0xfffffff4", "encoder"),
    ("[server] ERROR: Video encoding error", "encoder"),
    ("[server] ERROR: Could not create default video encoder for h264", "encoder"),
    ("[server] ERROR: Could not create video encoder", "encoder"),
    ("[server] ERROR: Video encoder 'c2.foo.avc.encoder' for h264 not found", "encoder"),
    ("[server] INFO: Device: [samsung] samsung SM-S918B (Android 16)", ""),
    ("[server] INFO: New display: 1280x800/160 (id=12)", ""),
    ("Audio disabled by the user", ""),
    ("", ""),
])
def test_classify_server_line(line, kind):
    assert M.classifyServerLine(line) == kind


@pytest.mark.parametrize("sdk, encoder", [
    (0, "c2.android.avc.encoder"),       # unknown
    (-1, "c2.android.avc.encoder"),
    (28, "OMX.google.h264.encoder"),     # Android 9
    (29, "c2.android.avc.encoder"),      # Android 10
    (36, "c2.android.avc.encoder"),
])
def test_software_encoder_for(sdk, encoder):
    assert M.softwareEncoderFor(sdk) == encoder


def test_notice_texts():
    for kind in ("inject", "no-audio", "software-encoder"):
        assert M.noticeText(kind)
    assert M.noticeText("encoder") == ""
    assert "Xiaomi" in M.noticeText("inject")
    assert "no permissions" not in M.describeDeviceState("no-permissions")
    assert "udev" in M.describeDeviceState("no-permissions")


# ── the async flow: probes run on the probe thread, results land on the GUI thread ──

def _spin(qapp, cond, timeout=3.0):
    import time
    end = time.monotonic() + timeout
    while time.monotonic() < end and not cond():
        qapp.processEvents()
        time.sleep(0.01)
    return cond()


@pytest.fixture
def manager(qapp, monkeypatch):
    import threading
    m = M()
    m._adb_path = "adb"
    m._maybe_reconnect_wireless = lambda: None
    m.probes = []
    m.gate = threading.Event()
    m.gate.set()
    m.rows = []

    def fake_probe():
        m.gate.wait(3)
        m.probes.append(threading.current_thread().name)
        return {"rows": list(m.rows), "state": M.summarizeState(m.rows), "hint": "", "models": {}}

    m._probe_devices = fake_probe
    m._run_adb_full = lambda args, timeout=10: ""
    yield m
    m.gate.set()
    # Let pending probe results and retry timers land while the manager is
    # alive, then delete it here rather than at some later test's GC, where a
    # late Qt callback into a half-collected manager crashed CI (exit 139).
    _spin(qapp, lambda: False, 0.5)
    m.stopScrcpy()
    m.deleteLater()
    qapp.processEvents()


def test_get_device_state_never_blocks_and_coalesces(qapp, manager):
    manager.rows = [(USB, "unauthorized")]
    manager.gate.clear()                     # the first probe hangs, like a dead adb
    changes = []
    manager.deviceStateChanged.connect(lambda: changes.append(manager.deviceState))
    assert manager.getDeviceState() == "none"    # cached, immediately
    for _ in range(5):
        manager.getDeviceState()                 # asked again mid-probe: one re-run, not five
    manager.gate.set()
    assert _spin(qapp, lambda: manager.deviceState == "unauthorized")
    assert _spin(qapp, lambda: not manager._probe_running and len(manager.probes) == 2)
    qapp.processEvents()
    assert len(manager.probes) == 2
    assert all(name == "phone-mirror-probe" for name in manager.probes)
    assert changes == ["unauthorized"]


def test_start_without_phone_reports_off_the_gui_thread(qapp, manager, monkeypatch):
    monkeypatch.setattr(M, "nativeAvailable", property(lambda self: True))
    manager.rows = [(USB, "no-permissions")]
    errors = []
    manager.scrcpyError.connect(errors.append)
    manager.startScrcpy()
    assert manager._is_starting                  # returned at once, the probe is pending
    assert _spin(qapp, lambda: errors)
    assert "USB permissions" in errors[0]
    assert manager.deviceState == "no-permissions"
    assert not manager._is_starting


def test_stop_while_probing_drops_the_start(qapp, manager, monkeypatch):
    monkeypatch.setattr(M, "nativeAvailable", property(lambda self: True))
    manager.rows = [(USB, "device")]
    manager.gate.clear()
    started = []
    monkeypatch.setattr(manager, "_set_connection_type", lambda kind: started.append(kind))
    manager.startScrcpy()
    manager.stopScrcpy()
    manager.gate.set()
    assert _spin(qapp, lambda: manager.probes)
    for _ in range(20):
        qapp.processEvents()
    assert "usb" not in started and manager._client is None


def test_devices_list_and_notices(qapp, manager):
    manager._apply_probe({"rows": [(USB, "device"), (TCP, "device")], "state": "device", "hint": "",
                          "models": {USB: "SM-S918B", TCP: ""}})
    rows = manager.devices
    assert [r["serial"] for r in rows] == [USB, TCP]
    assert rows[0]["model"] == "SM-S918B" and not rows[0]["wireless"] and rows[1]["wireless"]
    assert not any(r["selected"] for r in rows)
    # Models are kept when a later probe (one phone) does not fetch them
    manager._apply_probe({"rows": [(USB, "device")], "state": "device", "hint": "", "models": {}})
    assert manager.devices[0]["model"] == "SM-S918B"
    manager._on_server_log("[server] WARN: Audio disabled: it is not supported before Android 11")
    manager._on_server_log("[server] WARN: Audio disabled: it is not supported before Android 11")
    assert manager.mirrorNotice == M.noticeText("no-audio")
    manager._on_server_log("[server] ERROR: Capture/encoding error: boom")
    assert manager._fatal_cause == "encoder" and manager.mirrorNotice == M.noticeText("no-audio")
    manager.dismissNotice()
    assert manager.mirrorNotice == ""


def test_encoder_failure_retries_once_in_software(qapp, manager, monkeypatch):
    import time
    restarts = []
    monkeypatch.setattr(manager, "startScrcpy", lambda: restarts.append(1))
    errors = []
    manager.scrcpyError.connect(errors.append)
    manager._serial = USB
    manager._session_sdk = 34
    manager._fatal_cause, manager._fatal_cause_at = "encoder", time.monotonic()
    manager._on_disconnected("Phone disconnected. Reconnect the USB cable.")
    assert manager._software_encoder_serial == USB and errors == []
    assert _spin(qapp, lambda: restarts)
    # Software encoder failed too: a final error, not the retry-forever "disconnected"
    manager._fatal_cause, manager._fatal_cause_at = "encoder", time.monotonic()
    manager._on_disconnected("Phone disconnected. Reconnect the USB cable.")
    assert len(errors) == 1 and "even in software mode" in errors[0]
    # An old encoder line is not the cause of a later link drop
    manager._fatal_cause, manager._fatal_cause_at = "encoder", time.monotonic() - 10
    manager._on_disconnected("Phone disconnected. Reconnect the USB cable.")
    assert errors[-1] == "Phone disconnected. Reconnect the USB cable."
