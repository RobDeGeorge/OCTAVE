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
