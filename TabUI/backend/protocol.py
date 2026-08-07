from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable


SCENARIOS = {
    "none": "driving",
    "hot_summer": "driving",
    "camping": "camping",
    "parked": "parked",
    "camera_saturation": "driving",
}

ENVIRONMENT_KEYS = {
    "internalTemp": "internal_temp_c",
    "frontLeftSaturation": "front_left_saturation",
    "frontRightSaturation": "front_right_saturation",
    "edgeDensity": "edge_density",
}


@dataclass(frozen=True)
class CommandError(ValueError):
    message: str
    status: int = 400

    def __str__(self) -> str:
        return self.message


def _number(value: Any, name: str) -> float:
    if isinstance(value, bool):
        raise CommandError(f"{name} must be a number")
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise CommandError(f"{name} must be a number") from exc
    if number != number or number in {float("inf"), float("-inf")}:
        raise CommandError(f"{name} must be finite")
    return number


def _channel(value: Any) -> int:
    number = _number(value, "channel")
    channel = int(number)
    if number != channel or channel < 0 or channel > 3:
        raise CommandError("channel must be an integer from 0 to 3")
    return channel


def translate_ui_command(
    payload: dict[str, Any],
    next_seq: Callable[[], int],
    *,
    diagnostics_enabled: bool,
) -> list[dict[str, Any]]:
    """Translate the TabUI command API to ESP32_A's snake_case JSONL protocol.

    ESP32_A owns policy and MI calculation. This function deliberately sends no
    CH0-CH3 target array; only explicit manual-channel commands contain target_mi.
    """

    command_type = payload.get("type")
    if not isinstance(command_type, str):
        raise CommandError("type is required")

    commands: list[dict[str, Any]]
    if command_type == "setScenario":
        demo_mode = str(payload.get("demoMode", "none"))
        if demo_mode not in SCENARIOS:
            raise CommandError(f"unsupported demoMode: {demo_mode}")
        commands = [
            {"command": "set_mode", "mode": SCENARIOS[demo_mode]},
            {"command": "set_demo", "demo_mode": demo_mode},
        ]
    elif command_type == "setManualChannel":
        mi = _number(payload.get("mi"), "mi")
        if not 0.0 <= mi <= 1.0:
            raise CommandError("mi must be between 0 and 1")
        enable = payload.get("enable", True)
        if not isinstance(enable, bool):
            raise CommandError("enable must be a boolean")
        ttl_seconds = _number(payload.get("ttlSeconds", 30), "ttlSeconds")
        if not 1.0 <= ttl_seconds <= 300.0:
            raise CommandError("ttlSeconds must be between 1 and 300")
        commands = [{
            "command": "manual_channel",
            "channel_id": _channel(payload.get("channel")),
            "target_mi": round(mi, 4),
            "ttl_ms": round(ttl_seconds * 1000),
            "enable": enable,
        }]
    elif command_type == "returnAuto":
        command: dict[str, Any] = {"command": "return_auto"}
        if payload.get("channel") is not None:
            command["channel_id"] = _channel(payload["channel"])
        commands = [command]
    elif command_type == "resetFault":
        commands = [{"command": "reset_fault"}]
    elif command_type == "setEnvironment":
        _require_diagnostics(command_type, diagnostics_enabled)
        incoming = payload.get("environment")
        if not isinstance(incoming, dict) or not incoming:
            raise CommandError("environment must be a non-empty object")
        environment: dict[str, float | None] = {}
        for key, value in incoming.items():
            wire_key = ENVIRONMENT_KEYS.get(key)
            if wire_key is None:
                raise CommandError(f"unsupported environment field: {key}")
            environment[wire_key] = None if value is None else _number(value, key)
        commands = [{"command": "set_environment", "environment": environment}]
    elif command_type == "setChannelFault":
        _require_diagnostics(command_type, diagnostics_enabled)
        commands = [{
            "command": "set_channel_fault",
            "channel_id": _channel(payload.get("channel")),
            "fault": bool(payload.get("fault", True)),
        }]
    elif command_type == "setCameraStream":
        enabled = payload.get("enabled")
        if not isinstance(enabled, bool):
            raise CommandError("enabled must be a boolean")
        commands = [{
            "command": "camera_stream",
            "enable": enabled,
            "ttl_ms": 15000,
        }]
    elif command_type in {"saveReplay", "loadReplay"}:
        raise CommandError(f"{command_type} is handled by the TabUI backend", 500)
    else:
        raise CommandError(f"unsupported command type: {command_type}")

    return [{"v": 1, "type": "ui_command", "seq": next_seq(), **command} for command in commands]


def _require_diagnostics(command_type: str, diagnostics_enabled: bool) -> None:
    if not diagnostics_enabled:
        raise CommandError(
            f"{command_type} is MOCK/HIL-only; LIVE sensor and fault state cannot be overwritten",
            409,
        )
