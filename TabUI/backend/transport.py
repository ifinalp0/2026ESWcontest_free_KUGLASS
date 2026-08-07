from __future__ import annotations

import copy
import json
import struct
import threading
import time
from collections import deque
from typing import Any, Iterable, Protocol

from .camera import (
    CAMERA_FORMAT_JPEG,
    CAMERA_MAGIC,
    CAMERA_MAX_PAYLOAD,
    CameraFrame,
    fnv1a,
)
from .state import clamp, default_state, estimated_transmittance, optical_state


class Transport(Protocol):
    mode: str
    port: str | None
    connected: bool
    error: str | None

    def write_line(self, line: str) -> None: ...

    def read_lines(self) -> list[str]: ...

    def reconnect(self) -> bool: ...

    def close(self) -> None: ...


AUTO_USB_PORT = "auto"
USB_CDC_LINE_CODING_BAUD = 115200
CAMERA_HEADER = struct.Struct("<8sIHHB3xII")


def discover_esp32_usb_ports(port_infos: Iterable[Any] | None = None) -> list[str]:
    """Return macOS USB CDC device nodes suitable for the ESP32_A DevKit USB port."""

    if port_infos is None:
        from serial.tools import list_ports  # type: ignore

        port_infos = list_ports.comports()

    mac_usbmodem: list[str] = []
    described_esp32: list[str] = []
    for info in port_infos:
        device = str(getattr(info, "device", ""))
        if not device:
            continue
        if device.startswith("/dev/cu.usbmodem"):
            mac_usbmodem.append(device)
            continue
        description = " ".join(
            str(getattr(info, field, "") or "")
            for field in ("description", "product", "manufacturer", "interface")
        ).lower()
        if device.startswith("/dev/cu.") and (
            "esp32" in description or "usb jtag" in description
        ):
            described_esp32.append(device)

    # macOS exposes both dial-in and call-out nodes for some CDC devices. TabUI
    # deliberately uses only /dev/cu.* so one physical USB device is not counted twice.
    return sorted(set(mac_usbmodem or described_esp32))


