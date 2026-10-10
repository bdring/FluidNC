#!/usr/bin/env python3
# Local dev server that sends the COOP/COEP headers required for
# SharedArrayBuffer (and therefore Emscripten pthreads) to work in the
# browser. Plain `python3 -m http.server` will not do this, and pthread
# builds fail at runtime (not link time) without it.
#
# It also implements /.netlify/functions/webui-proxy, the same endpoint as
# ../functions/webui-proxy.js on the Netlify deploy, so the demo's "Install
# WebUI" menu can fetch GitHub release builds locally too.
#
# Usage: python3 serve.py [port]   (default port 8765)

import http.server
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

PROXY_PATH = "/.netlify/functions/webui-proxy"
SAFE_IDENTIFIER = re.compile(r"^[A-Za-z0-9._-]+$")
MAX_COMPRESSED_BYTES = 10 * 1024 * 1024  # same cap as webui-proxy.js


class CoiHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path == PROXY_PATH:
            self.webui_proxy(urllib.parse.parse_qs(url.query))
        else:
            super().do_GET()

    def reply(self, status, body, content_type="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # Fetches github.com/<owner>/<repo>/releases/.../index.html.gz and returns
    # it unchanged.  Like webui-proxy.js, it only ever builds that fixed URL
    # from validated identifiers -- never a caller-supplied URL.
    def webui_proxy(self, params):
        owner = params.get("owner", [""])[0]
        repo = params.get("repo", [""])[0]
        tag = params.get("tag", [""])[0]
        if not owner or not repo or not all(SAFE_IDENTIFIER.match(s) for s in (owner, repo, tag or "latest")):
            self.reply(400, b"Usage: ?owner=<github-owner>&repo=<github-repo>[&tag=<release-tag, default latest>]")
            return
        if tag:
            asset = f"https://github.com/{owner}/{repo}/releases/download/{tag}/index.html.gz"
        else:
            asset = f"https://github.com/{owner}/{repo}/releases/latest/download/index.html.gz"
        try:
            with urllib.request.urlopen(asset, timeout=30) as upstream:
                data = upstream.read(MAX_COMPRESSED_BYTES + 1)
        except urllib.error.HTTPError as err:
            self.reply(502, f"Upstream fetch failed: {err.code} {err.reason}".encode())
            return
        except Exception as err:
            self.reply(502, f"Fetch failed: {err}".encode())
            return
        if len(data) > MAX_COMPRESSED_BYTES:
            self.reply(502, b"Upstream asset exceeds size limit")
            return
        if data[:2] != b"\x1f\x8b":
            self.reply(502, b"Upstream asset is not gzip data")
            return
        self.reply(200, data, "application/gzip")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    http.server.test(HandlerClass=CoiHandler, port=port, bind="127.0.0.1")
