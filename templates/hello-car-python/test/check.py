"""The Python template against a real `octave --headless --sim` on a free port
with a throwaway config (OCTAVE_BIN names the binary): `hello_car.py --check`
must see readings and a playing track, and the copied-in client must still be
the SDK's, byte for byte. ctest runs this as template_python."""
import filecmp
import os
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.dirname(HERE)
REPO = os.path.normpath(os.path.join(TEMPLATE, "..", ".."))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class HelloCarPythonTest(unittest.TestCase):
    def test_the_copied_client_is_the_sdk(self):
        for name in ("__init__.py", "launch.py"):
            self.assertTrue(filecmp.cmp(os.path.join(TEMPLATE, "octave_client", name),
                                        os.path.join(REPO, "sdk", "python", "octave_client", name), shallow=False),
                            f"templates/hello-car-python/octave_client/{name} differs from sdk/python/octave_client/{name}: copy it again")

    def test_pairing_connects_again_when_the_connection_went_while_waiting(self):
        """OCTAVE closes an unpaired connection after a minute of silence; the
        pairing prompt must connect again with the code just typed. Against a
        scripted server in this process: no OCTAVE needed."""
        import builtins
        import importlib
        import json
        import threading

        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        self.addCleanup(listener.close)
        connections = []

        def serve(conn, first):
            trusted = False
            try:
                for line in conn.makefile("rb"):
                    req = json.loads(line)
                    if req["op"] == "hello":
                        result = ({"protocolVersion": 3, "auth": "trusted", "scopes": ["read", "media"], "app": {"version": "fake"}}
                                  if trusted else {"protocolVersion": 3, "auth": "untrusted", "loopbackOpen": False})
                    elif req["op"] == "pair":
                        trusted = True
                        result = {"token": "t" * 64, "scopes": ["read", "media"]}
                    else:
                        result = True
                    conn.sendall((json.dumps({"id": req["id"], "ok": True, "result": result}) + "\n").encode())
                    if first and req["op"] == "hello":
                        conn.shutdown(socket.SHUT_RDWR)   # the idle close
                        return
            except (OSError, ValueError):
                pass

        def accept():
            while True:
                try:
                    conn, _ = listener.accept()
                except OSError:
                    return
                connections.append(conn)
                threading.Thread(target=serve, args=(conn, len(connections) == 1), daemon=True).start()

        threading.Thread(target=accept, daemon=True).start()
        sys.path.insert(0, TEMPLATE)
        hello_car = importlib.import_module("hello_car")
        with tempfile.TemporaryDirectory(prefix="hello-car-") as work:
            hello_car.TOKEN_FILE = os.path.join(work, ".token")
            asked = []
            real_input, builtins.input = builtins.input, lambda prompt="": asked.append(prompt) or "123 456"
            saved_env = os.environ.pop("OCTAVE_TOKEN", None)
            try:
                octave = hello_car.connect("127.0.0.1", listener.getsockname()[1])
            finally:
                builtins.input = real_input
                if saved_env is not None:
                    os.environ["OCTAVE_TOKEN"] = saved_env
            self.addCleanup(octave.close)
            self.assertTrue(octave.trusted)
            self.assertEqual(len(connections), 2, "it connected again")
            self.assertEqual(len(asked), 1, "with the code already typed, not a second prompt")
            self.assertEqual(octave.server["app"]["version"], "fake")
            with open(hello_car.TOKEN_FILE) as fh:
                self.assertEqual(fh.read(), "t" * 64)
            self.assertEqual(os.stat(hello_car.TOKEN_FILE).st_mode & 0o777, 0o600)

    def test_runs_against_the_simulator(self):
        binary = os.environ.get("OCTAVE_BIN", os.path.join(REPO, "build", "octave"))
        if not os.path.exists(binary):
            self.skipTest("no OCTAVE_BIN")
        port = free_port()
        with tempfile.TemporaryDirectory(prefix="hello-car-") as home:
            env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home, XDG_DATA_HOME=home, XDG_CACHE_HOME=home, QT_QPA_PLATFORM="offscreen")
            app = subprocess.Popen([binary, "--headless", "--sim", "--service", str(port)], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    self.assertIsNone(app.poll(), "octave exited before serving")
                    try:
                        socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                        break
                    except OSError:
                        time.sleep(0.2)
                # The simulated car idles with the engine off: start it and press the pedal
                sys.path.insert(0, os.path.join(REPO, "sdk", "python"))
                from octave_client import Client
                with Client(port=port) as sim:
                    self.assertTrue(sim.call("sim", "setEngineOn", True))
                    self.assertTrue(sim.call("sim", "setThrottle", 40))
                # Run the template from a copy, so its token / cover files stay out of the tree
                import shutil
                work = os.path.join(home, "template")
                shutil.copytree(TEMPLATE, work, ignore=shutil.ignore_patterns("test", "__pycache__", ".token", "cover.*"))
                run = subprocess.run([sys.executable, os.path.join(work, "hello_car.py"), "--port", str(port), "--check", "4"],
                                     capture_output=True, text=True, timeout=120)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn("all checks passed", run.stdout)
                self.assertIn("RPM", run.stdout)
            finally:
                app.terminate()
                try:
                    app.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    app.kill()


if __name__ == "__main__":
    unittest.main()
