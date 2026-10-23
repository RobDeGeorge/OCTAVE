"""Start an OCTAVE to talk to: find the binary, run it headless.

    from octave_client import Client, launch_headless

    with launch_headless() as app:                 # a throwaway config by default
        with Client(port=app.port) as octave:
            print(octave.hello())
"""

import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

EXE = "octave.exe" if os.name == "nt" else "octave"
REPO = Path(__file__).resolve().parents[3]


def find_binary():
    """$OCTAVE_BIN, else this checkout's build/octave, else the newest build*/octave,
    else `octave` on PATH."""
    env = os.environ.get("OCTAVE_BIN")
    if env:
        return Path(env)
    if (REPO / "build" / EXE).is_file():
        return REPO / "build" / EXE
    found = sorted(REPO.glob(f"build*/{EXE}"), key=lambda p: p.stat().st_mtime, reverse=True)
    if found:
        return found[0]
    on_path = shutil.which("octave")
    if on_path:
        return Path(on_path)
    raise FileNotFoundError("No OCTAVE binary: build one (see BUILD.md) or set OCTAVE_BIN")


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class HeadlessApp:
    """A running `octave --headless`; stop() (or the with block) ends it."""

    def __init__(self, binary=None, port=None, config_dir=None, extra=(), ready_timeout=60.0):
        self.port = port or _free_port()
        self._tmp = None
        if config_dir is None:
            self._tmp = tempfile.TemporaryDirectory(prefix="octave-sdk-")
            config_dir = self._tmp.name
        env = dict(os.environ)
        # The app's settings, trip history and logs go here, not the user's
        for var in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA"):
            env[var] = str(config_dir)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        argv = [str(binary or find_binary()), "--headless", "--service", str(self.port), *extra]
        self.process = subprocess.Popen(argv, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._wait_ready(ready_timeout)

    def _wait_ready(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"octave exited with {self.process.returncode} before serving")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return
            except OSError:
                time.sleep(0.2)
        self.stop()
        raise TimeoutError(f"octave did not serve on port {self.port} within {timeout} s")

    def stop(self, timeout=15.0):
        if self.process.poll() is None:
            self.process.terminate()   # SIGTERM: the app runs its quit cleanup
            try:
                self.process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None
        return self.process.returncode

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()


def launch_headless(binary=None, port=None, config_dir=None, extra=(), ready_timeout=60.0):
    """Start `octave --headless --service PORT` (a free port by default) with its
    config in `config_dir` (a temp dir by default) and wait until it serves."""
    return HeadlessApp(binary, port, config_dir, extra, ready_timeout)
