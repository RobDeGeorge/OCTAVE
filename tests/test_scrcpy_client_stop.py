"""Stopping the phone mirror while it is still connecting must not strand the
adb forward or the adb shell running the server (they used to live until
OCTAVE exited)."""

import subprocess
import threading
import time

import pytest

from backend.phone_mirror import scrcpy_client as sc


class _FakeProc:
    def __init__(self):
        self.terminated = threading.Event()
        self.stdout = None

    def poll(self):
        return 0 if self.terminated.is_set() else None

    def terminate(self):
        self.terminated.set()

    def wait(self, timeout=None):
        return 0

    def kill(self):
        self.terminated.set()


@pytest.fixture
def client(qapp, monkeypatch):
    if not sc.HAVE_AV:
        pytest.skip("PyAV not installed")
    c = sc.ScrcpyClient("adb", "/nonexistent.jar")
    calls = []
    lock = threading.Lock()
    slow_step = {"name": None}

    def fake_adb(args, timeout=15):
        with lock:
            calls.append(list(args))
        if slow_step["name"] and args[:1] == [slow_step["name"]] and "--remove" not in args \
                and "--list" not in args:
            time.sleep(0.5)
        return subprocess.CompletedProcess(args, 0, stdout="", stderr="")

    monkeypatch.setattr(c, "_adb_run", fake_adb)
    c.calls = calls
    c.slow_step = slow_step
    yield c
    c.stop()


def _wait_thread(c):
    if c._thread:
        c._thread.join(timeout=5)
        assert not c._thread.is_alive()


def test_stop_during_push_leaves_no_forward(client):
    client.slow_step["name"] = "push"   # stop() lands before the forward exists
    assert client.start("SERIAL")
    time.sleep(0.1)
    client.stop()
    _wait_thread(client)

    created = {a[1] for a in client.calls if a[:1] == ["forward"] and a[1].startswith("tcp:")}
    removed = {a[2] for a in client.calls if a[:2] == ["forward", "--remove"]}
    assert created <= removed, f"stranded forwards: {created - removed}"
    assert client._port == 0


def test_stop_during_server_start_ends_the_adb_shell(client, monkeypatch):
    procs = []

    def fake_popen(*args, **kwargs):
        # Spawning is slow: stop() runs and finishes its own release while
        # the shell is still starting, then the process appears
        deadline = time.monotonic() + 2
        while not client._stopping and time.monotonic() < deadline:
            time.sleep(0.01)
        time.sleep(0.3)
        p = _FakeProc()
        procs.append(p)
        return p

    monkeypatch.setattr(sc.subprocess, "Popen", fake_popen)
    assert client.start("SERIAL")
    time.sleep(0.1)
    client.stop()
    _wait_thread(client)

    assert procs, "the session should have spawned the adb shell"
    assert procs[0].terminated.is_set(), "adb shell left running after stop()"
    created = {a[1] for a in client.calls if a[:1] == ["forward"] and a[1].startswith("tcp:")}
    removed = {a[2] for a in client.calls if a[:2] == ["forward", "--remove"]}
    assert created <= removed, f"stranded forwards: {created - removed}"
