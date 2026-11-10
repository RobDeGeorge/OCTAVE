"""python -m octave_console [--host H] [--port P] [--tab PANE] [--token T] [--token-file F]"""

import argparse
import os

from .app import PANES, OctaveConsole


def main():
    parser = argparse.ArgumentParser(prog="octave_console", description=__doc__)
    # OCTAVE sets OCTAVE_HOST / OCTAVE_PORT / OCTAVE_TOKEN when it runs the console as the screen; the flags win
    parser.add_argument("--host", default=os.environ.get("OCTAVE_HOST") or "127.0.0.1", help="default OCTAVE_HOST, then 127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("OCTAVE_PORT") or 9876), help="default OCTAVE_PORT, then 9876")
    parser.add_argument("--tab", default="live", choices=[pid for pid, _, _ in PANES])
    parser.add_argument("--token", help="a paired device's token (or OCTAVE_TOKEN); else the token file's, else pair")
    parser.add_argument("--token-file", help="where tokens are kept (default $XDG_CONFIG_HOME/octave-console/tokens.json)")
    args = parser.parse_args()
    OctaveConsole(args.host, args.port, args.tab, token=args.token, token_file=args.token_file).run()


if __name__ == "__main__":
    main()
