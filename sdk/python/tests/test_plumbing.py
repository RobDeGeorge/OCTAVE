"""The client's connection plumbing against a scripted line server in this
process (loopback, a free port): what a closed connection, a consumer that
stops reading, a refusal with no id, a rate limit, a short record and a
failed hello do to it. No OCTAVE needed."""
import json
import os
import socket
import struct
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from octave_client import PROTOCOL_VERSION, PROTOCOL_VERSIONS, Client, OctaveConnectError, OctaveError  # noqa: E402


class LineServer:
    """Answers hello, then hands every other request to `handle(req, send, conn)`."""

    def __init__(self, handle=None, hello=None):
        self.handle = handle or (lambda req, send, conn: send({"id": req["id"], "ok": True, "result": True}))
        self.hello = hello if hello is not None else {"protocolVersion": PROTOCOL_VERSION, "auth": "trusted", "scopes": ["full"],
                                                      "app": {"version": "fake", "build": "test", "headless": True}, "objects": []}
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.port = self.listener.getsockname()[1]
        self.conns = []
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                conn, _ = self.listener.accept()
            except OSError:
                return
            self.conns.append(conn)
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn):
        def send(obj):
            conn.sendall((json.dumps(obj) + "\n").encode())
        try:
            for line in conn.makefile("rb"):
                req = json.loads(line)
                if req.get("op") == "hello":
                    if self.hello == "silent":
                        continue
                    if self.hello == "refuse":
                        send({"id": req["id"], "ok": False, "error": "no"})
                        continue
                    send({"id": req["id"], "ok": True, "result": self.hello})
                    continue
                self.handle(req, send, conn)
        except (OSError, ValueError):
            pass

    def stop(self):
        self.listener.close()
        for conn in self.conns:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            conn.close()


