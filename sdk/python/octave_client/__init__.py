"""Python client for OCTAVE's service API (stdlib only).

    from octave_client import Client

    with Client() as octave:                       # 127.0.0.1:9876
        print(octave.hello()["objects"])
        octave.call("volumeController", "applyVolume", 30)
        print(octave.get("nowPlaying", "title"))
        octave.subscribe("paramStore", properties=["RPM"], throttle_ms=100)
        for event in octave.events():              # blocks; Ctrl-C to stop
            print(event["values"])

The app must be serving: `octave --service`, `octave --headless`, the
serviceEnabled setting, or `--profile` in a developer build. Protocol:
wiki/utilities.html#service-protocol; objects, types and the versioning rule:
docs/api/ (this client speaks protocol versions 1 and 2).

Over the network (an app started with --service-bind) a client pairs once:
show the code in OCTAVE's Settings > Service, then

    with Client("192.168.1.20") as octave:
        token = octave.pair("123456", "My script")   # keep it somewhere safe
    with Client("192.168.1.20", token=token) as octave:
        ...

On this device no pairing is needed unless serviceLoopbackOpen is false.
"""

import itertools
import json
import queue
import socket
import struct
import threading

from .launch import find_binary, launch_headless

__all__ = ["Client", "OctaveError", "PROTOCOL_VERSION", "PROTOCOL_VERSIONS", "find_binary", "launch_headless"]

PROTOCOL_VERSION = 2          # the newest this client knows
PROTOCOL_VERSIONS = (1, 2)    # every version it can talk to


def _record(raw):
    """A stream record as an event dict (header: u8 version, u8 stream,
    u8 flags, u8 reserved, u64 pts in microseconds)."""
    _version, stream, flags, _reserved, pts = struct.unpack(">BBBBQ", raw[:12])
    return {"event": "record", "stream": stream, "flags": flags, "pts": pts,
            "key": bool(flags & 1), "config": bool(flags & 2), "jpeg": bool(flags & 4),
            "discontinuity": bool(flags & 8), "data": raw[12:]}


class OctaveError(RuntimeError):
    """The app answered {"ok": false, "error": ...}, or the connection went."""


