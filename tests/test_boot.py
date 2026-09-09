"""
Headless boot smoke test: the Python app must start and stay up.

`python main.py` is launched offscreen and must still be running after a
few seconds with no traceback on its output. Import-level and manager
probes cannot catch a missing method that only a startup path calls (a
removed no-op slot once broke boot while every unit-level check passed).
"""

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_python_app_boots_and_stays_up():
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", OCTAVE_SMOKE_BOOT="1")
    proc = subprocess.Popen(
        [sys.executable, str(ROOT / "main.py")],
        cwd=str(ROOT), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    deadline = time.time() + 12.0
    while time.time() < deadline and proc.poll() is None:
        time.sleep(0.25)
    alive = proc.poll() is None
    if alive:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    output = proc.stdout.read() if proc.stdout else ""
    assert "Traceback" not in output, f"traceback during boot:\n{output[-3000:]}"
    assert alive, f"main.py exited early with code {proc.returncode}:\n{output[-3000:]}"
