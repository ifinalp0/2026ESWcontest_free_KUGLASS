from __future__ import annotations

import argparse
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib import error, request


ROOT = Path(__file__).resolve().parent / "public"
INDEX = ROOT / "index.html"


class TabUIHandler(BaseHTTPRequestHandler):
    api_base = "http://127.0.0.1:8080"

    def do_GET(self) -> None:  # noqa: N802
        if self.path.startswith("/api/"):
            self._proxy()
            return
        self._serve_static()

    def do_HEAD(self) -> None:  # noqa: N802
        if self.path.startswith("/api/"):
            self._send(405, "application/json", b"", head_only=True)
            return
        self._serve_static(head_only=True)

    def do_POST(self) -> None:  # noqa: N802
        if self.path.startswith("/api/"):
            self._proxy()
            return
        self._send(404, "text/plain; charset=utf-8", b"not found")

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def _serve_static(self, head_only: bool = False) -> None:
        route = self.path.split("?", 1)[0]
        if route in {"/", "/demo", "/simple"}:
            path = INDEX
        else:
            relative = route.lstrip("/")
            path = (ROOT / relative).resolve()
            if ROOT not in path.parents and path != ROOT:
                self._send(403, "text/plain; charset=utf-8", b"forbidden", head_only=head_only)
                return
        if not path.exists() or not path.is_file():
            self._send(404, "text/plain; charset=utf-8", b"not found", head_only=head_only)
            return
        content_type = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        body = path.read_bytes()
        self._send(200, content_type, body, head_only=head_only)

    def _proxy(self) -> None:
        body = b""
        if self.command in {"POST", "PUT", "PATCH"}:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length)

        target = self.api_base.rstrip("/") + self.path
        headers = {"Content-Type": self.headers.get("Content-Type", "application/json")}
        req = request.Request(target, data=body if body else None, method=self.command, headers=headers)
        try:
            with request.urlopen(req, timeout=2.0) as response:
                data = response.read()
                content_type = response.headers.get("Content-Type", "application/json")
                self._send(response.status, content_type, data)
        except error.HTTPError as exc:
            self._send(exc.code, exc.headers.get("Content-Type", "application/json"), exc.read())
        except OSError:
            self._send(502, "application/json", b'{"ok":false,"error":"api unavailable"}')

    def _send(self, code: int, content_type: str, body: bytes, head_only: bool = False) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store" if self.path.startswith("/api/") else "public, max-age=60")
        self.end_headers()
        if not head_only:
            self.wfile.write(body)


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve KUGLASS TabUI and proxy RasPi UI API calls.")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5173)
    parser.add_argument("--api", default="http://127.0.0.1:8080")
    args = parser.parse_args()
    TabUIHandler.api_base = args.api
    server = ThreadingHTTPServer((args.host, args.port), TabUIHandler)
    print(f"TabUI listening on http://{args.host}:{args.port}/demo")
    print(f"Proxying /api/* to {args.api}")
    server.serve_forever()


if __name__ == "__main__":
    main()
