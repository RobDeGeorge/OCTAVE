"""The starter templates as installed frontends, end to end: each is
installed from its folder into a throwaway config with
frontendManager.installFromFolder, made the active screen, and OCTAVE is
started plainly (no --headless) with --sim.

- hello-car-python (exec): OCTAVE runs it headless as the screen with a
  token of its manifest's scopes; it runs `--check`, which also requires
  read + media only and a refused settings change, and its exit 0 ends
  OCTAVE with 0.
- hello-car-js (web): served at GET /, and a WebSocket with the page's
  Origin holds read + media only.

ctest `templates_install`; OCTAVE_BIN names the binary.
"""
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "sdk", "python"))
from octave_client import Client  # noqa: E402

BIN = os.environ.get("OCTAVE_BIN", os.path.join(ROOT, "build", "octave"))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Run:
    """An octave process with an isolated home and the given config"""

    def __init__(self, home, args):
        env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home, XDG_DATA_HOME=home, XDG_CACHE_HOME=home,
                   QT_QPA_PLATFORM="offscreen")
        self.log = open(os.path.join(home, f"octave-{time.monotonic_ns()}.log"), "w+")
        self.proc = subprocess.Popen([BIN] + args, env=env, stdout=self.log, stderr=subprocess.STDOUT)

    def output(self):
        self.log.flush()
        self.log.seek(0)
        return self.log.read()

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(30)


def client(port, timeout=30):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return Client("127.0.0.1", port)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


def install(home, config, folder):
    """Install `folder` and make it the screen, through a headless run"""
    port = free_port()
    run = Run(home, ["--headless", "--config", config, "--service", str(port)])
    try:
        with client(port) as octave:
            fid = octave.call("frontendManager", "installFromFolder", folder)
            if not fid:
                raise AssertionError(octave.get("frontendManager", "lastError"))
            assert octave.call("frontendManager", "setActive", fid)
            return fid
    finally:
        run.stop()
        run.log.close()


def ws_request(port, request):
    """One request over a WebSocket opened as the page's browser would"""
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    host = f"127.0.0.1:{port}"
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET / HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nOrigin: http://{host}\r\n\r\n").encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    assert buf.startswith(b"HTTP/1.1 101"), buf[:200]
    buf = buf.split(b"\r\n\r\n", 1)[1]
    payload = json.dumps(request).encode()
    mask = os.urandom(4)
    s.sendall(bytes([0x81, 0x80 | 126]) + struct.pack(">H", len(payload)) + mask
              + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))
    while True:
        while len(buf) >= 2:
            n, off = buf[1] & 0x7F, 2
            if n == 126:
                n, off = struct.unpack(">H", buf[2:4])[0], 4
            elif n == 127:
                n, off = struct.unpack(">Q", buf[2:10])[0], 10
            if len(buf) < off + n:
                break
            frame, buf = buf[off:off + n], buf[off + n:]
            message = json.loads(frame)
            if message.get("id") == request["id"]:
                s.close()
                return message
        buf += s.recv(65536)


def seed_music(config, music):
    """A chosen --config is not seeded the way --sim's throwaway one is: a
    music folder with one short track, so the templates have something to play"""
    os.makedirs(music, exist_ok=True)
    with wave.open(os.path.join(music, "01 - Test.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(8000)
        w.writeframes(b"\x00\x10" * 8000 * 20)
    os.makedirs(config, exist_ok=True)
    with open(os.path.join(config, "settingsConfigure.json"), "w") as fh:
        json.dump({"mediaFolder": music, "esp32VolumeEnabled": False, "gestureSensorEnabled": False}, fh)


class TemplatesInstall(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.config = os.path.join(self.tmp, "config")
        seed_music(self.config, os.path.join(self.tmp, "Music"))

    def tearDown(self):
        # A failing exec frontend makes OCTAVE start itself again on the QML
        # UI, detached: end any process still using this test's own config
        if os.path.isdir("/proc"):
            for pid in filter(str.isdigit, os.listdir("/proc")):
                try:
                    with open(f"/proc/{pid}/cmdline", "rb") as fh:
                        cmdline = fh.read().split(b"\0")
                except OSError:
                    continue
                if self.config.encode() in cmdline:
                    try:
                        os.kill(int(pid), 15)
                    except OSError:
                        pass
            time.sleep(1)
        shutil.rmtree(self.tmp, ignore_errors=True)

    @unittest.skipIf(sys.platform == "win32" or not shutil.which("python3"), "the exec manifest names python3")
    def test_python_template_is_the_screen(self):
        # The template as it is, with --check added so it ends by itself
        src = os.path.join(self.tmp, "hello-car-python")
        shutil.copytree(os.path.join(ROOT, "templates", "hello-car-python"), src,
                        ignore=shutil.ignore_patterns("__pycache__", ".token", "cover.*"))
        manifest_path = os.path.join(src, "octave-frontend.json")
        with open(manifest_path) as fh:
            manifest = json.load(fh)
        for argv in manifest["exec"].values():
            argv += ["--check", "6"]
        with open(manifest_path, "w") as fh:
            json.dump(manifest, fh)
        self.assertEqual(install(self.tmp, self.config, src), "org.octave.hello-car-python")

        run = Run(self.tmp, ["--config", self.config, "--sim", "--service", str(free_port())])
        try:
            code = run.proc.wait(90)   # its exit 0 ends OCTAVE
        finally:
            run.stop()
        out = run.output()
        run.log.close()
        self.assertEqual(code, 0, out[-3000:])
        self.assertIn("is the screen; loopback trust is off", out)
        self.assertNotIn("Main.qml", out)
        self.assertRegex(out, r"as the screen with \[.*'media'.*\]; settings refused: True")
        self.assertIn("hello-car-python: all checks passed", out)

    def test_js_template_is_served(self):
        folder = os.path.join(ROOT, "templates", "hello-car-js")
        self.assertEqual(install(self.tmp, self.config, folder), "org.octave.hello-car-js")
        port = free_port()
        run = Run(self.tmp, ["--config", self.config, "--sim", "--service", str(port)])
        try:
            # The page, at /
            deadline = time.monotonic() + 30
            page = b""
            while time.monotonic() < deadline and b"app.js" not in page:
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
                        s.sendall(f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nConnection: close\r\n\r\n".encode())
                        page = b""
                        while chunk := s.recv(65536):
                            page += chunk
                except OSError:
                    time.sleep(0.3)
            self.assertIn(b"app.js", page, run.output()[-3000:])
            # Its browser connection holds what the manifest asked for
            hello = ws_request(port, {"op": "hello", "id": 1})
            self.assertTrue(hello["ok"], hello)
            self.assertEqual(set(hello["result"]["scopes"]) - {"read"}, {"media"}, hello)
            denied = ws_request(port, {"op": "call", "object": "settingsManager", "method": "save_setting",
                                       "args": ["uiScale", 1.0], "id": 2})
            self.assertEqual(denied.get("code"), "forbidden", denied)
            played = ws_request(port, {"op": "call", "object": "nowPlaying", "method": "togglePlay", "args": [], "id": 3})
            self.assertTrue(played["ok"], played)
            self.assertIn("Frontend org.octave.hello-car-js 1.0.0 (web) is the active screen", run.output())
        finally:
            run.stop()
            run.log.close()


if __name__ == "__main__":
    unittest.main()
