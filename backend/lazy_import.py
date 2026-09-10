"""Deferred module imports for the Python backend.

Several optional libraries are expensive to import (``obd`` pulls in pint,
``spotipy`` pulls in redis, ``numpy``/``av`` load large native extensions) and
none of them are needed until the user connects an adapter, logs in, or plays a
track. Importing them at startup cost about a second before the first frame on
the Orange Pi. ``lazy_module`` returns a proxy that imports on first attribute
access, so module code can keep writing ``obd.commands.RPM``; ``available``
answers "is it installed" without importing it.
"""

import importlib
import importlib.util


def available(name: str) -> bool:
    """True when the top-level package is installed (no import performed)."""
    try:
        return importlib.util.find_spec(name) is not None
    except (ImportError, ValueError):
        return False


class LazyModule:
    """Import ``name`` on first attribute access and forward everything to it."""

    def __init__(self, name: str):
        self._lazy_name = name
        self._lazy_module = None

    def _lazy_load(self):
        if self._lazy_module is None:
            self._lazy_module = importlib.import_module(self._lazy_name)
        return self._lazy_module

    def __getattr__(self, attr):
        return getattr(self._lazy_load(), attr)

    def __repr__(self):
        state = "loaded" if self._lazy_module is not None else "not loaded"
        return f"<LazyModule {self._lazy_name} ({state})>"


def lazy_module(name: str):
    return LazyModule(name)
