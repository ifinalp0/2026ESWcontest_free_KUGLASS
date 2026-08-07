from __future__ import annotations

import json
import queue
import threading
import time
from typing import Any

from .camera import CameraFrame, CameraFrameStore
from .protocol import CommandError, translate_ui_command
from .state import StateStore
from .transport import AUTO_USB_PORT, MockTransport, Transport, UsbCdcTransport


class ESP32AGateway:
    def __init__(
        self,
        *,
        transport: Transport,
        hil_enabled: bool = False,
        telemetry_timeout_seconds: float = 2.0,
        sequence_seed: int | None = None,
        manual_command_interval_seconds: float = 0.075,
    ) -> None:
        self.transport = transport
        self.hil_enabled = bool(hil_enabled)
        self.telemetry_timeout_seconds = telemetry_timeout_seconds
        self.manual_command_interval_seconds = max(0.05, float(manual_command_interval_seconds))
        self.state = StateStore()
        self.camera = CameraFrameStore()
        self._outbox: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=128)
        seed = int(time.time() * 1000.0) if sequence_seed is None else int(sequence_seed)
        self._seq = seed & 0xFFFFFFFF or 1
        self._seq_lock = threading.Lock()
        self._submit_lock = threading.Lock()
        self._pending_manual: dict[int, dict[str, Any]] = {}
        self._last_manual_write_at: dict[int, float] = {}
        self._running = threading.Event()
        self._thread: threading.Thread | None = None
        self._last_telemetry_at: float | None = None
        self._last_command_seq = 0
        self._gateway_error: str | None = None
        self._camera_stream_requested = False
        self._camera_lock = threading.Lock()
        self._transport_lock = threading.RLock()
        self._drop_commands_until_telemetry = False

    @classmethod
    def create(
        cls,
        *,
        mode: str,
        usb_port: str = AUTO_USB_PORT,
        hil_enabled: bool,
    ) -> "ESP32AGateway":
        if mode == "usb":
            transport: Transport = UsbCdcTransport(usb_port)
        elif mode == "mock":
            transport = MockTransport()
        else:
            raise ValueError("transport mode must be 'usb' or 'mock'")
        return cls(transport=transport, hil_enabled=hil_enabled)

    @property
    def diagnostics_enabled(self) -> bool:
        return self.transport.mode == "mock" or self.hil_enabled

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._running.set()
        self._thread = threading.Thread(target=self._run, name="esp32-a-gateway", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._running.clear()
        if self._thread:
            self._thread.join(timeout=1.0)
        with self._transport_lock:
            self.transport.close()

    def reconnect_controller(self) -> dict[str, Any]:
        """Start a fresh ESP32_A USB session without changing the runtime mode."""

        if self.transport.mode != "usb":
            raise CommandError("ESP32_A reconnect is available only in LIVE USB mode", 409)
        with self._transport_lock:
            self._last_telemetry_at = None
            self._gateway_error = None
            self._drop_commands_until_telemetry = True
            self.state.reset_firmware_handshake()
            self.camera.clear()
            port_open = self.transport.reconnect()
        return {
            "requested": True,
            "portOpen": port_open,
            "link": self.link_snapshot(),
        }

    def submit(self, payload: dict[str, Any]) -> list[int]:
        messages = translate_ui_command(
            payload,
            self._next_seq,
            diagnostics_enabled=self.diagnostics_enabled,
        )
        if self.transport.mode != "mock" and not self._hardware_connected():
            raise CommandError("ESP32_A telemetry is not fresh; command was not queued", 503)
        with self._submit_lock:
            regular_count = sum(message.get("command") != "manual_channel" for message in messages)
            new_manual_channels = {
                int(message["channel_id"])
                for message in messages
                if message.get("command") == "manual_channel" and int(message["channel_id"]) not in self._pending_manual
            }
            if self._outbox.qsize() + regular_count + len(self._pending_manual) + len(new_manual_channels) > self._outbox.maxsize:
                raise CommandError("ESP32_A command queue is full", 503)
            for message in messages:
                if message.get("command") == "manual_channel":
                    self._pending_manual[int(message["channel_id"])] = message
                else:
                    if message.get("command") == "return_auto":
                        channel_id = message.get("channel_id")
                        if channel_id is None:
                            self._pending_manual.clear()
                        else:
                            self._pending_manual.pop(int(channel_id), None)
                    self._outbox.put_nowait(message)
                self._last_command_seq = int(message["seq"])
        if payload.get("type") == "setCameraStream":
            enabled = bool(payload.get("enabled"))
            with self._camera_lock:
                self._camera_stream_requested = enabled
            if enabled:
                self.camera.clear()
        return [int(message["seq"]) for message in messages]

    def snapshot(self) -> dict[str, Any]:
        snapshot = self.state.snapshot()
        snapshot["link"] = self.link_snapshot()
        return snapshot

    def link_snapshot(self) -> dict[str, Any]:
        now = time.monotonic()
        hardware_connected = self._hardware_connected(now)
        downstream = self.state.snapshot()["downstreamDiagnostics"]
        return {
            "transport": self.transport.mode,
            "hardwareConnected": hardware_connected,
            "hilEnabled": self.diagnostics_enabled,
            "port": self.transport.port,
            "lastTelemetryAt": None if self._last_telemetry_at is None else time.time() - (now - self._last_telemetry_at),
            "lastCommandSeq": self._last_command_seq,
            "lastAckSeq": self.state.last_ack_seq,
            "lastAckCommand": self.state.last_ack_command,
            "lastAckOk": self.state.last_ack_ok,
            "lastAckError": self.state.last_ack_error,
            "downstreamHealthy": self.state.downstream_healthy,
            "downstreamError": self.state.downstream_error,
            "downstreamOperationalFault": downstream["operationalFault"],
            "downstreamBootId": downstream["bootId"],
            "downstreamStatusSeq": downstream["statusSeq"],
            "downstreamResetChallenge": downstream["resetChallenge"],
            "downstreamEstop": downstream["estopActive"],
            "downstreamFaultCode": downstream["faultCode"],
            "downstreamDiagnostic": downstream["diagnostic"],
            "downstreamControlResult": downstream["controlResult"],
            "downstreamAdc": downstream["adc"],
            "error": self.state.last_device_error or self._gateway_error or self.transport.error,
        }

    def camera_snapshot(self) -> CameraFrame | None:
        return self.camera.snapshot()

    def camera_status(self) -> dict[str, object]:
        with self._camera_lock:
            requested = self._camera_stream_requested
        bad_frames = int(getattr(self.transport, "bad_camera_frames", 0))
        status = self.camera.status(requested=requested, bad_frames=bad_frames)
        status["hardwareConnected"] = self._hardware_connected()
        status["transport"] = self.transport.mode
        return status

    def _next_seq(self) -> int:
        with self._seq_lock:
            self._seq = (self._seq + 1) & 0xFFFFFFFF
            if self._seq == 0:
                self._seq = 1
            return self._seq

    def _run(self) -> None:
        while self._running.is_set():
            self._flush_outbox()
            self._flush_pending_manual()
            self._read_transport()
            time.sleep(0.02)

    def _flush_outbox(self) -> None:
        while True:
            try:
                message = self._outbox.get_nowait()
            except queue.Empty:
                return
            try:
                self._write_message(message)
            finally:
                self._outbox.task_done()

    def _flush_pending_manual(self) -> None:
        now = time.monotonic()
        ready: list[tuple[int, dict[str, Any]]] = []
        with self._submit_lock:
            for channel_id, message in list(self._pending_manual.items()):
                last_write = self._last_manual_write_at.get(channel_id, float("-inf"))
                if now - last_write >= self.manual_command_interval_seconds:
                    ready.append((channel_id, message))
                    self._pending_manual.pop(channel_id, None)
                    self._last_manual_write_at[channel_id] = now
        for _, message in ready:
            self._write_message(message)

    def _write_message(self, message: dict[str, Any]) -> None:
        with self._transport_lock:
            if self._drop_commands_until_telemetry:
                return
            try:
                line = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
                self.transport.write_line(line)
                self._gateway_error = None
            except (OSError, ValueError) as exc:
                self._gateway_error = str(exc)

    def _read_transport(self) -> None:
        with self._transport_lock:
            for line in self.transport.read_lines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, dict):
                    continue
                if self.state.apply_record(record):
                    if record.get("type") in {"state", "status", "telemetry", "sensor", "sensors", "camera"}:
                        self._last_telemetry_at = time.monotonic()
                        self._drop_commands_until_telemetry = False
                    self._gateway_error = None
            read_camera_frames = getattr(self.transport, "read_camera_frames", None)
            if callable(read_camera_frames):
                for frame in read_camera_frames():
                    self.camera.update(frame)

    def _hardware_connected(self, now: float | None = None) -> bool:
        if not self.transport.connected:
            return False
        if self.transport.mode == "mock":
            return True
        checked_at = time.monotonic() if now is None else now
        return bool(
            self._last_telemetry_at is not None
            and checked_at - self._last_telemetry_at <= self.telemetry_timeout_seconds
        )
