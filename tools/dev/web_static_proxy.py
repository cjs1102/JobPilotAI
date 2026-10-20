#!/usr/bin/env python3
"""0022 fallback web runner: static files + a small same-origin API proxy.

This exists so the Web UI can be exercised on a host that cannot pull the
nginx image. It is a development/CI fallback, not the production host: the
compose `web` service uses `docker/nginx/default.conf`.

    python3 tools/dev/web_static_proxy.py --root web --port 8081 \
        --backend http://127.0.0.1:8080
"""

import argparse
import os
import sys
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


class WebProxyHandler(SimpleHTTPRequestHandler):
    backend = "http://127.0.0.1:8080"

    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def list_directory(self, path):
        self.send_error(404, "Not found")
        return None

    def _is_proxy_path(self):
        path = urlsplit(self.path).path
        return (
            path.startswith("/api/")
            or path == "/health"
        )

    def _proxy(self):
        parsed = urlsplit(self.path)
        target = self.backend.rstrip("/") + self.path

        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length) if length > 0 else None

        request = urllib.request.Request(
            target,
            data=body,
            method=self.command,
        )
        for name in (
            "Accept",
            "Authorization",
            "Content-Type",
            "X-Internal-Token",
        ):
            value = self.headers.get(name)
            if value:
                request.add_header(name, value)

        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                status = response.status
                headers = response.headers
                payload = response.read()
        except urllib.error.HTTPError as error:
            status = error.code
            headers = error.headers
            payload = error.read()
        except OSError as error:
            self.send_error(502, "backend unavailable: %s" % error)
            return

        self.send_response(status)
        content_type = headers.get("Content-Type")
        if content_type:
            self.send_header("Content-Type", content_type)
        content_length = headers.get("Content-Length")
        self.send_header("Content-Length", str(len(payload)) if content_length is None else content_length)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def _handle(self):
        if self._is_proxy_path():
            self._proxy()
            return
        if self.command == "GET":
            super().do_GET()
            return
        if self.command == "HEAD":
            super().do_HEAD()
            return
        self.send_error(405, "Method not allowed")

    def do_GET(self):
        self._handle()

    def do_HEAD(self):
        self._handle()

    def do_POST(self):
        self._handle()

    def do_PUT(self):
        self._handle()

    def do_DELETE(self):
        self._handle()

    def log_message(self, format_string, *args):
        sys.stderr.write("web-proxy %s - %s\n" % (self.address_string(), format_string % args))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Serve web/ and proxy /api and /health")
    parser.add_argument("--root", default="web")
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8081)
    parser.add_argument("--backend", default="http://127.0.0.1:8080")
    args = parser.parse_args(argv)

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("not a directory: %s" % root, file=sys.stderr)
        return 2

    WebProxyHandler.backend = args.backend
    handler = lambda *handler_args, **handler_kwargs: WebProxyHandler(
        *handler_args,
        directory=root,
        **handler_kwargs,
    )
    server = ThreadingHTTPServer((args.listen, args.port), handler)
    print(
        "web_static_proxy root=%s listen=http://%s:%d backend=%s"
        % (root, args.listen, args.port, args.backend),
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
