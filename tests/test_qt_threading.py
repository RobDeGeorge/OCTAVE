import threading
import time

from PySide6.QtCore import QThread

from backend.qt_threading import run_on_main


def _pump_until(qapp, predicate, timeout=2.0):
    deadline = time.monotonic() + timeout
    while not predicate() and time.monotonic() < deadline:
        qapp.processEvents()
        time.sleep(0.005)
    return predicate()


def test_run_on_main_from_python_thread(qapp):
    ran_on = []
    main_thread = qapp.thread()

    def work():
        run_on_main(lambda: ran_on.append(QThread.currentThread() is main_thread))

    t = threading.Thread(target=work)
    t.start()
    t.join()

    assert _pump_until(qapp, lambda: ran_on), "callback never ran"
    assert ran_on == [True]


def test_run_on_main_with_delay_from_python_thread(qapp):
    ran = []
    t = threading.Thread(target=lambda: run_on_main(lambda: ran.append(1), 50))
    t.start()
    t.join()

    assert _pump_until(qapp, lambda: ran), "delayed callback never ran"
