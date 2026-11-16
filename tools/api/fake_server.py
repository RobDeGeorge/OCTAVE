#!/usr/bin/env python3
"""A fake OCTAVE service for SDK authors: speaks docs/api/protocol.json over
the line protocol without the app. Standard library only.

    python tools/api/fake_server.py                 # 127.0.0.1:9876, prints "listening on PORT"
    python tools/api/fake_server.py --port 0 --untrusted --code 123456

What it serves, from docs/api/: `hello` (protocol version, the objects of
index.json, auth / scopes), `describe` (every object file), and canned data
on a few objects: volumeController (percent, muted, applyVolume, toggleMute,
setMuted), nowPlaying (title, artist, playing, hasTrack, artRevision,
togglePlay, next, previous), paramStore (RPM and SPEED ticking ten times a
second), obdManager (connected, setParameterDemand), parameterCatalog
(info(id) from parameters.json), mediaTicket, a stream
(phoneMirrorManager.video: synthetic H.264-shaped records, a keyframe with
the config flag first). Pairing: `pair` with --code (default 123456) gives a
token and the scopes asked for plus read; a trusted hello with that token
reports them; every other token is rejected. As in the app the code works
once and for two minutes from when it is shown (at start; --code-life 0
keeps it for ever, `fake.showPairingCode(code)` shows a new one). With
--untrusted, connections start untrusted (as over the network): every op
but hello and pair answers unauthorised until they pair, pairing is
throttled per address the way the app throttles a device on the network
(one attempt a second, ten wrong codes and the address is refused until
restart, ten attempts a second over all addresses), and a connection that
has not paired is closed after a minute without a word from it.
Permissions are enforced from the `scope` each member carries in
docs/api/: a device without the scope gets forbidden, like the app; both
streams need the phone permission. Revoking a device (unpair,
`fake.revokeTokens()`) closes its connections after the reply.

Not here, so check a client against a real `octave --headless` as well
(sdk/python/tests/test_protocol_spec.py): the WebSocket transport and the
HTTP routes (/art, /export), so browser clients cannot use this fake;
`throttleMs`; the limits per connection (line length, connections per
address, the ops-per-second budget, except through fake.rateLimitNext);
the `dropped`, `streamConfig` and `streamDropped` events.

A list model to practise on, also on the `fake` object (the app has none
today): `fake.items`, empty (get, rows, model events).

Faults on demand, through a `fake` object the real app does not have:
`fake.rateLimitNext(n)` answers the next n ops on every connection with
rateLimited (retryAfterMs 50); `fake.setForbidden(true)` makes every set /
call forbidden; `fake.revokeTokens()` makes every token unknown (the next
hello with one gets tokenRejected) and closes the connections of paired
devices; `fake.showPairingCode(code)` shows a pairing code again, as
Settings > Service > Pair a device does; `fake.setValue(object, property,
value)` changes a canned property and notifies subscribers;
`fake.emitSignal(object, name, args)` fires a signal.

Every reply and event is validated against protocol.json on the way out
(tools/api/jsonschema_lite.py), so the fake cannot drift from the spec; a
violation is a server error, never a wrong message. Not a security model:
the pairing code is chosen on the command line and tokens live in memory.
"""
import argparse
import itertools
import json
import os
import secrets
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsonschema_lite import SchemaError, Validator  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_API = os.path.join(ROOT, "docs", "api")
FAKE_OBJECT = {
    "class": "FakeServer", "object": "fake", "stability": "experimental",
    "properties": [
        {"name": "items", "type": "QAbstractListModel*", "readable": True, "writable": False, "constant": True},
    ],
    "methods": [
        {"name": "rateLimitNext", "kind": "invokable", "params": [{"name": "count", "type": "int"}], "returns": "", "scope": "read"},
        {"name": "setForbidden", "kind": "invokable", "params": [{"name": "on", "type": "bool"}], "returns": "", "scope": "read"},
        {"name": "revokeTokens", "kind": "invokable", "params": [], "returns": "", "scope": "read"},
        {"name": "showPairingCode", "kind": "invokable", "params": [{"name": "code", "type": "QString"}], "returns": "", "scope": "read"},
        {"name": "setValue", "kind": "invokable", "params": [{"name": "object", "type": "QString"}, {"name": "property", "type": "QString"}, {"name": "value", "type": "QVariant"}], "returns": "", "scope": "read"},
        {"name": "emitSignal", "kind": "invokable", "params": [{"name": "object", "type": "QString"}, {"name": "name", "type": "QString"}, {"name": "args", "type": "QVariantList"}], "returns": "", "scope": "read"},
    ],
    "signals": [],
}