class Client:
    """One connection. Requests may come from any thread; replies are matched
    by id, so subscription events can arrive in between.

    Connecting asks `hello` (with `token`, a paired device's, if given) and
    raises OctaveError if the app speaks a protocol version this client
    doesn't know (a version only changes on a breaking change); pass
    check_version=False to talk to it anyway. The reply is in `self.server`;
    `self.trusted` says whether ops will be accepted (if not, pair())."""

    def __init__(self, host="127.0.0.1", port=9876, timeout=30.0, check_version=True, token=None):
        self.timeout = timeout
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._sock.settimeout(None)
        self._ids = itertools.count(1)
        self._lock = threading.Lock()
        self._pending = {}
        self._events = queue.Queue()
        self._closed = False
        self._reader = threading.Thread(target=self._read, name="octave-client", daemon=True)
        self._reader.start()
        self.server = self.hello(token)
        if check_version and self.server.get("protocolVersion") not in PROTOCOL_VERSIONS:
            self.close()
            raise OctaveError(f"the app speaks protocol {self.server.get('protocolVersion')}, "
                              f"this client knows {', '.join(map(str, PROTOCOL_VERSIONS))} (docs/api/README.md)")
        # Version 1 has no pairing: everything is accepted
        self.trusted = self.server.get("auth", "trusted") == "trusted"

    # ── protocol ops ──────────────────────────────────────────────────────

    def hello(self, token=None):
        """{protocolVersion, app: {version, build, headless}, objects: [...],
        auth: "trusted" | "untrusted", loopbackOpen}. A paired device's
        `token` makes this connection trusted."""
        return self.request("hello", **({"token": token} if token else {}))

    def pair(self, code, name="octave_client"):
        """Pair with the 6-digit code OCTAVE's Settings > Service shows and
        return the token (store it; pass Client(token=...) next time). This
        connection is trusted from then on."""
        token = self.request("pair", code=str(code), name=name)["token"]
        self.trusted = True
        return token

    def media_ticket(self):
        """{ticket, expiresIn}: for GET /art?ticket=... and /export/...?ticket=...,
        so no token goes in a URL."""
        return self.request("mediaTicket")

    def describe(self, obj=None):
        """One object's properties / methods / signals, or every object's."""
        return self.request("describe", **({"object": obj} if obj else {}))

    def get(self, obj, prop=None):
        """A property's value; on paramStore `prop` is a parameter id (none = all)."""
        return self.request("get", object=obj, **({"property": prop} if prop else {}))

    def set(self, obj, prop, value):
        """Write a writable property; returns the value read back."""
        return self.request("set", object=obj, property=prop, value=value)

    def call(self, obj, method, *args, timeout_ms=None):
        """Call a slot or invokable; returns its value (None for void)."""
        extra = {"timeoutMs": timeout_ms} if timeout_ms else {}
        return self.request("call", object=obj, method=method, args=list(args), **extra)

    def rows(self, obj, prop, start=0, count=None):
        """Rows of a list-model property as {role: value} dicts."""
        extra = {"count": count} if count is not None else {}
        return self.request("rows", object=obj, property=prop, **{"from": start}, **extra)

    def subscribe(self, obj, signals=None, properties=None, throttle_ms=None):
        """Stream signals / property changes (or paramStore ids) as events;
        returns the subscription id. Read them with events()."""
        args = {"object": obj}
        if signals:
            args["signals"] = list(signals)
        if properties:
            args["properties"] = list(properties)
        if throttle_ms:
            args["throttleMs"] = throttle_ms
        return self.request("subscribe", **args)["sub"]

    def unsubscribe(self, sub):
        return self.request("unsubscribe", sub=sub)

    def stream(self, obj, name):
        """Media records (docs/api/README.md, "Streams"): e.g.
        stream("phoneMirrorManager", "video"). Returns the reply ({stream,
        format, codec?, width?, height?}); then events() yields
        {"event": "record", "stream", "flags", "pts", "key", "config",
        "data": bytes} (H.264 Annex-B, or JPEG) and "streamConfig" /
        "streamDropped" events."""
        return self.request("stream", object=obj, name=name)

    def unstream(self, stream_id):
        return self.request("unstream", stream=stream_id)

    def events(self, timeout=None):
        """Yield events as they come. With `timeout`, stop after that many
        seconds without one; without, run until the connection closes."""
        while True:
            try:
                event = self._events.get(timeout=timeout)
            except queue.Empty:
                return
            if event is None:
                return
            yield event

    def request(self, op, **args):
        """Any op by name, for ones this class has no method for."""
        reply_box = queue.Queue(maxsize=1)
        with self._lock:
            if self._closed:
                raise OctaveError("connection closed")
            rid = next(self._ids)
            self._pending[rid] = reply_box
            try:
                self._sock.sendall((json.dumps({"op": op, "id": rid, **args}) + "\n").encode())
            except OSError as e:
                self._pending.pop(rid, None)
                raise OctaveError(f"connection lost: {e}") from e
        try:
            reply = reply_box.get(timeout=self.timeout)
        except queue.Empty:
            with self._lock:
                self._pending.pop(rid, None)
            raise OctaveError(f"{op}: no reply within {self.timeout} s") from None
        if reply is None:
            raise OctaveError("connection closed")
        if not reply.get("ok"):
            raise OctaveError(reply.get("error", "unknown error"))
        return reply.get("result")

    # ── plumbing ──────────────────────────────────────────────────────────

    def close(self):
        with self._lock:
            if self._closed:
                return
            self._closed = True
        try:
            self._sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._sock.close()
        self._reader.join(timeout=2)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read(self):
        buf = b""
        try:
            while True:
                chunk = self._sock.recv(1 << 16)
                if not chunk:
                    break
                buf += chunk
                while buf:
                    # A stream record: 0xFF, big-endian u32 length, the record
                    # (a JSON line never starts with 0xFF)
                    if buf[0] == 0xFF:
                        if len(buf) < 5:
                            break
                        n = struct.unpack(">I", buf[1:5])[0]
                        if len(buf) < 5 + n:
                            break
                        self._events.put(_record(buf[5:5 + n]))
                        buf = buf[5 + n:]
                        continue
                    nl = buf.find(b"\n")
                    if nl < 0:
                        break
                    line, buf = buf[:nl], buf[nl + 1:]
                    if line.strip():
                        self._dispatch(json.loads(line))
        except (OSError, ValueError):   # gone, or not the protocol: fail what waits
            pass
        finally:
            with self._lock:
                self._closed = True
                pending, self._pending = self._pending, {}
            for box in pending.values():
                box.put(None)
            self._events.put(None)

    def _dispatch(self, message):
        if "event" in message:
            self._events.put(message)
            return
        with self._lock:
            box = self._pending.pop(message.get("id"), None)
        if box is not None:
            box.put(message)
