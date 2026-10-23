"""The connection to OCTAVE: one SDK Client on a background thread that
connects, subscribes, forwards every event to the app as a Textual message,
and reconnects when the app goes away.

The SDK is imported from the repo (sdk/python), not copied."""

import json
import os
import sys
import threading
import time
from pathlib import Path

from textual.message import Message

_SDK = os.environ.get("OCTAVE_SDK") or str(Path(__file__).resolve().parents[3] / "sdk" / "python")
if _SDK not in sys.path:
    sys.path.insert(0, _SDK)

from octave_client import Client, OctaveError  # noqa: E402

__all__ = ["Link", "OctaveError", "Linked", "Unlinked", "OctaveEvent", "NeedPairing", "TokenStore", "SUBSCRIPTIONS",
           "CONSUMER"]

CONSUMER = "console"   # our name for obdManager.setParameterDemand

# What the console follows: (object, properties, signals, throttle ms).
# paramStore with no properties means every id.
SUBSCRIPTIONS = [
    ("paramStore", None, None, 250),
    ("parameterCatalog", ["displayParameters"], None, 0),
    ("obdManager", ["connected", "connectionState", "connectionStatus", "connectionDetail", "connectionProgress",
                    "connectionLog", "availableAdapters", "vin", "vehicleMake", "vehicleModelYear",
                    "vehicleSupportedParameters", "vehicleScanComplete"],
     ["dtcCodesChanged", "freezeFrameChanged", "dtcClearResult", "milStatusChanged", "connectionLogLineAppended",
      "scanProgressChanged", "scanOutputChanged", "dtcCountChanged"], 0),
    ("obdAdapterFinder", ["supported", "bluetoothOn", "scanning", "devices", "busyAddress", "status",
                          "statusIsError"], None, 0),
    ("tripRecorder", ["enabled", "recording", "currentTripId", "currentTripElapsedS", "currentSampleCount", "trips",
                      "storageBytes", "lastExportPath", "lastError"], ["detailReady", "exportFinished", "seriesReady"], 0),
    ("nowPlaying", ["source", "playing", "hasTrack", "title", "artist", "album", "durationMs", "positionMs",
                    "canSeek", "canShuffle", "shuffled"], None, 250),
    ("volumeController", ["percent", "muted", "effectivelyMuted"], None, 0),
    ("downloadManager", ["isSearching", "activeDownloads", "downloadPath", "downloadsAvailable", "searchResults",
                         "downloadQueue"], ["statusMessage", "searchError", "downloadError"], 0),
    ("clock", ["time"], None, 0),
]


class TokenStore:
    """Paired devices' tokens, one per host:port, in a JSON file only the user
    can read (0600 in a 0700 directory). Default: $XDG_CONFIG_HOME/octave-console/tokens.json."""

    def __init__(self, path=None):
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
        self.path = Path(path) if path else Path(base) / "octave-console" / "tokens.json"

    def _load(self):
        try:
            return json.loads(self.path.read_text())
        except (OSError, ValueError):
            return {}

    def get(self, address):
        return self._load().get(address)

    def put(self, address, token):
        tokens = self._load()
        if token:
            tokens[address] = token
        else:
            tokens.pop(address, None)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        os.chmod(self.path.parent, 0o700)
        fd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            json.dump(tokens, f, indent=1)
        os.chmod(self.path, 0o600)


class NeedPairing(Message):
    """The service wants a paired device (protocol 2, not on its own loopback)."""

    def __init__(self, reason):
        super().__init__()
        self.reason = reason


class Linked(Message):
    """Connected and subscribed. `server` is the hello reply, `describe` every object."""

    def __init__(self, server, describe):
        super().__init__()
        self.server = server
        self.describe = describe


class Unlinked(Message):
    def __init__(self, reason):
        super().__init__()
        self.reason = reason


class OctaveEvent(Message):
    def __init__(self, event):
        super().__init__()
        self.event = event


class Link:
    """Owns the Client. request() may be called from any worker thread."""

    def __init__(self, app, host="127.0.0.1", port=9876, subscriptions=SUBSCRIPTIONS, token=None, tokens=None):
        self.app = app
        self.host = host
        self.port = port
        self.subscriptions = subscriptions
        self.tokens = tokens or TokenStore()
        self.token = token or os.environ.get("OCTAVE_TOKEN") or self.tokens.get(self.address)
        self.client = None
        self._paired = threading.Event()   # set by pair() while the link waits for it
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="octave-link", daemon=True)

    @property
    def address(self):
        return f"{self.host}:{self.port}"

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        client = self.client
        if client is not None:
            try:
                client.call("obdManager", "setParameterDemand", CONSUMER, [])
            except OctaveError:
                pass
            client.close()

    def request(self, op, **args):
        client = self.client
        if client is None:
            raise OctaveError("not connected")
        return client.request(op, **args)

    def call(self, obj, method, *args):
        return self.request("call", object=obj, method=method, args=list(args))

    def pair(self, code, name):
        """Pair with the 6-digit code from OCTAVE's Settings; stores the token."""
        client = self.client
        if client is None:
            raise OctaveError("not connected")
        self.token = client.pair(code, name)
        self.tokens.put(self.address, self.token)
        self._paired.set()
        return self.token

    def http_headers(self):
        """For GET /art and /export: a paired device's token (never in the URL)."""
        return {"Authorization": f"Bearer {self.token}"} if self.token else {}

    def _run(self):
        while not self._stop.is_set():
            try:
                client = Client(self.host, self.port, timeout=15, token=self.token)
            except OctaveError as e:
                if self.token and "token" in str(e):   # revoked or unknown: forget it and pair again
                    self.token = None
                    self.tokens.put(self.address, None)
                    continue
                self.app.post_message(Unlinked(f"no service at {self.address}: {e}"))
                self._stop.wait(1.0)
                continue
            except OSError as e:
                self.app.post_message(Unlinked(f"no service at {self.address}: {e}"))
                self._stop.wait(1.0)
                continue
            try:
                if client.server.get("tokenRejected") and self.token:   # revoked or unknown: forget it
                    self.token = None
                    self.tokens.put(self.address, None)
                if client.server.get("auth", "trusted") != "trusted":
                    # not trusted (another device, or loopback closed): wait for the user to pair
                    self.client = client
                    self._paired.clear()
                    self.app.post_message(NeedPairing(f"{self.address} wants this console paired"))
                    while not self._paired.wait(0.5):
                        if self._stop.is_set() or client._closed:
                            raise OctaveError("closed while pairing")
                describe = client.describe()
                self.client = client
                self.app.post_message(Linked(client.server, describe))
                for obj, props, sigs, throttle in self.subscriptions:
                    if obj == "paramStore" or props or sigs:
                        client.subscribe(obj, signals=sigs, properties=props, throttle_ms=throttle or None)
                for event in client.events():
                    self.app.post_message(OctaveEvent(event))
            except OctaveError as e:
                self.app.post_message(Unlinked(str(e)))
            finally:
                self.client = None
                client.close()
            if not self._stop.is_set():
                self.app.post_message(Unlinked(f"lost {self.address}, retrying"))
                time.sleep(1.0)
