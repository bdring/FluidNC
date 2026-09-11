#!/usr/bin/env python3
# Local dev server that sends the COOP/COEP headers required for
# SharedArrayBuffer (and therefore Emscripten pthreads) to work in the
# browser. Plain `python3 -m http.server` will not do this, and pthread
# builds fail at runtime (not link time) without it.
#
# Usage: python3 serve.py [port]   (default port 8765)

import http.server
import sys


class CoiHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    http.server.test(HandlerClass=CoiHandler, port=port, bind="127.0.0.1")
