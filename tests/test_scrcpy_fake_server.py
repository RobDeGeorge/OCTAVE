"""The Python phone-mirror client against a fake scrcpy 3.x server
(tests/fake_scrcpy_server.py) over OCTAVE_SCRCPY_PORT: handshake, config and
key packets, a mid-stream resolution change, a corrupt packet, an early exit."""

import os
import struct
import subprocess
import threading
import time

import pytest
from PySide6.QtCore import Qt

from backend.phone_mirror import scrcpy_client as sc
from tests import fake_scrcpy_server as fake

W, H = 64, 48       # first stream
W2, H2 = 96, 64     # after the "rotation" (new SPS)


class _FakeShell:
    """Stands in for the adb shell running the server: alive until terminated."""

    def __init__(self):
        r, self._w = os.pipe()
        self.stdout = os.fdopen(r, "rb")
        self._done = threading.Event()

    def poll(self):
        return 0 if self._done.is_set() else None

    def terminate(self):
        if not self._done.is_set():
            self._done.set()
            os.close(self._w)

    kill = terminate

    def wait(self, timeout=None):
        return 0


@pytest.fixture
def streams():
    if not sc.HAVE_AV:
        pytest.skip("PyAV not installed")
    try:
        return fake.encode_h264(W, H), fake.encode_h264(W2, H2)
    except Exception as e:   # PyAV build without an H.264 encoder
        pytest.skip(f"no H.264 encoder: {e}")


@pytest.fixture
def run_client(qapp, monkeypatch):
    clients = []

    def start(server):
        monkeypatch.setenv("OCTAVE_SCRCPY_PORT", str(server.port))
        c = sc.ScrcpyClient("adb", "/nonexistent.jar")
        monkeypatch.setattr(c, "_adb_run",
                            lambda args, timeout=15: subprocess.CompletedProcess(args, 0, stdout="", stderr=""))
        monkeypatch.setattr(sc.subprocess, "Popen", lambda *a, **k: _FakeShell())
        c.reasons = []
        c.sizes = []
        # Direct: the session thread emits, and no Qt event loop runs here
        c.disconnected.connect(c.reasons.append, Qt.ConnectionType.DirectConnection)
        c.frameSizeChanged.connect(lambda w, h: c.sizes.append((w, h)), Qt.ConnectionType.DirectConnection)
        clients.append(c)
        assert c.start("SERIAL")
        return c

    yield start
    for c in clients:
        c.stop()


def _until(cond, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.02)
    return cond()


def _script(stream):
    config, packets = stream
    return [("config", config)] + [("frame", p, k) for p, k in packets]


def test_handshake_and_frames(streams, run_client):
    first, _ = streams
    with fake.FakeScrcpyServer(_script(first), W, H) as srv:
        c = run_client(srv)
        assert _until(lambda: c.frame_count >= len(first[1]) - 1), c.frame_count
        assert c.is_running
        assert c.frame_size == (W, H)
        assert c.reasons == []
        # Control socket is live: a touch reaches the server as a 32-byte message
        c.inject_touch(0, sc.ACTION_DOWN, 10, 20)
        assert _until(lambda: len(srv.control_bytes) >= 32)
        assert srv.control_bytes[0] == sc.MSG_INJECT_TOUCH_EVENT


def test_mid_stream_sps_change_resizes(streams, run_client):
    first, second = streams
    with fake.FakeScrcpyServer(_script(first) + _script(second), W, H) as srv:
        c = run_client(srv)
        assert _until(lambda: c.frame_size == (W2, H2)), c.frame_size
        assert (W2, H2) in c.sizes
        assert _until(lambda: c.frame_count >= len(first[1]) + len(second[1]) - 2), c.frame_count
        assert c.is_running and c.reasons == []


def test_corrupt_packet_is_skipped(streams, run_client):
    first, second = streams
    garbage = b"\x00\x00\x00\x01\x65" + bytes(range(256)) * 4   # an IDR slice header followed by noise
    script = _script(first) + [("frame", garbage, True)] + _script(second)
    with fake.FakeScrcpyServer(script, W, H) as srv:
        c = run_client(srv)
        assert _until(lambda: c.frame_size == (W2, H2)), (c.frame_size, c.reasons)
        assert c.is_running and c.reasons == []


def test_out_of_sync_header_ends_session(streams, run_client):
    first, _ = streams
    bad = struct.pack(">QI", 0, sc.MAX_VIDEO_PACKET_BYTES + 1)
    with fake.FakeScrcpyServer(_script(first) + [("raw", bad)], W, H) as srv:
        c = run_client(srv)
        assert _until(lambda: c.reasons), "no disconnect"
        assert "out of sync" in c.reasons[0]


def test_server_exit_mid_stream_reports_disconnect(streams, run_client):
    first, _ = streams
    with fake.FakeScrcpyServer(_script(first) + [("close",)], W, H) as srv:
        c = run_client(srv)
        assert _until(lambda: c.reasons), "no disconnect"
        assert c.reasons == [sc.disconnected_message("SERIAL")]
        assert not c.is_running


def test_server_exit_during_handshake(run_client, streams):
    with fake.FakeScrcpyServer([], W, H, handshake_close=True) as srv:
        c = run_client(srv)
        assert _until(lambda: c.reasons), "no disconnect"
        assert not c.is_running
        assert c.frame_count == 0
