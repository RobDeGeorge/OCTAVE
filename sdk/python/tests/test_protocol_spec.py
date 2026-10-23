"""docs/api/protocol.json against a real `octave --headless`: every op the
spec names exists, every op the server registers is in the spec, and the
replies, errors and events the server sends validate against the spec's
schemas, strictly (a field the spec does not describe fails). Also the
spec's own coherence: its version is the server's, every $ref resolves,
the request enum lists exactly the ops."""
import json
import os
import socket
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "api"))

from jsonschema_lite import SchemaError, Validator  # noqa: E402
from octave_client import Client, OctaveError, launch_headless  # noqa: E402

SPEC_PATH = os.path.join(ROOT, "docs", "api", "protocol.json")


def load_spec():
    with open(SPEC_PATH, encoding="utf-8") as fh:
        return json.load(fh)


class RawConnection:
    """A line at a time, so replies can be checked as the server sends them."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=15)
        self.file = self.sock.makefile("rb")

    def send(self, obj):
        self.sock.sendall((json.dumps(obj) + "\n").encode())

    def read_json(self):
        while True:
            line = self.file.readline()
            if not line:
                raise EOFError("closed")
            if line[:1] == b"\xff":
                raise AssertionError("a stream record where JSON was expected")
            return json.loads(line)

    def request(self, obj):
        self.send(obj)
        while True:
            msg = self.read_json()
            if "event" not in msg:
                return msg

    def events_until(self, predicate, limit=50):
        seen = []
        for _ in range(limit):
            msg = self.read_json()
            if "event" in msg:
                seen.append(msg)
                if predicate(msg):
                    return seen
        raise AssertionError(f"no matching event in {limit} messages: {seen}")

    def close(self):
        self.sock.close()


class ProtocolSpecTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = load_spec()
        cls.v = Validator(cls.spec)
        cls.app = launch_headless()

    @classmethod
    def tearDownClass(cls):
        code = cls.app.stop()
        assert code == 0, f"octave exited with {code}"

    def setUp(self):
        self.octave = Client(port=self.app.port, timeout=15)
        self.raw = RawConnection(self.app.port)

    def tearDown(self):
        self.octave.close()
        self.raw.close()

    # ── helpers ───────────────────────────────────────────────────────────

    def check(self, value, schema, what):
        try:
            self.v.validate(value, schema, strict=True)
        except SchemaError as e:
            self.fail(f"{what}: {e}\n{json.dumps(value, indent=1)[:2000]}")

    def ok(self, op, **args):
        """Send `op` raw, check the envelope and the reply schema, return the result."""
        reply = self.raw.request({"op": op, "id": f"t-{op}", **args})
        self.check(reply, self.spec["reply"]["schema"], f"{op} envelope")
        self.assertTrue(reply["ok"], reply.get("error"))
        self.assertEqual(reply["id"], f"t-{op}")
        self.check(reply["result"], self.spec["ops"][op]["reply"], f"{op} reply")
        return reply["result"]

    def fails(self, op, **args):
        reply = self.raw.request({"op": op, "id": 1, **args})
        self.check(reply, self.spec["reply"]["schema"], f"{op} error envelope")
        self.assertFalse(reply["ok"])
        self.check(reply, self.spec["$defs"]["errorReply"], f"{op} error")
        return reply

    def event(self, name, msg):
        self.check(msg, self.spec["events"][name]["schema"], f"{name} event")

    # ── the spec itself ───────────────────────────────────────────────────

    def test_spec_is_coherent(self):
        for ref in self.v.refs():
            self.v.resolve(ref)   # raises when it does not
        ops = set(self.spec["ops"])
        self.assertEqual(set(self.spec["request"]["schema"]["properties"]["op"]["enum"]), ops)
        with open(os.path.join(ROOT, "docs", "api", "index.json"), encoding="utf-8") as fh:
            self.assertEqual(self.spec["protocolVersion"], json.load(fh)["protocolVersion"])
        for name, op in self.spec["ops"].items():
            self.assertIn("args", op, name)
            self.assertIn("reply", op, name)
            self.assertLessEqual(op["since"], self.spec["protocolVersion"], name)
        open_ops = {n for n, op in self.spec["ops"].items() if op.get("open")}
        self.assertEqual(open_ops, {"hello", "pair"})

    def test_every_op_both_ways(self):
        """The spec's ops are the server's ops, no more and no fewer."""
        self.assertEqual(self.octave.hello()["protocolVersion"], self.spec["protocolVersion"])
        served = self.octave.describe()["ops"]
        self.assertEqual(sorted(served), sorted(self.spec["ops"]))

    def test_published_object_files_match_the_description_schema(self):
        """docs/api/<object>.json and `describe` are the objectDescription shape."""
        schema = self.spec["$defs"]["objectDescription"]
        with open(os.path.join(ROOT, "docs", "api", "index.json"), encoding="utf-8") as fh:
            names = json.load(fh)["objects"]
        for name in names:
            with open(os.path.join(ROOT, "docs", "api", f"{name}.json"), encoding="utf-8") as fh:
                self.check(json.load(fh), schema, f"docs/api/{name}.json")

    # ── ops ───────────────────────────────────────────────────────────────

    def test_hello(self):
        r = self.ok("hello")
        self.assertEqual(r["auth"], "trusted")
        self.assertTrue(r["app"]["headless"])
        rejected = self.ok("hello", token="0" * 64)
        self.assertTrue(rejected["tokenRejected"])

    def test_describe(self):
        one = self.ok("describe", object="volumeController")
        self.assertEqual(one["class"], "VolumeController")
        every = self.ok("describe")
        self.assertIn("volumeController", every["objects"])
        self.assertIn("hello", every["ops"])
        # annotations, where the build has any, are the two the spec knows
        for obj in every["objects"].values():
            for member in obj["properties"] + obj["methods"] + obj["signals"]:
                self.assertLessEqual(set(member) & {"stability", "deprecated"}, {"stability", "deprecated"})

    def test_get_set_call(self):
        self.ok("get", object="volumeController", property="percent")
        summary = self.ok("get", object="downloadManager", property="downloadQueue")
        self.check(summary, self.spec["$defs"]["modelSummary"], "model summary")
        self.assertEqual(self.ok("get", object="paramStore"), {})
        self.assertIsNone(self.ok("get", object="paramStore", property="RPM"))
        self.assertIs(self.ok("set", object="volumeController", property="muted", value=False), False)
        self.assertIsNone(self.ok("call", object="volumeController", method="applyVolume", args=[21]))
        self.assertEqual(self.ok("get", object="volumeController", property="percent"), 21)
        info = self.ok("call", object="parameterCatalog", method="info", args=["RPM"])
        self.assertEqual(info["id"], "RPM")

    def test_rows(self):
        self.assertEqual(self.ok("rows", object="downloadManager", property="downloadQueue"), [])
        self.assertEqual(self.ok("rows", object="downloadManager", property="downloadQueue", **{"from": 0}, count=5), [])

    def test_subscribe_property_and_signal_events(self):
        r = self.ok("subscribe", object="volumeController", properties=["percent"], signals=["percentChanged"])
        sub = r["sub"]
        first = self.raw.events_until(lambda m: m["event"] == "property")[-1]
        self.event("property", first)
        self.assertEqual((first["sub"], first["object"], first["name"]), (sub, "volumeController", "percent"))
        target = 33 if first["value"] != 33 else 34
        self.octave.call("volumeController", "applyVolume", target)
        seen = self.raw.events_until(lambda m: m["event"] == "signal" and m["name"] == "percentChanged")
        for m in seen:
            self.event(m["event"], m)
        self.assertIs(self.ok("unsubscribe", sub=sub), True)

    def test_subscribe_params_event(self):
        self.ok("subscribe", object="paramStore", properties=["RPM", "SPEED"])
        first = self.raw.events_until(lambda m: m["event"] == "params")[-1]
        self.event("params", first)
        self.assertEqual(first["values"], {})   # no car: nothing has a value yet

    def test_subscribe_model_event(self):
        self.ok("subscribe", object="downloadManager", properties=["downloadQueue"])
        first = self.raw.events_until(lambda m: m["event"] == "model")[-1]
        self.event("model", first)
        self.assertEqual(first["change"], "reset")
        self.assertEqual(first["rows"], [])

    def test_stream_and_unstream(self):
        info = self.ok("stream", object="phoneMirrorManager", name="video")
        self.assertIn(info.get("format"), (None, "annexb"))   # no phone: the format is not known yet
        self.assertIs(self.ok("unstream", stream=info["stream"]), True)
        jpeg = self.ok("stream", object="androidAutoManager", name="frames")
        self.assertEqual(jpeg["format"], "jpeg")
        self.assertIs(self.ok("unstream", stream=jpeg["stream"]), True)
        self.assertIs(self.ok("unstream", stream=200), True)   # unknown: not an error
        for name, stream in self.spec["streams"]["names"].items():
            obj, _, sname = name.partition(".")
            self.assertIn(self.ok("stream", object=obj, name=sname).get("format"), (None, stream["format"]))

    def test_pairing_ops(self):
        ticket = self.ok("mediaTicket")
        self.assertEqual(ticket["expiresIn"], self.spec["pairing"]["limits"]["ticketLifetimeSeconds"])
        self.fails("pair", code="000000", name="spec test")       # no code is showing
        self.fails("unpair")                                      # loopback: no device to forget

    # ── errors ────────────────────────────────────────────────────────────

    def test_errors(self):
        self.assertIn("Unknown op", self.fails("noSuchOp")["error"])
        self.assertIn("Unknown object", self.fails("get", object="nope", property="x")["error"])
        self.fails("get", object="volumeController", property="nope")
        self.fails("set", object="volumeController", property="percent", value=1)      # read-only
        self.fails("set", object="volumeController", property="muted", value=[1, 2])     # wrong type
        self.fails("call", object="volumeController", method="nope")
        self.fails("rows", object="volumeController", property="percent")               # not a model
        self.fails("subscribe", object="volumeController")                              # nothing named
        self.fails("subscribe", object="volumeController", signals=["nope"])
        self.fails("unsubscribe", sub=999999)
        self.fails("stream", object="volumeController", name="video")
        for code in self.spec["errors"]["codes"]:
            self.assertIn(code, self.spec["$defs"]["errorReply"]["properties"]["code"]["enum"])
        reply = self.raw.request({"op": "get", "id": 7})          # missing object
        self.check(reply, self.spec["$defs"]["errorReply"], "bare get")
        with self.assertRaises(OctaveError):
            self.octave.get("nope")

    def test_rate_limited(self):
        # Past the burst (400 at once by default) an op is refused at once
        n = 500
        self.raw.sock.sendall(b"".join(b'{"op":"hello","id":%d}\n' % i for i in range(n)))
        refused = [r for r in (self.raw.read_json() for _ in range(n)) if not r["ok"]]
        self.assertTrue(refused, "500 ops at once were all answered")
        for reply in refused[:5]:
            self.check(reply, self.spec["$defs"]["errorReply"], "rateLimited")
            self.assertEqual(reply["code"], "rateLimited")
            self.assertGreater(reply["retryAfterMs"], 0)

    def test_invalid_json_line(self):
        self.raw.sock.sendall(b"{not json\n")
        reply = self.raw.read_json()
        self.check(reply, self.spec["$defs"]["errorReply"], "invalid JSON")
        self.assertIn("Invalid JSON", reply["error"])


if __name__ == "__main__":
    unittest.main()
