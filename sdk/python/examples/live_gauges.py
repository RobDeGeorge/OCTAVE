#!/usr/bin/env python3
"""Print engine readings as they arrive, with their titles and units.

    python sdk/python/examples/live_gauges.py [--port 9876] [RPM SPEED COOLANT_TEMP ...]

Needs an OCTAVE serving on the port (octave --service, or --headless).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from octave_client import Client  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9876)
    parser.add_argument("ids", nargs="*", default=["RPM", "SPEED", "COOLANT_TEMP"])
    args = parser.parse_args()

    with Client(args.host, args.port) as octave:
        info = {pid: octave.call("parameterCatalog", "info", pid) for pid in args.ids}
        octave.subscribe("paramStore", properties=args.ids, throttle_ms=250)
        try:
            for event in octave.events():
                if event.get("event") != "params":
                    continue
                parts = []
                for pid, value in sorted(event["values"].items()):
                    meta = info[pid]
                    shown = "-" if value is None else f"{value:.{meta.get('decimals', 0)}f}"
                    parts.append(f"{meta['title']}: {shown} {meta['unit']}".rstrip())
                print("   ".join(parts), flush=True)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