class UsbCdcTransport:
    """MacBook-to-ESP32_A link through the DevKit's physical USB connector."""

    mode = "usb"

    def __init__(
        self,
        port: str = AUTO_USB_PORT,
        reconnect_seconds: float = 1.0,
        max_line_bytes: int = 16384,
    ) -> None:
        self.requested_port = port
        self.port: str | None = None if port == AUTO_USB_PORT else port
        self.reconnect_seconds = reconnect_seconds
        self.max_line_bytes = max(256, int(max_line_bytes))
        self.connected = False
        self.error: str | None = None
        self._serial: Any = None
        self._last_open_attempt = 0.0
        self._lock = threading.RLock()
        self._rx_buffer = bytearray()
        self._discarding_oversize_line = False
        self._camera_frames: deque[CameraFrame] = deque(maxlen=2)
        self._bad_camera_frames = 0

    def write_line(self, line: str) -> None:
        with self._lock:
            if not self._ensure_open():
                raise OSError(self.error or f"ESP32_A USB unavailable: {self.port or AUTO_USB_PORT}")
            try:
                self._serial.write((line.rstrip("\n") + "\n").encode("utf-8"))
            except Exception as exc:  # pyserial raises several platform-specific errors
                self._disconnect(str(exc))
                raise OSError(str(exc)) from exc

    def read_lines(self) -> list[str]:
        with self._lock:
            if not self._ensure_open():
                return []
            try:
                waiting = int(self._serial.in_waiting)
                if waiting > 0:
                    chunk = self._serial.read(waiting)
                    if chunk:
                        self._rx_buffer.extend(chunk)
            except Exception as exc:
                self._disconnect(str(exc))
                return []
            return self._extract_complete_lines()

    def close(self) -> None:
        with self._lock:
            if self._serial is not None:
                try:
                    self._serial.close()
                except Exception:
                    pass
            self._serial = None
            self.connected = False
            self._rx_buffer.clear()
            self._discarding_oversize_line = False
            self._camera_frames.clear()

    def reconnect(self) -> bool:
        """Close the current CDC handle and immediately rediscover/reopen ESP32_A."""

        with self._lock:
            self._disconnect("ESP32_A reconnect requested")
            self._last_open_attempt = float("-inf")
            return self._ensure_open()

    def read_camera_frames(self) -> list[CameraFrame]:
        with self._lock:
            frames = list(self._camera_frames)
            self._camera_frames.clear()
            return frames

    @property
    def bad_camera_frames(self) -> int:
        with self._lock:
            return self._bad_camera_frames

    def _ensure_open(self) -> bool:
        if self._serial is not None and getattr(self._serial, "is_open", False):
            self.connected = True
            return True
        now = time.monotonic()
        if now - self._last_open_attempt < self.reconnect_seconds:
            return False
        self._last_open_attempt = now
        try:
            import serial  # type: ignore

            port = self._resolve_port()
            self._serial = serial.Serial(
                port,
                # USB Serial/JTAG presents a CDC/ACM device. pyserial requires
                # line coding, but this is not an external GPIO UART link.
                baudrate=USB_CDC_LINE_CODING_BAUD,
                timeout=0.0,
                write_timeout=0.25,
            )
            self.port = port
            self.connected = True
            self.error = None
            return True
        except Exception as exc:
            self._disconnect(str(exc))
            return False

    def _resolve_port(self) -> str:
        if self.requested_port != AUTO_USB_PORT:
            return self.requested_port
        candidates = discover_esp32_usb_ports()
        if not candidates:
            raise OSError(
                "ESP32_A USB device not found; connect the DevKit USB port with the micro-USB cable"
            )
        if len(candidates) > 1:
            joined = ", ".join(candidates)
            raise OSError(
                f"multiple ESP32 USB devices found ({joined}); select one with --usb-port"
            )
        return candidates[0]

    def _disconnect(self, error: str) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
        self._serial = None
        if self.requested_port == AUTO_USB_PORT:
            self.port = None
        self.connected = False
        self.error = error
        self._rx_buffer.clear()
        self._discarding_oversize_line = False
        self._camera_frames.clear()

    def _extract_complete_lines(self) -> list[str]:
        lines: list[str] = []
        while True:
            marker = self._rx_buffer.find(CAMERA_MAGIC)
            newline = self._rx_buffer.find(b"\n")

            if marker == 0:
                if len(self._rx_buffer) < CAMERA_HEADER.size:
                    return lines
                (
                    _magic,
                    sequence,
                    width,
                    height,
                    pixel_format,
                    payload_size,
                    expected_hash,
                ) = CAMERA_HEADER.unpack_from(self._rx_buffer)
                valid_header = (
                    1 <= width <= 1024
                    and 1 <= height <= 1024
                    and pixel_format == CAMERA_FORMAT_JPEG
                    and 4 <= payload_size <= CAMERA_MAX_PAYLOAD
                )
                if not valid_header:
                    del self._rx_buffer[0]
                    self._bad_camera_frames += 1
                    continue
                frame_size = CAMERA_HEADER.size + payload_size
                if len(self._rx_buffer) < frame_size:
                    return lines
                payload = bytes(self._rx_buffer[CAMERA_HEADER.size:frame_size])
                valid_jpeg = payload.startswith(b"\xff\xd8") and payload.endswith(b"\xff\xd9")
                if not valid_jpeg or fnv1a(payload) != expected_hash:
                    del self._rx_buffer[0]
                    self._bad_camera_frames += 1
                    continue
                del self._rx_buffer[:frame_size]
                self._camera_frames.append(CameraFrame(
                    sequence=sequence,
                    width=width,
                    height=height,
                    payload=payload,
                    received_at=time.monotonic(),
                ))
                self._discarding_oversize_line = False
                continue

            if marker > 0 and (newline < 0 or marker < newline):
                # ROM/log noise can appear around resets. A complete camera
                # marker is an unambiguous resynchronization point.
                del self._rx_buffer[:marker]
                self._discarding_oversize_line = False
                continue

            if newline < 0:
                if len(self._rx_buffer) > self.max_line_bytes:
                    self._rx_buffer.clear()
                    self._discarding_oversize_line = True
                return lines
            raw = bytes(self._rx_buffer[:newline])
            del self._rx_buffer[: newline + 1]
            if self._discarding_oversize_line:
                self._discarding_oversize_line = False
                continue
            if len(raw) > self.max_line_bytes:
                continue
            decoded = raw.rstrip(b"\r").decode("utf-8", errors="replace").strip()
            if decoded:
                lines.append(decoded)


