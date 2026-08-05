from __future__ import annotations

import argparse
import json
import mimetypes
import os
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlsplit

from backend.gateway import ESP32AGateway
from backend.protocol import CommandError


ROOT = Path(__file__).resolve().parent
DIST_ROOT = ROOT / "dist"
MAX_REQUEST_BYTES = 64 * 1024


class ReplayStore:
    def __init__(self, data_dir: Path) -> None:
        self.replay_dir = data_dir / "replays"

    def save(self, events: list[dict[str, Any]], requested_name: Any = None) -> Path:
        raw_name = str(requested_name or time.strftime("tabui_%Y%m%d_%H%M%S"))
        safe_name = re.sub(r"[^a-zA-Z0-9_.-]+", "_", raw_name).strip("._")[:80]
        if not safe_name:
            safe_name = time.strftime("tabui_%Y%m%d_%H%M%S")
        self.replay_dir.mkdir(parents=True, exist_ok=True)
        path = self.replay_dir / f"{safe_name}.jsonl"
        with path.open("w", encoding="utf-8") as output:
            for event in events:
                output.write(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n")
        return path


class TabUIServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        server_address: tuple[str, int],
        gateway: ESP32AGateway,
        *,
        dist_root: Path = DIST_ROOT,
        data_dir: Path = ROOT / "data",
    ) -> None:
        self.gateway = gateway
        self.dist_root = dist_root.resolve()
        self.replays = ReplayStore(data_dir)
        super().__init__(server_address, TabUIHandler)


class TabUIHandler(BaseHTTPRequestHandler):
    server: TabUIServer

    def do_GET(self) -> None:  # noqa: N802
        route = urlsplit(self.path).path
        if route == "/health":
            link = self.server.gateway.link_snapshot()
            self._send_json({
                "ok": True,
                "service": "kuglass-tabui",
                "controller": "ESP32_A",
                "link": link,
            })
            return
        if route == "/api/state":
            self._send_json(self.server.gateway.snapshot())
            return
        self._serve_static(route)

    def do_HEAD(self) -> None:  # noqa: N802
        route = urlsplit(self.path).path
        if route in {"/health", "/api/state"}:
            self._send_json({}, head_only=True)
            return
        self._serve_static(route, head_only=True)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.send_header("Allow", "GET,HEAD,POST,OPTIONS")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self) -> None:  # noqa: N802
        route = urlsplit(self.path).path
        if route != "/api/command":
            self._send_json({"ok": False, "error": "not found"}, 404)
            return
        try:
            payload = self._read_json()
            command_type = payload.get("type")
            if command_type == "saveReplay":
                path = self.server.replays.save(self.server.gateway.state.replay(), payload.get("name"))
                self._send_json({"ok": True, "path": str(path), "state": self.server.gateway.snapshot()})
                return
            if command_type == "loadReplay":
                raise CommandError("replay load cannot drive LIVE hardware", 409)
            sequences = self.server.gateway.submit(payload)
            self._send_json({
                "ok": True,
                "accepted": True,
                "sequences": sequences,
                "state": self.server.gateway.snapshot(),
            }, 202)
        except CommandError as exc:
            self._send_json({"ok": False, "error": str(exc)}, exc.status)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            self._send_json({"ok": False, "error": f"invalid JSON: {exc}"}, 400)
        except ValueError as exc:
            self._send_json({"ok": False, "error": str(exc)}, 400)

    def log_message(self, format_string: str, *args: object) -> None:
        if os.environ.get("TABUI_ACCESS_LOG", "0") == "1":
            super().log_message(format_string, *args)

    def _read_json(self) -> dict[str, Any]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("invalid Content-Length") from exc
        if length <= 0:
            raise ValueError("request body is required")
        if length > MAX_REQUEST_BYTES:
            raise CommandError("request body too large", 413)
        payload = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("JSON body must be an object")
        return payload

    def _serve_static(self, route: str, head_only: bool = False) -> None:
        if not self.server.dist_root.exists():
            self._send_json(
                {
                    "ok": False,
                    "error": "TabUI frontend is not built",
                    "hint": "Run `npm run build`, or use Vite on port 5173 during development.",
                },
                503,
                head_only=head_only,
            )
            return

        decoded_route = unquote(route)
        if decoded_route in {"/", "/demo", "/simple"}:
            path = self.server.dist_root / "index.html"
        else:
            relative = decoded_route.lstrip("/")
            path = (self.server.dist_root / relative).resolve()
            if self.server.dist_root not in path.parents:
                self._send_json({"ok": False, "error": "forbidden"}, 403, head_only=head_only)
                return
            if not path.is_file() and "." not in Path(relative).name:
                path = self.server.dist_root / "index.html"
        if not path.exists() or not path.is_file():
            self._send_json({"ok": False, "error": "not found"}, 404, head_only=head_only)
            return
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        body = path.read_bytes()
        cache = "no-cache" if path.name in {"index.html", "sw.js"} else "public, max-age=31536000, immutable"
        self._send(200, content_type, body, cache_control=cache, head_only=head_only)

    def _send_json(self, payload: dict[str, Any], code: int = 200, head_only: bool = False) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send(code, "application/json; charset=utf-8", body, cache_control="no-store", head_only=head_only)

    def _send(
        self,
        code: int,
        content_type: str,
        body: bytes,
        *,
        cache_control: str,
        head_only: bool = False,
    ) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache_control)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "same-origin")
        self.end_headers()
        if not head_only:
            self.wfile.write(body)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve KUGLASS TabUI and gateway commands to ESP32_A.")
    parser.add_argument("--host", default=os.environ.get("TABUI_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("TABUI_PORT", "8080")))
    parser.add_argument("--transport", choices=("usb", "mock"), default=os.environ.get("TABUI_TRANSPORT", "usb"))
    parser.add_argument("--usb-port", default=os.environ.get("TABUI_USB_PORT", "auto"))
    parser.add_argument("--hil", action="store_true", default=os.environ.get("TABUI_HIL_ENABLED", "0") == "1")
    parser.add_argument("--data-dir", type=Path, default=Path(os.environ.get("TABUI_DATA_DIR", str(ROOT / "data"))))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    gateway = ESP32AGateway.create(
        mode=args.transport,
        usb_port=args.usb_port,
        hil_enabled=args.hil,
    )
    gateway.start()
    server = TabUIServer((args.host, args.port), gateway, data_dir=args.data_dir)
    print(f"KUGLASS TabUI listening on http://{args.host}:{args.port}/demo")
    print(f"ESP32_A transport={args.transport} port={args.usb_port if args.transport == 'usb' else '-'} HIL={gateway.diagnostics_enabled}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        gateway.close()


if __name__ == "__main__":
    main()
