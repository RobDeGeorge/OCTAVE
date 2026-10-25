"""tools/api/fake_server.py against sdk/python's client: the fake speaks
protocol.json well enough to develop a client on. No app needed (ctest
runs this with api_compat)."""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "sdk", "python"))

from fake_server import FakeServer  # noqa: E402
from octave_client import PROTOCOL_VERSION, Client, OctaveError  # noqa: E402


class FakeServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = FakeServer(port=0).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def setUp(self):
        self.octave = Client(port=self.server.port, timeout=10)

    def tearDown(self):
        self.octave.close()

    def test_hello_and_describe(self):
        self.assertEqual(self.octave.server["protocolVersion"], PROTOCOL_VERSION)
        self.assertTrue(self.octave.trusted)
        self.assertIn("volumeController", self.octave.server["objects"])
        self.assertEqual(self.octave.describe("volumeController")["class"], "VolumeController")
        self.assertEqual(sorted(self.octave.describe()["ops"]), sorted(self.server.spec["ops"]))

    def test_get_set_call_and_events(self):
        sub = self.octave.subscribe("volumeController", properties=["percent"], signals=["percentChanged"])
        first = next(self.octave.events(timeout=5))
        self.assertEqual((first["event"], first["sub"], first["name"]), ("property", sub, "percent"))
        self.octave.call("volumeController", "applyVolume", 55)
        self.assertEqual(self.octave.get("volumeController", "percent"), 55)
        self.assertIs(self.octave.set("volumeController", "muted", True), True)
        values = [e["value"] for e in self.octave.events(timeout=1) if e.get("name") == "percent"]
        self.assertIn(55, values)
        self.octave.call("fake", "emitSignal", "volumeController", "percentChanged", [55])
        self.assertTrue(any(e["event"] == "signal" and e["args"] == [55] for e in self.octave.events(timeout=1)))
        self.octave.unsubscribe(sub)
        with self.assertRaises(OctaveError):
            self.octave.set("volumeController", "percent", 1)         # read-only
        with self.assertRaises(OctaveError):
            self.octave.call("volumeController", "nope")

    def test_param_store_ticks(self):
        self.octave.subscribe("paramStore", properties=["RPM"])
        events = []
        for e in self.octave.events(timeout=2):
            if e["event"] == "params":
                events.append(e)
            if len(events) >= 3:
                break
        self.assertTrue(all(set(e["values"]) <= {"RPM"} for e in events))
        self.assertEqual(self.octave.call("parameterCatalog", "info", "RPM")["id"], "RPM")

    def test_model(self):
        self.assertEqual(self.octave.get("fake", "items")["model"], True)
        self.assertEqual(self.octave.rows("fake", "items"), [])
        self.octave.subscribe("fake", properties=["items"])
        reset = next(self.octave.events(timeout=5))
        self.assertEqual((reset["event"], reset["change"], reset["rows"]), ("model", "reset", []))

    def test_stream_records(self):
        reply = self.octave.stream("phoneMirrorManager", "video")
        self.assertEqual(reply["format"], "annexb")
        records = []
        for ev in self.octave.events(timeout=2):
            if ev["event"] == "record":
                records.append(ev)
            if len(records) >= 5:
                break
        self.assertEqual(len(records), 5)
        self.assertTrue(records[0]["key"] and records[0]["config"] and records[0]["discontinuity"])
        self.assertFalse(records[1]["key"])
        self.assertEqual(records[0]["stream"], reply["stream"])
        self.octave.unstream(reply["stream"])

    def test_pairing_scopes_and_forbidden(self):
        with self.assertRaises(OctaveError):
            self.octave.pair("000000", "test")
        token = self.octave.pair("123456", "test", scopes=["media"])
        self.assertEqual(self.octave.scopes, ["media", "read"])
        with Client(port=self.server.port, token=token) as again:
            self.assertEqual(again.scopes, ["media", "read"])
            again.call("nowPlaying", "togglePlay")                    # media: allowed
            again.call("obdManager", "setParameterDemand", "x", [])   # read: always allowed
            again.set("volumeController", "muted", False)             # media: allowed
            with self.assertRaises(OctaveError) as e:
                again.call("nowPlaying", "extractArtColors")          # full: refused
            self.assertIn("permission", str(e.exception))
        self.octave.call("fake", "revokeTokens")
        with Client(port=self.server.port, token=token) as rejected:
            self.assertTrue(rejected.server["tokenRejected"])
        self.assertEqual(self.octave.media_ticket()["expiresIn"], 600)

    def test_untrusted_until_paired(self):
        server = FakeServer(port=0, untrusted=True).start()
        try:
            with Client(port=server.port) as c:
                self.assertFalse(c.trusted)
                with self.assertRaises(OctaveError) as e:
                    c.get("volumeController", "percent")
                self.assertIn("not paired", str(e.exception))
                c.pair("123456", "test", scopes=["vehicle"])
                self.assertEqual(c.get("volumeController", "percent"), 32)
                with self.assertRaises(OctaveError) as e:
                    c.call("nowPlaying", "togglePlay")                # needs media
                self.assertIn("media permission", str(e.exception))
        finally:
            server.stop()

    def test_faults_on_demand(self):
        self.octave.call("fake", "rateLimitNext", 2)
        for _ in range(2):
            with self.assertRaises(OctaveError) as e:
                self.octave.get("volumeController", "percent")
            self.assertIn("too many ops", str(e.exception))
        self.assertEqual(self.octave.get("volumeController", "percent") is not None, True)
        self.octave.call("fake", "setForbidden", True)
        try:
            with self.assertRaises(OctaveError) as e:
                self.octave.call("nowPlaying", "next")
            self.assertIn("permission", str(e.exception))
        finally:
            self.server.forbid_all = False      # fake.setForbidden(false) would itself be forbidden
        self.octave.call("fake", "setValue", "nowPlaying", "title", "Set by the test")
        self.assertEqual(self.octave.get("nowPlaying", "title"), "Set by the test")


if __name__ == "__main__":
    unittest.main()
