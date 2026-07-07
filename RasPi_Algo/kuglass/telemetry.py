from __future__ import annotations

import json
from typing import Any

from .models import ChannelTelemetry, ControllerTelemetry


def parse_telemetry_line(line: str) -> ControllerTelemetry | dict[str, Any] | None:
    try:
        payload = json.loads(line)
    except json.JSONDecodeError:
        return None
    if payload.get("type") == "status":
        channels = [
            ChannelTelemetry(
                int(ch.get("id", 0)),
                float(ch.get("mi", 0.0)),
                float(ch.get("target_mi", ch.get("mi", 0.0))),
                bool(ch.get("enable", True)),
                float(ch.get("vrms", 0.0)),
                float(ch.get("irms", 0.0)),
                float(ch.get("temp_c", 0.0)),
                bool(ch.get("fault", False)),
                str(ch.get("fault_code", "")),
            )
            for ch in payload.get("ch", [])
        ]
        return ControllerTelemetry(
            str(payload.get("controller_id", "?")),
            int(payload.get("seq", 0)),
            bool(payload.get("fault", False)),
            str(payload.get("mode", "")),
            float(payload.get("dc_v", 0.0)),
            channels,
        )
    return payload

