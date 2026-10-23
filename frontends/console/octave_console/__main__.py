"""python -m octave_console [--host H] [--port P] [--tab live|obd|trips|settings|downloads|api]"""

import argparse

from .app import PANES, OctaveConsole


def main():
    parser = argparse.ArgumentParser(prog="octave_console", description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9876)
    parser.add_argument("--tab", default="live", choices=[pid for pid, _, _ in PANES])
    args = parser.parse_args()
    OctaveConsole(args.host, args.port, args.tab).run()


if __name__ == "__main__":
    main()
