#!/usr/bin/env python3
"""Hello, car: the smallest OCTAVE frontend in Python. Connects, pairs when
this machine is not the head unit (asking only for the media permission),
shows RPM and speed live, the track with play / pause on Enter, and saves
the cover next to this file.

    python hello_car.py                      # 127.0.0.1:9876
    python hello_car.py 192.168.1.20         # the head unit over the network: pairs once, keeps the token
    python hello_car.py --check 5            # run 5 s unattended and exit 0 if everything arrived (the test)

octave_client/ beside this file is OCTAVE's stdlib-only Python client
(sdk/python/octave_client), copied in so this folder is complete on its own.
"""
import argparse
import os
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from octave_client import Client, OctaveError  # noqa: E402

NAME = "hello-car"              # this frontend's name: its demand consumer and its device name
PIDS = ["RPM", "SPEED"]
HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.join(HERE, ".token")


def connect(host, port):
    """A trusted client: on the head unit at once, elsewhere after pairing."""
    token = open(TOKEN_FILE).read().strip() if os.path.exists(TOKEN_FILE) else None
    octave = Client(host, port, token=token)
    if octave.server.get("tokenRejected") and os.path.exists(TOKEN_FILE):
        os.remove(TOKEN_FILE)              # revoked or unknown: pair again
    while not octave.trusted:
        print("This machine is not the head unit. On OCTAVE open Settings > Service > Pair a device.")
        code = input("The 6-digit code: ").strip().replace(" ", "")
        try:
            token = octave.pair(code, NAME, scopes=["media"])   # play / pause only; readings are always allowed
        except OctaveError as e:
            print(f"  {e}")
            continue
        with open(TOKEN_FILE, "w") as fh:
            fh.write(token)
        os.chmod(TOKEN_FILE, 0o600)
        print(f"Paired with {octave.scopes}; the token is in {TOKEN_FILE}")
    return octave


def fetch_cover(octave, host, port, revision):
    """GET /art with a short-lived media ticket (never the token) -> a file path, or None."""
    ticket = octave.media_ticket()["ticket"]
    try:
        with urllib.request.urlopen(f"http://{host}:{port}/art?rev={revision}&ticket={ticket}", timeout=10) as r:
            kind = r.headers.get("Content-Type", "image/jpeg").split("/")[-1]
            path = os.path.join(HERE, f"cover.{kind}")
            with open(path, "wb") as fh:
                fh.write(r.read())
            return path
    except urllib.error.HTTPError:
        return None                         # 404: this track has no cover


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9876)
    parser.add_argument("--check", type=float, metavar="SECONDS", help="unattended: toggle play, run this long, exit 0 if readings and a track arrived")
    args = parser.parse_args()

    octave = connect(args.host, args.port)
    app = octave.server["app"]
    print(f"OCTAVE {app['version']}, protocol {octave.server['protocolVersion']}{', headless' if app['headless'] else ''}")

    # Readings: titles and units from the catalog, values from paramStore (native units).
    # OCTAVE polls the car only for what someone shows: ask for ours, by our own consumer name.
    info = {pid: octave.call("parameterCatalog", "info", pid) for pid in PIDS}
    octave.call("obdManager", "setParameterDemand", NAME, PIDS)
    octave.subscribe("paramStore", properties=PIDS, throttle_ms=200)
    octave.subscribe("nowPlaying", properties=["hasTrack", "title", "artist", "playing", "artRevision"], throttle_ms=250)

    values, track, seen_cover, pressed = {}, {}, [None], [False]
    if args.check is None:
        print("Enter toggles play / pause, Ctrl-C quits.")
        threading.Thread(target=lambda: [octave.call("nowPlaying", "togglePlay") for _ in sys.stdin], daemon=True).start()

    def show():
        if args.check is not None and not sys.stdout.isatty():
            return
        readings = "  ".join(f"{pid} {values[pid]:.{info[pid]['decimals']}f} {info[pid]['unit']}" for pid in PIDS if values.get(pid) is not None)
        state = ("playing" if track.get("playing") else "paused") if track.get("hasTrack") else "nothing playing"
        line = f"{readings:32} | {state}: {track.get('title', '')}" + (f"  cover: {seen_cover[0]}" if seen_cover[0] else "")
        print("\r" + line[:118].ljust(118), end="", flush=True)

    deadline = time.monotonic() + (args.check or float("inf"))
    try:
        while time.monotonic() < deadline:
            for event in octave.events(timeout=0.5):   # events() returns after half a second of quiet
                if event["event"] == "params":
                    values.update(event["values"])
                elif event["event"] == "property" and event["object"] == "nowPlaying":
                    track[event["name"]] = event["value"]
                    if event["name"] == "artRevision" and track.get("hasTrack"):
                        seen_cover[0] = fetch_cover(octave, args.host, args.port, event["value"])
                    if args.check is not None and event["name"] == "playing" and not event["value"] and not pressed[0]:
                        pressed[0] = True
                        octave.call("nowPlaying", "togglePlay")   # press Play once (nothing loaded yet starts the first track)
                else:
                    continue
                show()
                if time.monotonic() > deadline:
                    break
    except KeyboardInterrupt:
        pass
    finally:
        print()
        octave.call("obdManager", "setParameterDemand", NAME, [])   # done showing them (a closed connection does this too)
        octave.close()

    if args.check is not None:
        ok = values.get("RPM", 0) > 0 and "SPEED" in values and track.get("hasTrack") and track.get("playing")
        print("hello-car-python:", f"all checks passed: {values} {track.get('title')}" if ok else f"FAIL values={values} track={track}")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
