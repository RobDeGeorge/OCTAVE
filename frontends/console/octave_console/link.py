"""The connection to OCTAVE: one SDK Client on a background thread that
connects, subscribes, forwards every event to the app as a Textual message,
and reconnects when the app goes away.

The SDK is imported from the repo (sdk/python), not copied."""

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

__all__ = ["Link", "OctaveError", "Linked", "Unlinked", "OctaveEvent", "SUBSCRIPTIONS", "CONSUMER"]

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
      "scanProgressChanged"], 0),
    ("obdAdapterFinder", ["supported", "bluetoothOn", "scanning", "devices", "busyAddress", "status",
                          "statusIsError"], None, 0),
    ("tripRecorder", ["enabled", "recording", "currentTripId", "currentTripElapsedS", "currentSampleCount", "trips",
                      "storageBytes", "lastExportPath", "lastError"], ["detailReady", "exportFinished"], 0),
    ("nowPlaying", ["source", "playing", "hasTrack", "title", "artist", "album", "durationMs", "positionMs",
                    "canSeek", "canShuffle", "shuffled"], None, 250),
    ("volumeController", ["percent", "muted", "effectivelyMuted"], None, 0),
    ("downloadManager", ["isSearching", "activeDownloads", "downloadPath", "downloadsAvailable", "searchResults",
                         "downloadQueue"], ["statusMessage", "searchError", "downloadError"], 0),
    ("clock", None, ["timeChanged"], 0),
]


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

    def __init__(self, app, host="127.0.0.1", port=9876, subscriptions=SUBSCRIPTIONS):
        self.app = app
        self.host = host
        self.port = port
        self.subscriptions = subscriptions
        self.client = None
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

    def _run(self):
        while not self._stop.is_set():
            try:
                client = Client(self.host, self.port, timeout=15)
            except (OSError, OctaveError) as e:
                self.app.post_message(Unlinked(f"no service at {self.address}: {e}"))
                self._stop.wait(1.0)
                continue
            try:
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
