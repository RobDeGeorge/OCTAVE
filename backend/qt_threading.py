"""Run a callable on the Qt main thread from any thread.

``QTimer.singleShot`` called from a plain ``threading.Thread`` never fires:
the timer belongs to the calling thread, which has no Qt event loop. Worker
threads (OBD monitor/scan, sensor read loops, ESP32 serial reader) must use
``run_on_main`` instead to hand work back to the GUI thread.
"""

import threading

from PySide6.QtCore import QCoreApplication, QObject, Qt, QTimer, Signal, Slot


class _MainThreadInvoker(QObject):
    _invoke = Signal(object, int)

    def __init__(self):
        super().__init__()
        self._invoke.connect(self._run, Qt.QueuedConnection)

    @Slot(object, int)
    def _run(self, fn, delay_ms):
        if delay_ms > 0:
            QTimer.singleShot(delay_ms, fn)
        else:
            fn()


_invoker = None
_invoker_lock = threading.Lock()


def _get_invoker():
    global _invoker
    with _invoker_lock:
        if _invoker is None:
            # Created on the main thread it already lives there. Only a first
            # call from a worker has to move it, and that is the one path that
            # asks Qt for a QThread off the main thread: PySide6 6.11.2 can
            # later delete the main QThread through such a wrapper and
            # segfault at exit. The module-level call below avoids it.
            _invoker = _MainThreadInvoker()
            if threading.current_thread() is not threading.main_thread():
                app = QCoreApplication.instance()
                if app is not None:
                    _invoker.moveToThread(app.thread())
        return _invoker


def run_on_main(fn, delay_ms=0):
    """Queue ``fn`` to run on the main thread after ``delay_ms`` milliseconds.

    Safe to call from any thread. From the main thread it behaves like
    ``QTimer.singleShot(delay_ms, fn)``.
    """
    _get_invoker()._invoke.emit(fn, int(delay_ms))


# Managers import this module on the main thread at startup, so the invoker
# is built there before any worker thread can call run_on_main().
if threading.current_thread() is threading.main_thread():
    _get_invoker()
