"""The Python SDK against a real `octave --headless` (ctest runs this as
sdk_python with OCTAVE_BIN set to the build's binary)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from octave_client import PROTOCOL_VERSION, Client, OctaveError, launch_headless  # noqa: E402


class HeadlessClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = launch_headless()

    @classmethod
    def tearDownClass(cls):
        code = cls.app.stop()
        assert code == 0, f"octave exited with {code}"

    def setUp(self):
        self.octave = Client(port=self.app.port, timeout=15)

    def tearDown(self):
        self.octave.close()

    def test_hello(self):
        hello = self.octave.hello()
        self.assertEqual(hello["protocolVersion"], PROTOCOL_VERSION)
        self.assertTrue(hello["app"]["headless"])
        self.assertIn("nowPlaying", hello["objects"])

    def test_describe_matches_published_docs(self):
        d = self.octave.describe("volumeController")
        self.assertEqual(d["class"], "VolumeController")
        self.assertIn("percent", [p["name"] for p in d["properties"]])

    def test_get_call_and_events(self):
        sub = self.octave.subscribe("volumeController", properties=["percent"])
        first = next(self.octave.events(timeout=5))   # the current value, at once
        self.assertEqual(first["name"], "percent")
        target = 17 if first["value"] != 17 else 23
        self.assertIsNone(self.octave.call("volumeController", "applyVolume", target))
        self.assertEqual(self.octave.get("volumeController", "percent"), target)
        values = [e["value"] for e in self.octave.events(timeout=2) if e.get("sub") == sub]
        self.assertIn(target, values)
        self.octave.unsubscribe(sub)

    def test_param_store_and_catalog(self):
        info = self.octave.call("parameterCatalog", "info", "RPM")
        self.assertEqual(info["id"], "RPM")
        self.assertIsNone(self.octave.get("paramStore", "RPM"))   # no car: null before a reading
        self.assertEqual(self.octave.get("paramStore"), {})       # only ids with readings

    def test_rows_of_a_list_model(self):
        summary = self.octave.get("downloadManager", "downloadQueue")
        self.assertTrue(summary["model"])
        self.assertEqual(self.octave.rows("downloadManager", "downloadQueue"), [])

    def test_hello_on_connect(self):
        self.assertEqual(self.octave.server["protocolVersion"], PROTOCOL_VERSION)

    def test_errors_raise(self):
        with self.assertRaises(OctaveError):
            self.octave.get("noSuchObject", "x")
        with self.assertRaises(OctaveError):
            self.octave.call("volumeController", "noSuchMethod")

    def test_requests_from_threads(self):
        import threading
        results = []

        def worker():
            results.append(self.octave.get("volumeController", "percent"))

        threads = [threading.Thread(target=worker) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(len(results), 8)


if __name__ == "__main__":
    unittest.main()
