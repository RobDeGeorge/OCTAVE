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
        # On this device, with loopback open (the default), no pairing
        self.assertEqual(self.octave.server["auth"], "trusted")
        self.assertTrue(self.octave.trusted)

    def test_a_rejected_token_still_connects(self):
        with Client(port=self.app.port, timeout=15, token="0" * 64) as octave:
            self.assertTrue(octave.server["tokenRejected"])
            self.assertTrue(octave.trusted)   # on this device anyway, with loopback open

    def test_media_ticket(self):
        ticket = self.octave.media_ticket()
        self.assertEqual(len(ticket["ticket"]), 32)
        self.assertEqual(ticket["expiresIn"], 600)

    def test_pair_needs_the_code_on_screen(self):
        with self.assertRaises(OctaveError) as e:
            self.octave.pair("000000", "test")
        self.assertIn("no pairing code", str(e.exception))

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


class StreamRecordTest(unittest.TestCase):
    """Binary stream records on TCP (docs/api/README.md, "Streams") through
    the dev-only test source, sim_video; skipped without the dev tools."""

    @classmethod
    def setUpClass(cls):
        cls.app = launch_headless(extra=("--dev",))

    @classmethod
    def tearDownClass(cls):
        code = cls.app.stop()
        assert code == 0, f"octave exited with {code}"

    def _action(self, name, **args):
        import json
        import socket
        with socket.create_connection(("127.0.0.1", self.app.port), timeout=10) as s:
            s.sendall((json.dumps({"action": name, "args": args}) + "\n").encode())
            return json.loads(s.makefile().readline())

    def test_video_records(self):
        with Client(port=self.app.port, timeout=15) as octave:
            reply = octave.stream("phoneMirrorManager", "video")
            started = self._action("sim_video", on=True)
            if not started.get("ok"):
                self.skipTest("built without the dev tools (no sim_video)")
            try:
                records, configs = [], []
                for event in octave.events(timeout=5):
                    if event["event"] == "record":
                        records.append(event)
                    elif event["event"] == "streamConfig":
                        configs.append(event)
                    if len(records) >= 40:
                        break
            finally:
                self._action("sim_video", on=False)
            self.assertGreaterEqual(len(records), 40)
            self.assertEqual(records[0]["stream"], reply["stream"])
            self.assertTrue(records[0]["key"] and records[0]["config"] and records[0]["discontinuity"])
            self.assertTrue(records[0]["data"].startswith(b"\x00\x00\x00\x01"))   # Annex-B, SPS first
            self.assertEqual(records[0]["data"][4] & 0x1F, 7)
            self.assertFalse(records[1]["key"])
            self.assertGreater(records[1]["pts"], records[0]["pts"])
            self.assertTrue(configs and configs[0]["codec"].startswith("avc1."))
            # Requests still work on the same connection between records
            self.assertEqual(octave.get("volumeController", "percent"), octave.get("volumeController", "percent"))