class MockTransport:
    """In-process ESP32_A stand-in used only for UI development and HIL rehearsal."""

    mode = "mock"
    port = None
    connected = True
    error = None

    SCENARIO_TARGETS = {
        "none": [0.95] * 4,
        "hot_summer": [0.82, 0.80, 0.52, 0.52],
        "camping": [0.04] * 4,
        "parked": [0.03] * 4,
        "camera_saturation": [0.42, 0.76, 0.86, 0.86],
    }

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._outgoing: deque[str] = deque()
        self._state = default_state()
        self._state["downstream"] = {"controller_id": "B", "healthy": True, "error": "OK"}
        self._last_state_at = 0.0
        self._status_seq = 0
        self._boot_id = 1001
        self._reset_challenge = 2001
        self._source_session_id = 3001
        self._pending_control_result: dict[str, Any] | None = None
        self._manual_targets: dict[int, tuple[float, bool, float]] = {}
        self._auto_targets = [0.95] * 4
        self._state["environment"].update({
            "internalTemp": 27.0,
            "frontLeftSaturation": 0.08,
            "frontRightSaturation": 0.07,
            "edgeDensity": 0.86,
        })
        self._state["controllerDiagnostics"].update({
            "protocolVersion": 1,
            "role": "algorithm_master",
            "sourceSessionId": self._source_session_id,
            "downstreamReady": True,
            "firmwareDiagnosticsEnabled": True,
            "stateSeq": 0,
            "thermalRisk": 0.0,
        })
        for channel in self._state["channels"]:
            self._set_channel(channel, 0.95, 0.95, enabled=True)
        self._state["decisionReason"] = "개발용 MOCK ESP32_A가 기본 자동 정책을 보고합니다."

    def write_line(self, line: str) -> None:
        record = json.loads(line)
        with self._lock:
            self._apply_command(record)
            ack = {
                "type": "ack",
                "seq": record.get("seq"),
                "command": record.get("command"),
                "ok": True,
            }
            if record.get("command") == "reset_fault":
                self._outgoing.append(self._status_line())
                self._outgoing.append(json.dumps(ack, separators=(",", ":")))
            else:
                self._outgoing.append(json.dumps(ack, separators=(",", ":")))
            self._outgoing.append(self._state_line())
            if record.get("command") != "reset_fault":
                self._outgoing.append(self._status_line())

    def read_lines(self) -> list[str]:
        with self._lock:
            self._step()
            now = time.monotonic()
            if now - self._last_state_at >= 0.1:
                self._last_state_at = now
                self._outgoing.append(self._state_line())
                self._outgoing.append(self._status_line())
            lines = list(self._outgoing)
            self._outgoing.clear()
            return lines

    def reconnect(self) -> bool:
        return True

    def close(self) -> None:
        return

    def read_camera_frames(self) -> list[CameraFrame]:
        return []

    @property
    def bad_camera_frames(self) -> int:
        return 0

    def _apply_command(self, record: dict[str, Any]) -> None:
        command = record.get("command")
        if command == "set_mode":
            mode = str(record.get("mode", "driving"))
            if mode in {"driving", "stopped", "camping", "parked"}:
                self._state["vehicleMode"] = mode
        elif command == "set_demo":
            demo_mode = str(record.get("demo_mode", "none"))
            if demo_mode in self.SCENARIO_TARGETS:
                self._set_scenario(demo_mode)
        elif command == "manual_channel":
            channel_id = int(record["channel_id"])
            mi = clamp(record["target_mi"])
            enable = bool(record.get("enable", True))
            expires = time.time() + max(0.0, float(record.get("ttl_ms", 30000))) / 1000.0
            self._manual_targets[channel_id] = (mi, enable, expires)
            channel = self._state["channels"][channel_id]
            channel["targetMi"] = mi if enable else 0.0
            channel["commandedEnable"] = enable
            channel["commandedEnableKnown"] = True
            channel["manualUntil"] = expires
            action = f"MI {mi:.2f}" if enable else "ENABLE OFF"
            self._state["decisionReason"] = f"MOCK ESP32_A: CH{channel_id} 수동 {action}, TTL 적용 중."
        elif command == "return_auto":
            channel_id = record.get("channel_id")
            if channel_id is None:
                self._manual_targets.clear()
                ids = range(4)
            else:
                channel_id = int(channel_id)
                self._manual_targets.pop(channel_id, None)
                ids = (channel_id,)
            for item in ids:
                channel = self._state["channels"][item]
                channel["manualUntil"] = None
                channel["targetMi"] = self._auto_targets[item]
                channel["commandedEnable"] = True
                channel["commandedEnableKnown"] = True
            self._state["decisionReason"] = "MOCK ESP32_A: 자동 정책으로 복귀했습니다."
        elif command == "reset_fault":
            for channel in self._state["channels"]:
                channel["fault"] = False
            self._state["decisionReason"] = "MOCK ESP32_A: fault latch 초기화를 요청했습니다."
            self._pending_control_result = {
                "command": "reset_fault",
                "seq": int(record["seq"]),
                "source_session_id": self._source_session_id,
                "ok": True,
                "error": "NONE",
            }
            self._reset_challenge = (self._reset_challenge + 1) & 0xFFFFFFFF or 1
        elif command == "set_channel_fault":
            channel_id = int(record["channel_id"])
            self._state["channels"][channel_id]["fault"] = bool(record.get("fault", True))
            self._state["decisionReason"] = f"MOCK/HIL: CH{channel_id} fault 상태를 변경했습니다."
        elif command == "set_environment":
            aliases = {
                "internal_temp_c": "internalTemp", "front_left_saturation": "frontLeftSaturation",
                "front_right_saturation": "frontRightSaturation", "edge_density": "edgeDensity",
            }
            for key, value in record.get("environment", {}).items():
                if key in aliases:
                    self._state["environment"][aliases[key]] = value
            self._state["decisionReason"] = "MOCK/HIL 환경 override를 ESP32_A 입력으로 적용했습니다."
        elif command == "camera_stream":
            # MOCK has no physical image source. It still acknowledges the
            # command so the viewer can exercise its waiting/error UI.
            return

    def _set_scenario(self, demo_mode: str) -> None:
        self._state["demoMode"] = demo_mode
        self._auto_targets = list(self.SCENARIO_TARGETS[demo_mode])
        self._manual_targets.clear()
        for index, target in enumerate(self._auto_targets):
            self._state["channels"][index]["targetMi"] = target
            self._state["channels"][index]["commandedEnable"] = True
            self._state["channels"][index]["commandedEnableKnown"] = True
            self._state["channels"][index]["manualUntil"] = None
        environments = {
            "none": (27, 0.08, 0.07, 0.86),
            "hot_summer": (39, 0.18, 0.16, 0.84),
            "camping": (24, 0.02, 0.02, 0.72),
            "parked": (33, 0.10, 0.09, 0.76),
            "camera_saturation": (28, 0.90, 0.36, 0.83),
        }
        values = environments[demo_mode]
        keys = ("internalTemp", "frontLeftSaturation", "frontRightSaturation", "edgeDensity")
        self._state["environment"].update(dict(zip(keys, values)))
        reasons = {
            "none": "MOCK ESP32_A: 기본 주행 자동 정책.",
            "hot_summer": "MOCK ESP32_A: 내부온도에 따라 4채널 열부하 제어를 적용합니다.",
            "camping": "MOCK ESP32_A: 차박 프라이버시를 위해 전 채널을 강산란으로 전환합니다.",
            "parked": "MOCK ESP32_A: 주차 도난방지와 열부하 저감을 위해 전 채널을 보호합니다.",
            "camera_saturation": "MOCK ESP32_A: 전면 ROI 강광을 감지해 CH0/CH1 fast-attack을 적용합니다.",
        }
        self._state["decisionReason"] = reasons[demo_mode]
        self._update_camera()

    def _step(self) -> None:
        now = time.time()
        for channel_id, (_, _, expires) in list(self._manual_targets.items()):
            if expires <= now:
                self._manual_targets.pop(channel_id, None)
                channel = self._state["channels"][channel_id]
                channel["manualUntil"] = None
                channel["targetMi"] = self._auto_targets[channel_id]
                channel["commandedEnable"] = True
                channel["commandedEnableKnown"] = True
        for channel in self._state["channels"]:
            enabled = bool(channel["commandedEnable"])
            target = 0.0 if channel["fault"] or not enabled else float(channel["targetMi"])
            applied = float(channel["appliedMi"])
            rate = 0.12 if target < applied else 0.04
            delta = max(-rate, min(rate, target - applied))
            self._set_channel(channel, float(channel["targetMi"]), applied + delta, enabled=enabled)
        self._update_camera()
        self._state["timestamp"] = now

    def _update_camera(self) -> None:
        environment = self._state["environment"]
        front_mi = (self._state["channels"][0]["appliedMi"] + self._state["channels"][1]["appliedMi"]) / 2
        dimming = clamp((0.95 - front_mi) / 0.75)
        left = clamp((environment["frontLeftSaturation"] or 0) * (1 - 0.52 * dimming))
        right = clamp((environment["frontRightSaturation"] or 0) * (1 - 0.52 * dimming))
        edge = clamp((environment["edgeDensity"] or 0) - 0.1 * dimming)
        self._state["cameraMetrics"].update({
            "frontLeftSaturation": round(left, 3),
            "frontRightSaturation": round(right, 3),
            "edgeDensity": round(edge, 3),
            "glare": round(max(left, right), 3),
            "frameId": self._state["cameraMetrics"]["frameId"] + 1,
            "timestamp": time.time(),
        })

    @staticmethod
    def _set_channel(
        channel: dict[str, Any],
        target: float,
        applied: float,
        *,
        enabled: bool,
    ) -> None:
        target, applied = clamp(target), clamp(applied)
        channel.update({
            "targetMi": round(target, 4),
            "commandedMi": round(target, 4),
            "commandedEnable": enabled,
            "commandedEnableKnown": True,
            "appliedMi": round(applied, 4),
            "estimatedTransmittance": estimated_transmittance(applied),
            "opticalState": optical_state(applied),
        })

    def _state_line(self) -> str:
        diagnostics = self._state["controllerDiagnostics"]
        diagnostics["stateSeq"] = int(diagnostics["stateSeq"] or 0) + 1
        return json.dumps({"type": "state", "state": copy.deepcopy(self._state)}, ensure_ascii=False, separators=(",", ":"))

    def _status_line(self) -> str:
        self._status_seq = (self._status_seq + 1) & 0xFFFFFFFF
        channels = [
            {
                "id": channel["channel"],
                "mi": channel["appliedMi"],
                "target_mi": channel["commandedMi"],
                "fault": channel["fault"],
            }
            for channel in self._state["channels"]
        ]
        status: dict[str, Any] = {
            "v": 1,
            "type": "status",
            "controller_id": "B",
            "seq": self._status_seq,
            "boot_id": self._boot_id,
            "reset_challenge": self._reset_challenge,
            "estop": False,
            "fault_code": "NONE",
            "diagnostic": "MOCK",
            "ch": channels,
            "adc": {
                "initialized": True,
                "i_cali": True,
                "t_cali": True,
                "raw_valid_mask": 255,
                "mv_valid_mask": 255,
                "i_raw": [118, 121, 119, 120],
                "t_raw": [2008, 2012, 2004, 2010],
                "i_mv": [28, 29, 28, 29],
                "t_mv": [1618, 1621, 1615, 1619],
            },
        }
        if self._pending_control_result is not None:
            status["control_result"] = self._pending_control_result
            self._pending_control_result = None
        return json.dumps(
            status,
            ensure_ascii=False,
            separators=(",", ":"),
        )
