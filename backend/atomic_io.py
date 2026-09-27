"""Power-cut-safe file writes.

The head unit loses power with the ignition, so any file rewritten while
driving must be replaced in one step: write a temp file in the same
directory, fsync it, os.replace() it over the target, then fsync the
directory. A cut at any point leaves either the old file or the new one.
C++ uses QSaveFile for the same guarantee.
"""

import os
import sys
import tempfile


def atomic_write_bytes(path, data):
    """Atomically replace ``path`` with ``data`` (bytes)."""
    path = os.fspath(path)
    dir_name = os.path.dirname(path) or "."
    fd, temp_path = tempfile.mkstemp(dir=dir_name, prefix=".tmp-", suffix=".part")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp_path, path)
    except BaseException:
        try:
            os.remove(temp_path)
        except OSError:
            pass
        raise
    if sys.platform != "win32":
        # Persist the rename itself (the directory entry)
        try:
            dir_fd = os.open(dir_name, os.O_RDONLY)
            try:
                os.fsync(dir_fd)
            finally:
                os.close(dir_fd)
        except OSError:
            pass


def atomic_write_text(path, text, encoding="utf-8"):
    """Atomically replace ``path`` with ``text``."""
    atomic_write_bytes(path, text.encode(encoding))
