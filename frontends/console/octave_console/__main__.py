"""python -m octave_console [--host H] [--port P] [--tab PANE] [--token T] [--token-file F]"""

import argparse

from .app import PANES, OctaveConsole


def main():
    parser = argparse.ArgumentParser(prog="octave_console", description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9876)
    parser.add_argument("--tab", default="live", choices=[pid for pid, _, _ in PANES])
    parser.add_argument("--token", help="a paired device's token (or OCTAVE_TOKEN); else the token file's, else pair")
    parser.add_argument("--token-file", help="where tokens are kept (default $XDG_CONFIG_HOME/octave-console/tokens.json)")
    args = parser.parse_args()
    OctaveConsole(args.host, args.port, args.tab, token=args.token, token_file=args.token_file).run()


if __name__ == "__main__":
    main()
