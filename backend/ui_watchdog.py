"""Detects a hung Qt event loop and says so in the log. A worker thread
pings the GUI thread every ping_s; if the GUI thread has not answered within
stall_s a WARNING is logged (once per stall, with the duration when it
recovers), so a frozen head unit leaves a trace. Mirrors
src/util/uiwatchdog.{h,cpp}."""

import threading
import time

from PySide6.QtCore import QObject, Signal, Slot, Qt

from backend.logging_config import get_logger

logger = get_logger(__name__)


class UiWatchdog(QObject):
    _ping = Signal()   # worker -> GUI thread (queued)

    def __init__(self, parent=None, ping_s: float = 2.0, stall_s: float = 5.0):
        super().__init__(parent)
        self._ping_s = ping_s
        self._stall_s = stall_s
        self._last_pong = time.monotonic()
        self._running = False
        self._thread = None
        self._ping.connect(self._pong, Qt.ConnectionType.QueuedConnection)

    @Slot()
    def _pong(self):
        # Runs on the GUI thread when the event loop gets to it
        self._last_pong = time.monotonic()

    def start(self):
        if self._running:
            return
        self._running = True
        self._last_pong = time.monotonic()
        self._thread = threading.Thread(target=self._loop, name="ui-watchdog", daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        t = self._thread
        if t is not None and t is not threading.current_thread():
            t.join(timeout=self._ping_s + 0.5)
        self._thread = None

    def _loop(self):
        stalled = False
        stall_start = 0.0
        while self._running:
            time.sleep(self._ping_s)
            if not self._running:
                break
            try:
                self._ping.emit()
            except RuntimeError:
                # The QObject was torn down under us (shutdown without stop())
                break
            silent = time.monotonic() - self._last_pong
            if not stalled and silent > self._stall_s:
                stalled = True
                stall_start = self._last_pong
                logger.warning(f"UI thread blocked for {silent:.1f} s and counting")
            elif stalled and silent <= self._ping_s:
                stalled = False
                logger.warning(f"UI thread responsive again after {self._last_pong - stall_start:.1f} s")