class PlumbingTest(unittest.TestCase):
    def serve(self, handle=None, hello=None):
        server = LineServer(handle, hello)
        self.addCleanup(server.stop)
        return server

    def test_versions_follow_the_newest(self):
        self.assertEqual(PROTOCOL_VERSIONS, tuple(range(1, PROTOCOL_VERSION + 1)))

    def test_nothing_listening_is_an_octave_error_and_an_os_error(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        with self.assertRaises(OctaveConnectError) as e:
            Client(port=port, timeout=2)
        self.assertIsInstance(e.exception, OctaveError)
        self.assertIsInstance(e.exception, OSError)
        self.assertIn(str(port), str(e.exception))

    def test_events_end_when_the_app_goes_and_closed_says_so(self):
        server = self.serve()
        octave = Client(port=server.port, timeout=5)
        self.assertFalse(octave.closed)
        self.assertEqual(list(octave.events(timeout=0.1)), [])
        self.assertFalse(octave.closed, "a quiet timeout is not a closed connection")
        server.stop()
        self.assertEqual(list(octave.events()), [])          # ends, without a timeout
        self.assertTrue(octave.closed)
        start = time.monotonic()
        for _ in range(3):                                    # and every later call ends at once: no 2 Hz spin
            self.assertEqual(list(octave.events(timeout=5)), [])
        self.assertLess(time.monotonic() - start, 1)
        with self.assertRaises(OctaveError) as e:
            next(octave.events(closed_error=True))
        self.assertEqual(e.exception.code, "closed")
        with self.assertRaises(OctaveError):
            octave.request("get", object="x")
        octave.close()
        octave.close()

    def test_events_received_before_the_close_are_still_read(self):
        def handle(req, send, conn):
            send({"event": "property", "sub": 1, "object": "o", "name": "n", "value": 1})
            send({"id": req["id"], "ok": True, "result": True})
            conn.shutdown(socket.SHUT_RDWR)
        server = self.serve(handle)
        octave = Client(port=server.port, timeout=5)
        octave.request("go")
        self.assertEqual([e["value"] for e in octave.events()], [1])
        self.assertTrue(octave.closed)

    def test_an_unread_queue_drops_the_oldest(self):
        flood = 500

        def handle(req, send, conn):
            for i in range(flood):
                send({"event": "property", "sub": 1, "object": "o", "name": "n", "value": i})
            send({"id": req["id"], "ok": True, "result": 7})
        server = self.serve(handle)
        with Client(port=server.port, timeout=5, max_events=100) as octave:
            self.assertEqual(octave.request("flood"), 7)     # the reply behind the flood still arrives
            self.assertEqual(octave.dropped_events, flood - 100)
            values = [e["value"] for e in octave.events(timeout=0.2)]
            self.assertEqual(values, list(range(flood - 100, flood)), "the newest are kept, in order")

    def test_refusals_carry_retry_after_and_an_idless_one_fails_what_waits(self):
        def handle(req, send, conn):
            if req["op"] == "limited":
                send({"id": req["id"], "ok": False, "error": "too many ops", "code": "rateLimited", "retryAfterMs": 25})
            elif req["op"] == "unreadable":
                send({"ok": False, "error": "Invalid JSON: test"})   # as the app answers a line it cannot parse
            else:
                send({"id": req["id"], "ok": True, "result": True})
        server = self.serve(handle)
        with Client(port=server.port, timeout=10) as octave:
            with self.assertRaises(OctaveError) as e:
                octave.request("limited")
            self.assertEqual((e.exception.code, e.exception.retry_after_ms), ("rateLimited", 25))
            start = time.monotonic()
            with self.assertRaises(OctaveError) as e:
                octave.request("unreadable")
            self.assertIn("Invalid JSON", str(e.exception))
            self.assertLess(time.monotonic() - start, 5, "it did not wait for the timeout")
            self.assertIsNone(e.exception.retry_after_ms)
            self.assertTrue(octave.request("fine"))

    def test_a_short_or_foreign_record_is_skipped(self):
        def handle(req, send, conn):
            good = struct.pack(">BBBBQ", 1, 3, 1, 0, 1234) + b"\xaa"
            for record in (b"\x01\x03", struct.pack(">BBBBQ", 9, 3, 0, 0, 0), good):
                conn.sendall(b"\xff" + struct.pack(">I", len(record)) + record)
            send({"id": req["id"], "ok": True, "result": True})
        server = self.serve(handle)
        with Client(port=server.port, timeout=5) as octave:
            self.assertTrue(octave.request("records"))
            records = list(octave.events(timeout=0.2))
            self.assertEqual(len(records), 1)
            self.assertEqual((records[0]["stream"], records[0]["pts"], records[0]["key"], records[0]["data"]), (3, 1234, True, b"\xaa"))
            self.assertFalse(octave.closed, "a bad record does not end the connection")
            self.assertTrue(octave.request("still fine"))

    def test_a_failed_hello_leaves_no_thread_or_socket(self):
        before = {t.ident for t in threading.enumerate()}
        for hello, timeout in (("refuse", 5), ("silent", 0.3)):
            server = self.serve(hello=hello)
            with self.assertRaises(OctaveError):
                Client(port=server.port, timeout=timeout)
        deadline = time.monotonic() + 3
        left = None
        while time.monotonic() < deadline:
            left = [t for t in threading.enumerate() if t.name == "octave-client" and t.ident not in before]
            if not left:
                break
            time.sleep(0.05)
        self.assertEqual(left, [], "the reader thread of a client that never came up is still running")

    def test_pairing_asks_hello_again(self):
        untrusted = {"protocolVersion": PROTOCOL_VERSION, "auth": "untrusted", "loopbackOpen": False}
        state = {"trusted": False}
        server = LineServer(hello=untrusted)
        self.addCleanup(server.stop)

        def handle(req, send, conn):
            if req["op"] == "pair":
                state["trusted"] = True
                server.hello = {"protocolVersion": PROTOCOL_VERSION, "auth": "trusted", "scopes": ["read", "media"],
                                "app": {"version": "fake", "build": "test", "headless": True}, "objects": ["nowPlaying"]}
                send({"id": req["id"], "ok": True, "result": {"token": "t" * 64, "scopes": ["read", "media"]}})
            elif req["op"] == "unpair":
                send({"id": req["id"], "ok": True, "result": True})
            else:
                send({"id": req["id"], "ok": True, "result": True})
        server.handle = handle
        with Client(port=server.port, timeout=5) as octave:
            self.assertFalse(octave.trusted)
            self.assertNotIn("app", octave.server)
            self.assertEqual(octave.pair("123456", "test", scopes=["media"]), "t" * 64)
            self.assertTrue(octave.trusted)
            self.assertEqual(octave.scopes, ["read", "media"])
            self.assertEqual(octave.server["app"]["version"], "fake", "hello was asked again after pairing")
            octave.unpair()
            self.assertFalse(octave.trusted)
            self.assertEqual(octave.scopes, [])


if __name__ == "__main__":
    unittest.main()
