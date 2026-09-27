"""connect_to_adapter(MAC) on Linux must not run `rfcomm bind` on the GUI
thread: the slot returns at once, and the result (log lines, then either
force_connect() or the Error status) arrives later on the main thread."""

import threading
import time

from PySide6.QtCore import QThread

from backend.obd_manager import OBDManager

MAC = "AA:BB:CC:DD:EE:FF"


def _pump_until(qapp, predicate, timeout=3.0):
    deadline = time.monotonic() + timeout
    while not predicate() and time.monotonic() < deadline:
        qapp.processEvents()
        time.sleep(0.005)
    return predicate()


def _manager(monkeypatch, bind_result):
    mgr = OBDManager()
    monkeypatch.setattr(mgr, "_get_platform", lambda: "linux")
    release = threading.Event()

    def slow_bind(mac, log):
        assert QThread.currentThread() is not mgr.thread()
        log(f"rfcomm: binding {mac} → /dev/rfcomm0")
        release.wait(2.0)
        return bind_result

    monkeypatch.setattr(mgr, "_ensure_rfcomm_bound", slow_bind)
    events = []
    mgr.connectionLogLineAppended.connect(lambda line: events.append(("log", line)))
    mgr.connectionStatusChanged.connect(lambda s: events.append(("status", s)))
    monkeypatch.setattr(
        mgr, "force_connect",
        lambda: events.append(("force_connect", QThread.currentThread() is mgr.thread())))
    return mgr, release, events


def test_bind_success_connects_on_main_thread(qapp, monkeypatch):
    mgr, release, events = _manager(monkeypatch, "/dev/rfcomm0")

    start = time.monotonic()
    mgr.connect_to_adapter(MAC)
    assert time.monotonic() - start < 0.5, "slot blocked on the bind"
    assert not any(e[0] == "force_connect" for e in events)

    release.set()
    assert _pump_until(qapp, lambda: any(e[0] == "force_connect" for e in events))
    kinds = [e[0] for e in events]
    # connect log line, bind log line, then the connect — in that order
    assert kinds == ["log", "log", "force_connect"]
    assert events[-1] == ("force_connect", True)


def test_bind_failure_reports_error(qapp, monkeypatch):
    mgr, release, events = _manager(monkeypatch, None)
    mgr.connect_to_adapter(MAC)
    release.set()
    assert _pump_until(qapp, lambda: ("status", "Error") in events)
    assert not any(e[0] == "force_connect" for e in events)


def test_superseded_bind_is_ignored(qapp, monkeypatch):
    mgr, release, events = _manager(monkeypatch, "/dev/rfcomm0")
    finished = []
    real_finish = mgr._finish_rfcomm_bind

    def finish(generation, bound):
        real_finish(generation, bound)
        finished.append(bound)

    monkeypatch.setattr(mgr, "_finish_rfcomm_bind", finish)
    mgr.connect_to_adapter(MAC)
    # A newer request (a plain serial port) wins before the bind finishes
    mgr.connect_to_adapter("/dev/ttyUSB0")
    release.set()
    assert _pump_until(qapp, lambda: finished)
    assert [e for e in events if e[0] == "force_connect"] == [("force_connect", True)]
