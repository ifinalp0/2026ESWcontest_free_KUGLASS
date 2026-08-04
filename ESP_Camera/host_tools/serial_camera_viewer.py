#!/usr/bin/env python3
"""Display framed ESP32 camera images from USB-to-UART in a local browser."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    import serial
except ImportError as exc:
    raise SystemExit(
        "pyserial is required. Run this script with the ESP-IDF Python "
        "environment or install pyserial."
    ) from exc


MAGIC = b"KUGLCAM1"
HEADER = struct.Struct("<8sIHHB3xII")
FORMAT_JPEG = 2
MAX_PAYLOAD = 2 * 1024 * 1024


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


@dataclass(frozen=True)
class Frame:
    sequence: int
    width: int
    height: int
    payload: bytes
    received_at: float


class FrameState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._frame: Frame | None = None
        self._good_frames = 0
        self._bad_frames = 0
        self._connected = False
        self._last_error = ""
        self._started_at = time.monotonic()

    def set_connected(self, connected: bool, error: str = "") -> None:
        with self._lock:
            self._connected = connected
            self._last_error = error

    def count_bad_frame(self) -> None:
        with self._lock:
            self._bad_frames += 1

    def update(
        self,
        sequence: int,
        width: int,
        height: int,
        payload: bytes,
    ) -> None:
        now = time.monotonic()
        with self._lock:
            self._frame = Frame(sequence, width, height, payload, now)
            self._good_frames += 1
            self._connected = True
            self._last_error = ""

    def snapshot(self) -> Frame | None:
        with self._lock:
            return self._frame

    def status(self) -> dict[str, object]:
        now = time.monotonic()
        with self._lock:
            frame = self._frame
            elapsed = max(now - self._started_at, 0.001)
            return {
                "serial_connected": self._connected,
                "frame_ready": frame is not None,
                "sequence": frame.sequence if frame else None,
                "width": frame.width if frame else None,
                "height": frame.height if frame else None,
                "format": "jpeg" if frame else None,
                "payload_bytes": len(frame.payload) if frame else None,
                "frame_age_seconds": round(now - frame.received_at, 3)
                if frame
                else None,
                "good_frames": self._good_frames,
                "bad_frames": self._bad_frames,
                "average_fps": round(self._good_frames / elapsed, 2),
                "last_error": self._last_error,
            }


class SerialCameraReader(threading.Thread):
    def __init__(self, port: str, baud: int, state: FrameState) -> None:
        super().__init__(name="serial-camera-reader", daemon=True)
        self._port = port
        self._baud = baud
        self._state = state
        self._stop_event = threading.Event()

    def stop(self) -> None:
        self._stop_event.set()

    def run(self) -> None:
        while not self._stop_event.is_set():
            try:
                with serial.Serial(
                    self._port,
                    self._baud,
                    timeout=0.25,
                ) as device:
                    self._state.set_connected(True)
                    print(
                        f"Serial connected: {self._port} @ {self._baud}",
                        flush=True,
                    )
                    self._read_frames(device)
            except (OSError, serial.SerialException) as exc:
                message = str(exc)
                self._state.set_connected(False, message)
                print(f"Serial reconnecting after error: {message}", flush=True)
                self._stop_event.wait(1.0)

    def _read_frames(self, device: serial.Serial) -> None:
        buffer = bytearray()
        while not self._stop_event.is_set():
            chunk = device.read(65536)
            if not chunk:
                continue
            buffer.extend(chunk)

            while True:
                marker = buffer.find(MAGIC)
                if marker < 0:
                    if len(buffer) > len(MAGIC) - 1:
                        del buffer[: -(len(MAGIC) - 1)]
                    break
                if marker:
                    del buffer[:marker]
                if len(buffer) < HEADER.size:
                    break

                (
                    _magic,
                    sequence,
                    width,
                    height,
                    pixel_format,
                    payload_size,
                    expected_hash,
                ) = HEADER.unpack_from(buffer)

                valid_header = (
                    1 <= width <= 1024
                    and 1 <= height <= 1024
                    and pixel_format == FORMAT_JPEG
                    and 4 <= payload_size <= MAX_PAYLOAD
                )
                if not valid_header:
                    del buffer[0]
                    self._state.count_bad_frame()
                    continue

                frame_size = HEADER.size + payload_size
                if len(buffer) < frame_size:
                    break

                payload = bytes(buffer[HEADER.size:frame_size])
                valid_jpeg = (
                    payload.startswith(b"\xff\xd8")
                    and payload.endswith(b"\xff\xd9")
                )
                if fnv1a(payload) != expected_hash or not valid_jpeg:
                    del buffer[0]
                    self._state.count_bad_frame()
                    continue

                del buffer[:frame_size]
                self._state.update(sequence, width, height, payload)


INDEX_HTML = """<!doctype html>
<html lang="ko">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>KuGlass 유선 OV2640 카메라</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 0; background: #11151b; color: #eef2f7; }
    main { width: min(920px, calc(100% - 32px)); margin: 28px auto; }
    h1 { font-size: 1.35rem; margin: 0 0 6px; }
    p { color: #aeb8c5; margin: 0 0 18px; }
    .panel { background: #1b222c; border: 1px solid #313c49;
             border-radius: 12px; padding: 16px; }
    canvas { display: block; width: min(100%, 800px); aspect-ratio: 4 / 3;
             background: #050607; image-rendering: pixelated;
             border-radius: 8px; }
    .bar { display: flex; gap: 16px; flex-wrap: wrap; align-items: center;
           margin-top: 14px; color: #c6d0dc; }
    .ok { color: #65d58a; } .waiting { color: #f4c76a; }
  </style>
</head>
<body>
  <main>
    <h1>KuGlass 유선 OV2640 카메라</h1>
    <p>320×240 소프트웨어 JPEG · ESP32-S3 → USB-to-UART → 이 컴퓨터</p>
    <section class="panel">
      <canvas id="view" width="320" height="240"></canvas>
      <div class="bar">
        <span id="status" class="waiting">프레임 대기 중…</span>
        <span id="stats"></span>
      </div>
    </section>
  </main>
  <script>
    const canvas = document.getElementById("view");
    const context = canvas.getContext("2d", {alpha: false});
    const statusNode = document.getElementById("status");
    const statsNode = document.getElementById("stats");
    let lastSequence = -1;

    const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

    async function renderNext() {
      try {
        const response = await fetch(`/frame?after=${lastSequence}`, {
          cache: "no-store"
        });
        if (response.status === 204) {
          await pause(35);
          return;
        }
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const width = Number(response.headers.get("X-Frame-Width"));
        const height = Number(response.headers.get("X-Frame-Height"));
        const sequence = Number(response.headers.get("X-Frame-Sequence"));

        if (canvas.width !== width || canvas.height !== height) {
          canvas.width = width;
          canvas.height = height;
        }

        const bitmap = await createImageBitmap(await response.blob());
        context.drawImage(bitmap, 0, 0, width, height);
        bitmap.close();
        lastSequence = sequence;
        statusNode.textContent = "실시간 수신 중";
        statusNode.className = "ok";
      } catch (error) {
        statusNode.textContent = `연결 대기: ${error.message}`;
        statusNode.className = "waiting";
        await pause(250);
      }
    }

    async function refreshStatus() {
      try {
        const response = await fetch("/status", {cache: "no-store"});
        const value = await response.json();
        statsNode.textContent =
          `${value.width || "-"}×${value.height || "-"} · ` +
          `${value.average_fps} fps · ` +
          `${Math.round((value.payload_bytes || 0) / 1024)} KiB · ` +
          `정상 ${value.good_frames} · ` +
          `오류 ${value.bad_frames}`;
      } catch (_) {}
    }

    async function loop() {
      while (true) await renderNext();
    }
    loop();
    refreshStatus();
    setInterval(refreshStatus, 1000);
  </script>
</body>
</html>
"""


def make_handler(state: FrameState) -> type[BaseHTTPRequestHandler]:
    class CameraHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/":
                self._send_bytes(
                    HTTPStatus.OK,
                    INDEX_HTML.encode("utf-8"),
                    "text/html; charset=utf-8",
                )
                return
            if parsed.path == "/status":
                payload = json.dumps(state.status()).encode("utf-8")
                self._send_bytes(
                    HTTPStatus.OK, payload, "application/json; charset=utf-8"
                )
                return
            if parsed.path == "/frame":
                frame = state.snapshot()
                after_values = parse_qs(parsed.query).get("after", ["-1"])
                try:
                    after = int(after_values[0])
                except ValueError:
                    after = -1
                if frame is None or frame.sequence == after:
                    self.send_response(HTTPStatus.NO_CONTENT)
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    return
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", str(len(frame.payload)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Frame-Width", str(frame.width))
                self.send_header("X-Frame-Height", str(frame.height))
                self.send_header("X-Frame-Sequence", str(frame.sequence))
                self.end_headers()
                self.wfile.write(frame.payload)
                return
            if parsed.path == "/snapshot.jpg":
                frame = state.snapshot()
                if frame is None:
                    self._send_bytes(
                        HTTPStatus.SERVICE_UNAVAILABLE,
                        b"No JPEG camera frame is available yet.\n",
                        "text/plain; charset=utf-8",
                    )
                    return
                self._send_bytes(
                    HTTPStatus.OK, frame.payload, "image/jpeg"
                )
                return
            if parsed.path == "/favicon.ico":
                self.send_response(HTTPStatus.NO_CONTENT)
                self.end_headers()
                return
            self._send_bytes(
                HTTPStatus.NOT_FOUND,
                b"Not found\n",
                "text/plain; charset=utf-8",
            )

        def _send_bytes(
            self, status: HTTPStatus, payload: bytes, content_type: str
        ) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, _format: str, *args: object) -> None:
            del args

    return CameraHandler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serve ESP32-S3 serial camera frames to a local browser."
    )
    parser.add_argument(
        "--port",
        required=True,
        help="USB-to-UART serial device (for example /dev/ttyUSB0)",
    )
    parser.add_argument("--baud", type=int, default=2000000)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8765)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = FrameState()
    reader = SerialCameraReader(args.port, args.baud, state)
    reader.start()

    server = ThreadingHTTPServer(
        (args.listen, args.http_port), make_handler(state)
    )
    url = f"http://{args.listen}:{args.http_port}/"
    print(f"Camera viewer: {url}", flush=True)
    print("Press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
        reader.stop()
        reader.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
