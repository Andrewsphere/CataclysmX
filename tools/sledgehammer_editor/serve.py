#!/usr/bin/env python3
"""Launch Sledgehammer Editor for Catax worldgen."""

import argparse
import contextlib
import http.server
import json
import mimetypes
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import urllib.parse
import webbrowser


STATIC = {"index.html", "style.css", "app.js", "source.js", "forms.js", "map.js", "model.js", "layout.js", "catalog.js"}
MAX_REQUEST = 16 * 1024 * 1024
HERE = Path(__file__).resolve().parent


@contextlib.contextmanager
def checkout_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as lock:
        try:
            if os.name == "nt":
                import msvcrt
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError("Another editor is already open for this checkout.") from error
        yield lock


class EditorServer(http.server.HTTPServer):
    def worker(self, request):
        with tempfile.TemporaryDirectory(prefix="catax-worldgen-") as candidate:
            result = subprocess.run(
                [str(self.binary), str(self.checkout), candidate, self.tileset],
                input=request, capture_output=True, check=True,
                **self.child_options,
            )
        return result.stdout


class Handler(http.server.BaseHTTPRequestHandler):
    def respond(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; img-src 'self'; style-src 'self'; "
            "object-src 'none'; frame-ancestors 'none'; base-uri 'none'",
        )
        self.end_headers()
        self.wfile.write(body)

    def error(self, status, reason):
        self.respond(status, json.dumps({"error": reason}).encode(), "application/json")

    def trusted_host(self):
        return self.headers.get("Host") == self.server.host

    def do_GET(self):
        if not self.trusted_host():
            self.error(403, "Use the editor's local address.")
            return
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).lstrip("/")
        if not path:
            path = "index.html"
        if path in STATIC:
            file = HERE / path
        elif path.startswith("image/"):
            relative = path.removeprefix("image/")
            if (not relative or any(part in {"", ".", ".."} for part in relative.split("/"))
                    or "\\" in relative or "\0" in relative):
                self.error(404, "Unknown image.")
                return
            file = (self.server.image_root / relative).resolve()
            if file.suffix != ".png" or not file.is_relative_to(self.server.image_root):
                self.error(404, "Unknown image.")
                return
        else:
            self.error(404, "Unknown editor resource.")
            return
        try:
            body = file.read_bytes()
        except (OSError, ValueError):
            self.error(404, "Could not read the resource.")
            return
        content_type = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
        self.respond(200, body, content_type)

    def do_POST(self):
        if (self.path != "/api" or not self.trusted_host()
                or self.headers.get("Origin") != self.server.origin
                or not secrets.compare_digest(self.headers.get("X-Editor-Token", ""), self.server.token)):
            self.error(403, "Open the editor using the URL printed by the launcher.")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if not 0 < length <= MAX_REQUEST:
            self.error(413, "Editor request is empty or too large.")
            return
        try:
            response = self.server.worker(self.rfile.read(length))
        except (OSError, subprocess.CalledProcessError) as error:
            self.log_error("Editor worker failed: %s", error)
            self.error(500, "The editor worker failed. See the launcher output.")
            return
        self.respond(200, response, "application/json")

    def log_request(self, code="-", size="-"):
        if str(code).startswith("5"):
            super().log_request(code, size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True, help="path to the built sledgehammer_editor worker")
    parser.add_argument("--checkout", type=Path, default=HERE.parent.parent)
    parser.add_argument("--tileset", default="assets/tiles/gen1_32x32", help="tileset directory relative to the checkout")
    parser.add_argument("--port", type=int, default=0, help="loopback port, or 0 to select a free port")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    worker = args.worker.resolve()
    tileset = (checkout / args.tileset).resolve()
    if not worker.is_file() or not (checkout / "data/content.json5").is_file():
        parser.error("Select an existing worker executable and Catax checkout.")
    if not (checkout / "assets/tiles").resolve() in tileset.parents or not (tileset / "tileset.json5").is_file():
        parser.error("Select a tileset under the checkout's assets/tiles directory.")
    recovery = checkout / "out/worldgen-editor"
    if recovery.resolve() != recovery:
        parser.error("The editor recovery directory must be inside the checkout without symlinks.")
    try:
        with checkout_lock(recovery / "editor.lock") as lock, EditorServer(("127.0.0.1", args.port), Handler) as server:
            server.checkout = checkout
            server.binary = worker
            server.tileset = tileset.relative_to(checkout).as_posix()
            server.image_root = tileset
            server.child_options = {} if os.name == "nt" else {"pass_fds": (lock.fileno(),)}
            server.host = f"127.0.0.1:{server.server_port}"
            server.origin = f"http://{server.host}"
            server.token = secrets.token_urlsafe(24)
            result = json.loads(server.worker(b'{"operation":"recover"}'))
            if not result.get("ok"):
                raise RuntimeError(result.get("error"))
            url = f"{server.origin}/#{server.token}"
            print(url, flush=True)
            if not args.no_browser:
                webbrowser.open(url)
            server.serve_forever()
    except KeyboardInterrupt:
        pass
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
