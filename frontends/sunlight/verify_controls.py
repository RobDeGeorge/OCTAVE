"""Tap sunlight's touch controls on an X display and read the results back
through the Python SDK (sdk/python), so the check is independent of sunlight's
own client.

    Xvfb :83 -screen 0 1280x800x24 &
    python3 frontends/sunlight/verify_controls.py --display :83 --port 9876 --bin ./sunlight

Needs a running OCTAVE service with music to play (octave --dev --profile or
--headless --dev against a test library) and libXtst. Changes volume, mute,
shuffle and playback, and puts volume, mute and shuffle back afterwards. On
the DTC page it reads the codes and clears them through the two-tap confirm:
use the simulated car, whose codes come back on its next start."""

import argparse
import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "python"))

from octave_client import Client  # noqa: E402

# Touch targets on the 1280x800 MEDIA page (window at 1:1 scale)
TARGETS = {
    "prev": (163, 605), "play": (480, 605), "next": (797, 605), "shuffle": (1115, 605),
    "vol_down": (100, 732), "vol_up": (290, 732), "mute": (1180, 732), "vol_bar": (735, 732),
    "seek": (640, 519),
    # the top bar: DRIVE DASH MEDIA DTC TRIPS
    "tab_drive": (80, 40), "tab_dash": (230, 40), "tab_media": (380, 40), "tab_dtc": (530, 40), "tab_trips": (680, 40),
    # DTC page
    "dtc_read": (216, 740), "dtc_clear": (639, 740),
    # TRIPS page: the first (newest) trip
    "trip_first": (293, 141),
}


class Clicker:
    def __init__(self, display):
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.xt = ctypes.CDLL("libXtst.so.6")
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.d = ctypes.c_void_p(self.x11.XOpenDisplay(display.encode()))
        if not self.d.value:
            sys.exit(f"cannot open display {display}")

    def click(self, x, y):
        self.xt.XTestFakeMotionEvent(self.d, 0, x, y, 0)
        self.x11.XFlush(self.d)
        time.sleep(0.15)
        self.xt.XTestFakeButtonEvent(self.d, 1, 1, 0)
        self.x11.XFlush(self.d)
        time.sleep(0.1)
        self.xt.XTestFakeButtonEvent(self.d, 1, 0, 0)
        self.x11.XFlush(self.d)


def wait(cond, timeout=4.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--display", default=":83")
    ap.add_argument("--port", type=int, default=9876)
    ap.add_argument("--bin", default="./sunlight")
    args = ap.parse_args()

    env = {k: v for k, v in os.environ.items() if k != "WAYLAND_DISPLAY"} | {"DISPLAY": args.display}
    failures = []
    with Client(port=args.port) as sdk:   # connect first: no sunlight window is left behind if this fails
        saved = {k: sdk.get("volumeController", k) for k in ("percent", "muted")}
        saved_shuffle = sdk.get("nowPlaying", "shuffled")
        proc = subprocess.Popen([args.bin, "--port", str(args.port), "--page", "media"], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(3)   # window up, linked
            tap = Clicker(args.display)

            def check(name, target, cond):
                tap.click(*TARGETS[target])
                ok = wait(cond)
                print(f"{'ok  ' if ok else 'FAIL'} {name}")
                if not ok:
                    failures.append(name)

            sdk.call("volumeController", "applyVolume", 50)
            sdk.set("volumeController", "muted", False)
            time.sleep(0.5)
            check("VOL + raises the volume by 5", "vol_up", lambda: sdk.get("volumeController", "percent") == 55)
            check("VOL - lowers it by 5", "vol_down", lambda: sdk.get("volumeController", "percent") == 50)
            check("tapping the volume bar sets it from the position", "vol_bar",
                  lambda: 45 <= sdk.get("volumeController", "percent") <= 60)
            check("MUTE mutes", "mute", lambda: sdk.get("volumeController", "muted") is True)
            time.sleep(0.3)
            check("MUTE again unmutes", "mute", lambda: sdk.get("volumeController", "muted") is False)
            was = sdk.get("nowPlaying", "playing")
            check("PLAY / PAUSE toggles playback", "play", lambda: sdk.get("nowPlaying", "playing") != was)
            if not sdk.get("nowPlaying", "playing"):
                tap.click(*TARGETS["play"])
                wait(lambda: sdk.get("nowPlaying", "playing"))
            title = sdk.get("nowPlaying", "title")
            check("NEXT changes the track", "next", lambda: sdk.get("nowPlaying", "title") != title)
            time.sleep(0.5)
            if sdk.get("nowPlaying", "canSeek") and sdk.get("nowPlaying", "durationMs"):
                check("tapping the middle of the position bar seeks there", "seek",
                      lambda: abs(sdk.get("nowPlaying", "positionMs") / sdk.get("nowPlaying", "durationMs") - 0.5) < 0.1)
            shuffled = sdk.get("nowPlaying", "shuffled")
            check("SHUFFLE toggles shuffle", "shuffle", lambda: sdk.get("nowPlaying", "shuffled") != shuffled)
            tap.click(*TARGETS["play"])   # leave it paused

            # DTC: opening the page reads the codes; CLEAR needs a second tap within 4 s
            tap.click(*TARGETS["tab_dtc"])
            if wait(lambda: sdk.call("obdManager", "get_dtc_codes"), timeout=6):
                print("ok   opening the DTC page reads the stored codes")
                tap.click(*TARGETS["dtc_clear"])
                time.sleep(1.0)
                ok = bool(sdk.call("obdManager", "get_dtc_codes"))
                print(f"{'ok  ' if ok else 'FAIL'} one tap on CLEAR only arms it")
                if not ok:
                    failures.append("one tap on CLEAR only arms it")
                check("a second tap clears the codes", "dtc_clear", lambda: not sdk.call("obdManager", "get_dtc_codes"))
            else:
                tap.click(*TARGETS["dtc_read"])
                print("skip the car has no stored codes to read and clear (cleared earlier?)")

            # TRIPS: tapping a trip asks for its summary
            trips = sorted(sdk.get("tripRecorder", "trips"), key=lambda t: t.get("startIso", ""), reverse=True)
            if trips:
                sub = sdk.subscribe("tripRecorder", signals=["detailReady"])
                tap.click(*TARGETS["tab_trips"])
                time.sleep(1.0)
                for _ in sdk.events(timeout=0.2):   # drop the page's own first request
                    pass
                tap.click(*TARGETS["trip_first"])
                got = [e for e in sdk.events(timeout=4) if e.get("name") == "detailReady"]
                ok = any(e["args"][0] == trips[0]["id"] for e in got)
                print(f"{'ok  ' if ok else 'FAIL'} tapping the newest trip loads its summary")
                if not ok:
                    failures.append("tapping the newest trip loads its summary")
                sdk.unsubscribe(sub)
        finally:
            sdk.call("volumeController", "applyVolume", saved["percent"])
            sdk.set("volumeController", "muted", saved["muted"])
            if sdk.get("nowPlaying", "shuffled") != saved_shuffle:
                sdk.call("nowPlaying", "toggleShuffle")
            proc.terminate()
            proc.wait(timeout=5)
    print("all controls read back" if not failures else f"{len(failures)} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