class Reject(Exception):
    """An error reply: message, and a code with extra fields when the spec has one."""

    def __init__(self, message, code=None, **extra):
        super().__init__(message)
        self.code = code
        self.extra = extra


class FakeServer:
    # The app's rules (src/service/servicepairing.h, commandserver.h)
    CODE_LIFE = 120.0             # a pairing code is good for two minutes
    ATTEMPT_INTERVAL = 1.0        # between attempts from one address
    MAX_FAILURES_PER_PEER = 10    # wrong codes per address, until restart
    MAX_ATTEMPTS_PER_SECOND = 10  # over every address
    UNTRUSTED_IDLE = 60.0         # a connection that has not paired, silent this long, is closed

    def __init__(self, api_dir=DEFAULT_API, port=9876, host="127.0.0.1", untrusted=False, code="123456",
                 code_life=CODE_LIFE, untrusted_idle=UNTRUSTED_IDLE, attempt_interval=ATTEMPT_INTERVAL):
        self.api_dir = api_dir
        with open(os.path.join(api_dir, "protocol.json"), encoding="utf-8") as fh:
            self.spec = json.load(fh)
        self.validator = Validator(self.spec)
        with open(os.path.join(api_dir, "index.json"), encoding="utf-8") as fh:
            self.index = json.load(fh)
        self.objects = {}
        for name in self.index["objects"]:
            with open(os.path.join(api_dir, f"{name}.json"), encoding="utf-8") as fh:
                self.objects[name] = json.load(fh)
        self.objects["fake"] = FAKE_OBJECT
        with open(os.path.join(api_dir, "parameters.json"), encoding="utf-8") as fh:
            self.parameters = {p["id"]: p for p in json.load(fh)}
        self.version = self.spec["protocolVersion"]
        self.host, self.port = host, port
        self.untrusted = untrusted
        self.code = code            # "" once used or expired: single use
        self.code_life = code_life  # seconds; 0 = never expires
        self.code_shown = time.monotonic()
        self.untrusted_idle = untrusted_idle
        self.attempt_interval = attempt_interval
        self.last_attempt = {}      # address -> monotonic time of its last pairing attempt
        self.failures = {}          # address -> wrong codes
        self.window_start, self.window_attempts = 0.0, 0
        self.lock = threading.RLock()
        self.tokens = {}            # token -> scopes
        self.tickets = set()
        self.rate_limit_left = 0
        self.forbid_all = False
        self.demand = {}
        self.connections = set()
        self.state = {
            "volumeController": {"percent": 32, "muted": False},
            "nowPlaying": {"title": "Idle", "artist": "OCTAVE Simulator", "album": "", "playing": False, "hasTrack": True,
                           "artRevision": 1, "source": "local", "positionMs": 0, "durationMs": 180000},
            "obdManager": {"connected": True, "connectionState": 2},
            "paramStore": {"RPM": 800.0, "SPEED": 0.0},
            "fake": {"items": []},
        }
        self._stop = threading.Event()
        self._t0 = time.monotonic()

    # ── lifecycle ─────────────────────────────────────────────────────────

    def start(self):
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(16)
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()
        threading.Thread(target=self._tick, daemon=True).start()
        return self

    def stop(self):
        self._stop.set()
        try:
            self.sock.close()
        except OSError:
            pass
        with self.lock:
            for c in list(self.connections):
                c.close()

    def _accept(self):
        while not self._stop.is_set():
            try:
                sock, _ = self.sock.accept()
            except OSError:
                return
            Connection(self, sock).start()

    def _tick(self):
        """The simulated car: RPM and speed move ten times a second."""
        while not self._stop.wait(0.1):
            t = time.monotonic() - self._t0
            with self.lock:
                store = self.state["paramStore"]
                store["RPM"] = round(800 + 1200 * (0.5 + 0.5 * __import__("math").sin(t)), 0)
                store["SPEED"] = round(30 + 20 * __import__("math").sin(t / 3), 1)
                values = dict(store)
                conns = list(self.connections)
            now = time.monotonic()
            for c in conns:
                # An untrusted client gets a minute of silence, then it is closed (commandserver.cpp)
                if not c.trusted and self.untrusted_idle and now - c.last_heard > self.untrusted_idle:
                    c.close()
                    continue
                c.params_changed(values)

    # ── pairing, by the app's rules (servicepairing.cpp) ──────────────────

    def show_pairing_code(self, code):
        with self.lock:
            self.code = str(code)
            self.code_shown = time.monotonic()

    def pair(self, conn, code, asked):
        known = self.spec["$defs"]["scope"]["enum"]
        for s in asked:      # a bad request costs no attempt and doesn't use the code
            if s not in known:
                raise Reject(f"unknown permission {s} (known: {', '.join(known)})")
        with self.lock:
            now = time.monotonic()
            # Guesses are limited for connections that are not this device's own; --untrusted
            # stands for "over the network", so that is when the limits apply
            limited = self.untrusted
            if limited:
                if self.failures.get(conn.peer, 0) >= self.MAX_FAILURES_PER_PEER:
                    raise Reject("too many wrong codes from this address")
                last = self.last_attempt.get(conn.peer)
                if last is not None and now - last < self.attempt_interval:
                    raise Reject("too fast, try again in a second")
                if now - self.window_start >= 1.0:
                    self.window_start, self.window_attempts = now, 0
                if self.window_attempts >= self.MAX_ATTEMPTS_PER_SECOND:
                    raise Reject("too many pairing attempts right now, try again in a second")
                self.window_attempts += 1
                self.last_attempt[conn.peer] = now
            if not self.code or (self.code_life and now - self.code_shown >= self.code_life):
                raise Reject("no pairing code is showing on the head unit")
            if str(code) != self.code:
                if limited:
                    self.failures[conn.peer] = self.failures.get(conn.peer, 0) + 1
                raise Reject("wrong code")
            self.code = ""   # single use
            scopes = ["full"] if not asked or "full" in asked else sorted({"read", *asked})
            token = secrets.token_hex(32)
            self.tokens[token] = scopes
        return token, scopes

    def revoke(self, tokens):
        """Forget these tokens and, as the app does, close the connections of
        their devices: a moment later, so the reply to the op that asked goes
        out first (the app queues the revocation the same way)."""
        with self.lock:
            for t in tokens:
                self.tokens.pop(t, None)
            self.tickets.clear()
            doomed = [c for c in self.connections if c.token in tokens]

        def close():
            for c in doomed:
                c.close()
        threading.Timer(0.05, close).start()

    def ms(self):
        return int((time.monotonic() - self._t0) * 1000)

    # ── the objects ───────────────────────────────────────────────────────

    def object(self, name):
        if name not in self.objects:
            raise Reject(f"Unknown object: {name}")
        return self.objects[name]

    def member(self, obj_name, kind, name):
        for m in self.object(obj_name)[kind]:
            if m["name"] == name:
                return m
        what = {"properties": "property", "methods": "method", "signals": "signal"}[kind]
        raise Reject(f"{self.object(obj_name)['class']} has no {what} {name}")

    def check_scope(self, conn, obj_name, member):
        need = member.get("scope", "full")
        have = conn.scopes
        if self.forbid_all or ("full" not in have and need != "read" and need not in have):
            raise Reject(f"{obj_name}.{member['name']} needs the {need} permission; this device has {', '.join(have)}", "forbidden")

    def set_value(self, obj_name, prop, value):
        with self.lock:
            self.state.setdefault(obj_name, {})[prop] = value
            conns = list(self.connections)
        for c in conns:
            c.property_changed(obj_name, prop, value)

    def emit(self, obj_name, signal, args):
        with self.lock:
            conns = list(self.connections)
        for c in conns:
            c.signal_fired(obj_name, signal, args)

    def call(self, conn, obj_name, method, args):
        m = [x for x in self.object(obj_name)["methods"] if x["name"] == method]
        if not m:
            raise Reject(f"{self.object(obj_name)['class']} has no method {method}")
        exact = [x for x in m if len(x["params"]) == len(args)]
        if not exact:
            raise Reject(f"{self.object(obj_name)['class']}.{method} takes {', '.join(str(len(x['params'])) for x in m)} arguments, not {len(args)}")
        self.check_scope(conn, obj_name, exact[0])
        st = self.state.get(obj_name, {})
        key = f"{obj_name}.{method}"
        if key == "volumeController.applyVolume":
            self.set_value(obj_name, "percent", max(0, min(100, int(args[0]))))
            if st["muted"] and args[0] > 0:
                self.set_value(obj_name, "muted", False)
            return None
        if key == "volumeController.toggleMute":
            self.set_value(obj_name, "muted", not st["muted"])
            return None
        if key == "nowPlaying.togglePlay":
            self.set_value(obj_name, "playing", not st["playing"])
            return None
        if key in ("nowPlaying.next", "nowPlaying.previous"):
            self.set_value(obj_name, "title", f"Track {secrets.randbelow(100)}")
            self.set_value(obj_name, "artRevision", st["artRevision"] + 1)
            return None
        if key == "parameterCatalog.info":
            return self.parameters.get(str(args[0]), {})
        if key in ("obdManager.setParameterDemand", "obdManager.setBackgroundDemand"):
            self.demand[str(args[0])] = list(args[1])
            return None
        if key == "fake.rateLimitNext":
            self.rate_limit_left = int(args[0])
            return None
        if key == "fake.setForbidden":
            self.forbid_all = bool(args[0])
            return None
        if key == "fake.revokeTokens":
            self.revoke(list(self.tokens))
            return None
        if key == "fake.showPairingCode":
            self.show_pairing_code(args[0])
            return None
        if key == "fake.setValue":
            self.set_value(str(args[0]), str(args[1]), args[2])
            return None
        if key == "fake.emitSignal":
            self.emit(str(args[0]), str(args[1]), list(args[2]))
            return None
        raise Reject(f"fake server: {key} has no canned behaviour (see tools/api/fake_server.py)")


