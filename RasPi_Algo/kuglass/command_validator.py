from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .models import DemoMode, ManualOverride, VehicleMode
from .utils import clamp, now_ms


@dataclass(frozen=True)
class UICommand:
    command: str
    vehicle_mode: VehicleMode | None = None
    demo_mode: DemoMode | None = None
    manual: ManualOverride | None = None


def validate_ui_command(payload: dict[str, Any], default_ttl_ms: int = 15000) -> UICommand:
    command = str(payload.get("command", "")).strip()
    if command == "set_mode":
        return UICommand(command, vehicle_mode=VehicleMode(str(payload["mode"])))
    if command == "set_demo":
        return UICommand(command, demo_mode=DemoMode(str(payload["demo_mode"])))
    if command == "manual_channel":
        channel_id = int(payload["channel_id"])
        if channel_id < 0 or channel_id > 7:
            raise ValueError("channel_id must be 0..7")
        ttl_ms = int(payload.get("ttl_ms", default_ttl_ms))
        return UICommand(
            command,
            manual=ManualOverride(
                channel_id,
                clamp(float(payload["target_mi"]), 0.0, 1.0),
                now_ms() + max(100, ttl_ms),
                bool(payload.get("enable", True)),
            ),
        )
    if command == "return_auto":
        return UICommand(command)
    raise ValueError(f"unsupported command: {command}")

