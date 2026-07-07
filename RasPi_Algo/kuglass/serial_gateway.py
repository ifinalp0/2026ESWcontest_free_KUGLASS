from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Protocol

from .channels import CHANNELS
from .models import ChannelTarget, ControllerTelemetry
from .telemetry import parse_telemetry_line


class SerialTransport(Protocol):
    def write_line(self, line: str) -> None:
        ...

    def read_lines(self) -> list[str]:
        ...


@dataclass
class MockSerialTransport:
    controller_id: str
    written: list[str] = field(default_factory=list)
    seq: int = 0

    def write_line(self, line: str) -> None:
        self.written.append(line)
        self.seq += 1

    def read_lines(self) -> list[str]:
        return [
            json.dumps(
                {
                    "type": "status",
                    "controller_id": self.controller_id,
                    "seq": self.seq,
                    "mode": "RUN",
                    "fault": False,
                    "global_en": True,
                    "dc_v": 72.0,
                    "ch": [],
                },
                separators=(",", ":"),
            )
        ]


class PySerialTransport:
    def __init__(self, port: str, baudrate: int) -> None:
        import serial  # type: ignore

        self._serial = serial.Serial(port, baudrate=baudrate, timeout=0.0)

    def write_line(self, line: str) -> None:
        self._serial.write((line.rstrip("\n") + "\n").encode("utf-8"))

    def read_lines(self) -> list[str]:
        lines: list[str] = []
        while self._serial.in_waiting:
            raw = self._serial.readline().decode("utf-8", errors="replace").strip()
            if raw:
                lines.append(raw)
        return lines


class SerialGateway:
    def __init__(self, transports: dict[str, SerialTransport], ttl_ms: int = 200) -> None:
        self.transports = transports
        self.ttl_ms = ttl_ms
        self.channel_to_controller = {spec.channel_id: spec.controller_id for spec in CHANNELS}

    def send_targets(self, seq: int, targets: list[ChannelTarget]) -> None:
        by_controller: dict[str, list[ChannelTarget]] = {"A": [], "B": []}
        for target in targets:
            controller = self.channel_to_controller.get(target.channel_id)
            if controller in by_controller:
                by_controller[controller].append(target)
        for controller, items in by_controller.items():
            transport = self.transports.get(controller)
            if transport is None:
                continue
            transport.write_line(build_command_line(seq, self.ttl_ms, items))

    def read_telemetry(self) -> list[ControllerTelemetry | dict | None]:
        records: list[ControllerTelemetry | dict | None] = []
        for transport in self.transports.values():
            for line in transport.read_lines():
                records.append(parse_telemetry_line(line))
        return records


def build_command_line(seq: int, ttl_ms: int, targets: list[ChannelTarget]) -> str:
    payload = {
        "seq": int(seq),
        "ttl_ms": int(ttl_ms),
        "ch": [[target.channel_id, round(target.target_mi, 4), 1 if target.enable else 0] for target in targets],
    }
    return json.dumps(payload, separators=(",", ":"))


def make_serial_gateway(config: dict) -> SerialGateway:
    serial_cfg = config.get("serial", {})
    enabled = bool(serial_cfg.get("enabled", False))
    baudrate = int(serial_cfg.get("baudrate", 115200))
    if enabled:
        transports: dict[str, SerialTransport] = {
            "A": PySerialTransport(str(serial_cfg["controller_a_port"]), baudrate),
            "B": PySerialTransport(str(serial_cfg["controller_b_port"]), baudrate),
        }
    else:
        transports = {"A": MockSerialTransport("A"), "B": MockSerialTransport("B")}
    return SerialGateway(transports, int(config.get("runtime", {}).get("ttl_ms", 200)))

