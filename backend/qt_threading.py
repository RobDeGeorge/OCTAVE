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
            _invoker = _MainThreadInvoker()
            app = QCoreApplication.instance()
            if app is not None and _invoker.thread() is not app.thread():
                _invoker.moveToThread(app.thread())
        return _invoker


def run_on_main(fn, delay_ms=0):
    """Queue ``fn`` to run on the main thread after ``delay_ms`` milliseconds.

    Safe to call from any thread. From the main thread it behaves like
    ``QTimer.singleShot(delay_ms, fn)``.
    """
    _get_invoker()._invoke.emit(fn, int(delay_ms))
