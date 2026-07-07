from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from .command_queue import CommandQueue
from .config import load_config
from .state_store import StateStore


DEMO_HTML = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>KUGLASS demo</title>
<style>body{font-family:system-ui;margin:24px;background:#101214;color:#f5f5f5}button{margin:4px;padding:10px 14px}pre{background:#1d2228;padding:16px;overflow:auto}</style></head>
<body><h1>KUGLASS /demo fallback</h1>
<button onclick="cmd({command:'set_demo',demo_mode:'hot_summer'})">Hot summer</button>
<button onclick="cmd({command:'set_mode',mode:'camping'})">Camping</button>
<button onclick="cmd({command:'set_mode',mode:'parked'})">Parked</button>
<button onclick="cmd({command:'set_demo',demo_mode:'flashlight_360'})">360 light</button>
<button onclick="cmd({command:'return_auto'})">Auto</button>
<pre id="state"></pre>
<script>
async function cmd(payload){await fetch('/api/command',{method:'POST',body:JSON.stringify(payload)});}
async function tick(){let r=await fetch('/api/state');document.getElementById('state').textContent=JSON.stringify(await r.json(),null,2);}
setInterval(tick,500);tick();
</script></body></html>"""


class StdlibUI:
    def __init__(self, state_store: StateStore, command_queue: CommandQueue) -> None:
        self.state_store = state_store
        self.command_queue = command_queue

    def handler_class(self) -> type[BaseHTTPRequestHandler]:
        ui = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802
                if self.path in {"/", "/demo", "/simple"}:
                    self._send(200, "text/html; charset=utf-8", DEMO_HTML)
                elif self.path == "/health":
                    self._send_json({"ok": True, "service": "kuglass-ui"})
                elif self.path == "/api/state":
                    self._send_json(ui.state_store.read())
                else:
                    self._send_json({"error": "not found"}, 404)

            def do_POST(self) -> None:  # noqa: N802
                if self.path != "/api/command":
                    self._send_json({"error": "not found"}, 404)
                    return
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length).decode("utf-8")
                try:
                    payload = json.loads(raw)
                    ui.command_queue.push(payload)
                    self._send_json({"ok": True})
                except json.JSONDecodeError:
                    self._send_json({"ok": False, "error": "invalid json"}, 400)

            def log_message(self, _format: str, *_args: Any) -> None:
                return

            def _send_json(self, payload: dict, code: int = 200) -> None:
                self._send(code, "application/json", json.dumps(payload).encode("utf-8"))

            def _send(self, code: int, content_type: str, body: bytes) -> None:
                self.send_response(code)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        return Handler


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="config/config.yaml")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    config = load_config(args.config)
    runtime = config.get("runtime", {})
    ui = StdlibUI(
        StateStore(runtime.get("state_path", "/tmp/kuglass_state.json")),
        CommandQueue(runtime.get("command_queue_path", "/tmp/kuglass_commands.jsonl")),
    )
    server = ThreadingHTTPServer((args.host, args.port), ui.handler_class())
    print(f"kuglass-ui listening on http://{args.host}:{args.port}/demo")
    server.serve_forever()


if __name__ == "__main__":
    main()

