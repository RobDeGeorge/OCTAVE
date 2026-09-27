"""ESP32 serial reader: a partial line with no newline must not grow forever."""

import logging

from backend import esp32_volume_manager as esp
from backend.esp32_volume_manager import ESP32_MAX_LINE_BYTES, ESP32VolumeManager


class _FakeSerial:
    """Feeds canned readline() chunks, then stops the read loop."""

    def __init__(self, mgr, chunks):
        self._mgr = mgr
        self._chunks = list(chunks)

    def readline(self):
        if not self._chunks:
            self._mgr._stop_thread = True
            return b""
        return self._chunks.pop(0)


def _run(mgr, chunks):
    commands = []
    mgr._process_command = commands.append
    mgr._stop_thread = False
    mgr._serial_connection = _FakeSerial(mgr, chunks)
    mgr._read_loop()
    return commands


def test_garbage_without_newline_is_dropped_and_warned_once(qapp, caplog):
    mgr = ESP32VolumeManager()
    garbage = [b"x" * 100] * 20  # 2000 bytes, never a newline
    with caplog.at_level(logging.WARNING, logger=esp.logger.name):
        # The "+" ends the noisy line, so it is discarded with it; the reader
        # resyncs on that newline and the next line parses. A second overflow
        # on the same connection is dropped silently.
        commands = _run(mgr, garbage + [b"+\n", b"-\n"] + garbage + [b"\n+\n"])
    assert commands == ["-", "+"]
    drops = [r for r in caplog.records if "no newline" in r.getMessage()]
    assert len(drops) == 1


def test_short_partial_lines_are_joined(qapp):
    mgr = ESP32VolumeManager()
    commands = _run(mgr, [b"VOL", b":42\n", b"-\n"])
    assert commands == ["VOL:42", "-"]


def test_partial_line_at_limit_is_kept(qapp):
    mgr = ESP32VolumeManager()
    body = b"a" * ESP32_MAX_LINE_BYTES
    commands = _run(mgr, [body, b"\n"])
    assert commands == [body.decode()]