class Connection:
    def __init__(self, server, sock):
        self.server = server
        self.sock = sock
        try:
            self.peer = sock.getpeername()[0]
        except OSError:
            self.peer = ""
        self.last_heard = time.monotonic()
        self.file = sock.makefile("rb")
        self.wlock = threading.Lock()
        self.trusted = not server.untrusted
        self.scopes = ["full"] if self.trusted else []
        self.token = None
        self.subs = {}            # id -> {object, properties, signals}
        self.streams = {}         # id -> name
        self.ids = itertools.count(1)
        self.stream_ids = itertools.count(1)
        self.closed = False

    def start(self):
        with self.server.lock:
            self.server.connections.add(self)
        threading.Thread(target=self._serve, daemon=True).start()

    def close(self):
        self.closed = True
        with self.server.lock:
            self.server.connections.discard(self)
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()

    # ── wire ──────────────────────────────────────────────────────────────

    def send(self, obj, schema=None):
        if schema is not None:
            try:
                self.server.validator.validate(obj, schema)
            except SchemaError as e:
                raise RuntimeError(f"fake server would send a message that breaks protocol.json: {e}\n{obj}") from e
        line = (json.dumps(obj) + "\n").encode()
        with self.wlock:
            try:
                self.sock.sendall(line)
            except OSError:
                self.close()

    def send_record(self, stream_id, flags, pts_us, payload):
        rec = struct.pack(">BBBBQ", 1, stream_id, flags, 0, pts_us) + payload
        with self.wlock:
            try:
                self.sock.sendall(b"\xff" + struct.pack(">I", len(rec)) + rec)
            except OSError:
                self.close()

    def _serve(self):
        try:
            while not self.closed:
                line = self.file.readline()
                if not line:
                    break
                self.last_heard = time.monotonic()
                if not line.strip():
                    continue
                self.send(self.handle(line), self.server.spec["reply"]["schema"])
        except (OSError, RuntimeError) as e:
            if isinstance(e, RuntimeError):
                print(f"fake server: {e}", file=sys.stderr)
        finally:
            self.close()

    def handle(self, line):
        try:
            msg = json.loads(line)
            if not isinstance(msg, dict):
                raise ValueError("not an object")
        except ValueError as e:
            return {"ok": False, "error": f"Invalid JSON: {e}"}
        reply = {"id": msg["id"]} if "id" in msg else {}
        op = msg.get("op")
        if op is None:
            return {**reply, "ok": False, "error": "Unknown action: this is the fake server; send ops"}
        try:
            if self.server.rate_limit_left > 0:
                self.server.rate_limit_left -= 1
                raise Reject("too many ops: 200 a second", "rateLimited", retryAfterMs=50)
            if not self.trusted and op not in ("hello", "pair"):
                raise Reject("not paired: pair with the code shown in OCTAVE's Settings", "unauthorised")
            fn = getattr(self, "op_" + op, None)
            if fn is None:
                raise Reject(f"Unknown op: {op}")
            args = {k: v for k, v in msg.items() if k not in ("op", "id")}
            result = fn(args)
            self.server.validator.validate(result, self.server.spec["ops"][op]["reply"])
            return {**reply, "ok": True, "result": result}
        except Reject as e:
            out = {**reply, "ok": False, "error": str(e)}
            if e.code:
                out["code"] = e.code
            out.update(e.extra)
            return out
        except SchemaError as e:
            raise RuntimeError(f"{op} reply: {e}") from e

    # ── ops ───────────────────────────────────────────────────────────────

    def op_hello(self, args):
        token = args.get("token")
        rejected = False
        if token:
            scopes = self.server.tokens.get(token)
            if scopes is None:
                rejected, self.token = True, None
                if self.server.untrusted:
                    self.trusted, self.scopes = False, []
            else:
                self.token, self.trusted, self.scopes = token, True, scopes
        reply = {"protocolVersion": self.server.version, "auth": "trusted" if self.trusted else "untrusted",
                 "loopbackOpen": not self.server.untrusted}
        if self.trusted:
            reply["app"] = {"version": "fake", "build": "release", "headless": True}
            reply["objects"] = sorted(self.server.objects)
            reply["scopes"] = self.scopes
        if rejected:
            reply["tokenRejected"] = True
        return reply

    def op_describe(self, args):
        if args.get("object"):
            return {k: v for k, v in self.server.object(args["object"]).items() if k != "object"}
        return {"protocolVersion": self.server.version,
                "objects": {n: {k: v for k, v in o.items() if k != "object"} for n, o in self.server.objects.items()},
                "ops": sorted(self.server.spec["ops"])}

    def _state(self, obj_name):
        self.server.object(obj_name)
        st = self.server.state.get(obj_name)
        if st is None:
            raise Reject(f"fake server: {obj_name} has no canned data (see tools/api/fake_server.py)")
        return st

    def op_get(self, args):
        obj_name = args.get("object", "")
        st = self._state(obj_name)
        prop = args.get("property")
        if obj_name == "paramStore":
            return dict(st) if not prop else st.get(prop)
        if not prop:
            raise Reject("get needs a property")
        self.server.member(obj_name, "properties", prop)
        value = st.get(prop)
        if obj_name == "fake" and prop == "items":
            return {"model": True, "count": len(value), "roles": ["title"]}
        return value

    def op_set(self, args):
        obj_name, prop = args.get("object", ""), args.get("property", "")
        st = self._state(obj_name)
        if obj_name == "paramStore":
            raise Reject(f"{prop} is read-only")
        m = self.server.member(obj_name, "properties", prop)
        if not m.get("writable"):
            raise Reject(f"{prop} is read-only")
        self.server.check_scope(self, obj_name, m)
        value = args.get("value")
        if m["type"] == "bool" and not isinstance(value, bool):
            raise Reject(f"{prop} takes a bool")
        if m["type"] in ("int", "double", "float") and not isinstance(value, (int, float)):
            raise Reject(f"{prop} takes a {m['type']}")
        self.server.set_value(obj_name, prop, value)
        return st[prop]

    def op_call(self, args):
        return self.server.call(self, args.get("object", ""), args.get("method", ""), list(args.get("args") or []))

    def op_rows(self, args):
        st = self._state(args.get("object", ""))
        rows = st.get(args.get("property"))
        if not isinstance(rows, list):
            raise Reject(f"{args.get('property')} is not a list model")
        start = max(0, int(args.get("from", 0)))
        count = args.get("count")
        return rows[start:] if count is None else rows[start:start + int(count)]

    def op_subscribe(self, args):
        obj_name = args.get("object", "")
        self.server.object(obj_name)
        props, sigs = list(args.get("properties") or []), list(args.get("signals") or [])
        if obj_name != "paramStore":
            for p in props:
                self.server.member(obj_name, "properties", p)
            for s in sigs:
                self.server.member(obj_name, "signals", s)
            if not props and not sigs:
                raise Reject("subscribe needs signals or properties")
        sub = next(self.ids)
        self.subs[sub] = {"object": obj_name, "properties": props, "signals": sigs}
        # The current values follow the reply at once. The reply has to go out first and handle()
        # only returns it, so the values are sent from a timer thread 10 ms later (the app sends
        # them on the connection's own thread right after the reply)
        threading.Timer(0.01, self._initial, (sub,)).start()
        return {"sub": sub}

    def _initial(self, sub):
        s = self.subs.get(sub)
        if not s:
            return
        st = self.server.state.get(s["object"], {})
        if s["object"] == "paramStore":
            values = {k: v for k, v in st.items() if not s["properties"] or k in s["properties"]}
            self.event({"event": "params", "sub": sub, "object": "paramStore", "values": values, "t": self.server.ms()})
            return
        for p in s["properties"]:
            value = st.get(p)
            if isinstance(value, list):
                self.event({"event": "model", "sub": sub, "object": s["object"], "name": p, "change": "reset",
                            "first": 0, "last": len(value) - 1, "rows": value, "t": self.server.ms()})
            else:
                self.event({"event": "property", "sub": sub, "object": s["object"], "name": p, "value": value, "t": self.server.ms()})

    def op_unsubscribe(self, args):
        if self.subs.pop(args.get("sub"), None) is None:
            raise Reject(f"No subscription {args.get('sub')}")
        return True

    def op_stream(self, args):
        name = f"{args.get('object')}.{args.get('name')}"
        self.server.object(args.get("object", ""))
        # A stream whose owner is in the phone scope (the phone mirror, Android Auto: a phone's
        # screen either way) needs that permission, checked before the name as in servicebridge.cpp
        if args.get("object") in ("phoneMirrorManager", "androidAutoManager"):
            self.server.check_scope(self, args.get("object", ""), {"name": args.get("name"), "scope": "phone"})
        if name not in self.server.spec["streams"]["names"]:
            raise Reject(f"{args.get('object')} has no stream {args.get('name')}")
        sid = next(self.stream_ids)
        self.streams[sid] = name
        fmt = self.server.spec["streams"]["names"][name]["format"]
        threading.Thread(target=self._pump, args=(sid, fmt), daemon=True).start()
        return {"stream": sid, "format": fmt, "codec": "avc1.42c01e", "width": 640, "height": 360} if fmt == "annexb" else {"stream": sid, "format": fmt}

    def _pump(self, sid, fmt):
        """Thirty records a second: a keyframe with the config first, then deltas; a keyframe every 30."""
        n = 0
        while not self.closed and sid in self.streams:
            key = n % 30 == 0
            if fmt == "jpeg":
                flags, payload = 1 | 4 | (8 if n == 0 else 0), b"\xff\xd8\xff\xe0" + bytes(64) + b"\xff\xd9"
            else:
                sps = b"\x00\x00\x00\x01\x67\x42\xc0\x1e" + bytes(8) + b"\x00\x00\x00\x01\x68\xce\x38\x80"
                nal = b"\x00\x00\x00\x01" + (b"\x65" if key else b"\x41") + bytes(32)
                flags, payload = ((1 | 2) if key else 0) | (8 if n == 0 else 0), (sps + nal if key else nal)
            self.send_record(sid, flags, n * 33333, payload)
            n += 1
            time.sleep(1 / 30)

    def op_unstream(self, args):
        self.streams.pop(args.get("stream"), None)
        return True

    def op_pair(self, args):
        token, scopes = self.server.pair(self, args.get("code"), list(args.get("scopes") or []))
        self.token, self.trusted, self.scopes = token, True, scopes
        return {"token": token, "scopes": scopes}

    def op_unpair(self, args):
        if not self.token:
            raise Reject("this connection is not a paired device")
        self.server.revoke([self.token])   # closes this device's connections, this one too, after the reply
        return True

    def op_mediaTicket(self, args):
        ticket = secrets.token_hex(16)
        self.server.tickets.add(ticket)
        return {"ticket": ticket, "expiresIn": 600}

    # ── events ────────────────────────────────────────────────────────────

    def event(self, ev):
        self.send(ev, self.server.spec["events"][ev["event"]]["schema"])

    def params_changed(self, values):
        for sub, s in list(self.subs.items()):
            if s["object"] == "paramStore":
                sel = {k: v for k, v in values.items() if not s["properties"] or k in s["properties"]}
                self.event({"event": "params", "sub": sub, "object": "paramStore", "values": sel, "t": self.server.ms()})

    def property_changed(self, obj_name, prop, value):
        for sub, s in list(self.subs.items()):
            if s["object"] == obj_name and prop in s["properties"]:
                self.event({"event": "property", "sub": sub, "object": obj_name, "name": prop, "value": value, "t": self.server.ms()})

    def signal_fired(self, obj_name, name, args):
        for sub, s in list(self.subs.items()):
            if s["object"] == obj_name and name in s["signals"]:
                self.event({"event": "signal", "sub": sub, "object": obj_name, "name": name, "args": args, "t": self.server.ms()})


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=9876, help="0 for any free port")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--api", default=DEFAULT_API, help="the docs/api directory to serve")
    parser.add_argument("--untrusted", action="store_true", help="connections start untrusted, as over the network")
    parser.add_argument("--code", default="123456", help="the pairing code (single use)")
    parser.add_argument("--code-life", type=float, default=FakeServer.CODE_LIFE, metavar="SECONDS",
                        help="how long the code is good for from start, as in the app (default 120; 0 = for ever)")
    args = parser.parse_args()
    server = FakeServer(args.api, args.port, args.host, args.untrusted, args.code, code_life=args.code_life).start()
    print(f"listening on {server.port}", flush=True)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        server.stop()


if __name__ == "__main__":
    main()
