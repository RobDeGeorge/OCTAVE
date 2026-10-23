#!/usr/bin/env python3
"""Show what is playing and control it.

    python sdk/python/examples/now_playing.py               # print the track, then follow changes
    python sdk/python/examples/now_playing.py next          # or: play, previous, volume 40, mute

Needs an OCTAVE serving on the port (octave --service, or --headless).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from octave_client import Client  # noqa: E402

FIELDS = ["hasTrack", "title", "artist", "album", "playing", "source"]


def describe(state):
    if not state.get("hasTrack"):
        return "Nothing playing"
    who = " - ".join(x for x in (state.get("artist"), state.get("album")) if x)
    mark = "playing" if state.get("playing") else "paused"
    return f"[{mark}, {state.get('source')}] {state.get('title')}" + (f"  ({who})" if who else "")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9876)
    parser.add_argument("command", nargs="*", help="play | next | previous | volume N | mute")
    args = parser.parse_args()

    with Client(args.host, args.port) as octave:
        cmd = args.command
        if cmd:
            if cmd[0] == "play":
                octave.call("nowPlaying", "togglePlay")
            elif cmd[0] == "next":
                octave.call("nowPlaying", "next")
            elif cmd[0] == "previous":
                octave.call("nowPlaying", "previous")
            elif cmd[0] == "volume" and len(cmd) == 2:
                octave.call("volumeController", "applyVolume", int(cmd[1]))
                print("volume", octave.get("volumeController", "percent"))
            elif cmd[0] == "mute":
                octave.call("volumeController", "toggleMute")
                print("muted" if octave.get("volumeController", "muted") else "unmuted")
            else:
                parser.error(f"unknown command: {' '.join(cmd)}")
            return

        state = {}
        octave.subscribe("nowPlaying", properties=FIELDS)
        try:
            for event in octave.events():
                if event.get("event") == "property":
                    state[event["name"]] = event["value"]
                    if len(state) == len(FIELDS):   # the initial values have all arrived
                        print(describe(state), flush=True)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
