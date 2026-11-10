#!/usr/bin/env python3
"""Serve a staged WebAssembly site locally, with the headers threads need.

    python3 packaging/wasm/serve.py /tmp/octave-site [PORT]

Sends Cross-Origin-Opener-Policy: same-origin and
Cross-Origin-Embedder-Policy: require-corp on every response (what
coi-serviceworker.js fakes on GitHub Pages) and, as GitHub Pages does,
Access-Control-Allow-Origin: *; on 127.0.0.1 only.
"""

import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # As GitHub Pages: CORS open, no Cross-Origin-Resource-Policy. The
        # playground's sandboxed frontend (an opaque origin) loads its files
        # through CORS, so this is what makes it work there too
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    Handler.extensions_map = {**Handler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript"}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=root))
    print(f"serving {root} at http://127.0.0.1:{port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
