from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any


DEFAULT_CONFIG: dict[str, Any] = {
    "runtime": {
        "mode": "mock",
        "loop_hz": 20,
        "state_path": "/tmp/kuglass_state.json",
        "command_queue_path": "/tmp/kuglass_commands.jsonl",
        "log_dir": "logs",
        "ttl_ms": 200,
    },
    "camera": {
        "source": "mock",
        "width": 320,
        "height": 180,
        "saturation_threshold": 245,
        "edge_threshold": 22,
    },
    "sensors": {"source": "mock"},
    "weather": {
        "source": "mock",
        "latitude": 37.5665,
        "longitude": 126.9780,
        "refresh_s": 180,
        "stale_s": 600,
    },
    "serial": {"enabled": False, "baudrate": 115200},
    "policy": {
        "front_strong_threshold": 0.46,
        "hot_summer_temperature_c": 31.0,
        "thermal_lux_threshold": 9000.0,
        "driving_ch0_min_transmission": 0.62,
        "driving_ch1_min_transmission": 0.55,
    },
}


def deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def load_config(path: str | Path | None = None) -> dict[str, Any]:
    if path is None:
        return copy.deepcopy(DEFAULT_CONFIG)
    cfg_path = Path(path)
    if not cfg_path.exists():
        return copy.deepcopy(DEFAULT_CONFIG)
    text = cfg_path.read_text(encoding="utf-8")
    if cfg_path.suffix.lower() == ".json":
        loaded = json.loads(text)
    else:
        loaded = _load_yaml_if_available(text)
    return deep_merge(DEFAULT_CONFIG, loaded)


def _load_yaml_if_available(text: str) -> dict[str, Any]:
    try:
        import yaml  # type: ignore
    except Exception:
        return _parse_simple_yaml(text)
    loaded = yaml.safe_load(text) or {}
    if not isinstance(loaded, dict):
        return {}
    return loaded


def _parse_simple_yaml(text: str) -> dict[str, Any]:
    # Small fallback parser for this repository's simple two-level config files.
    root: dict[str, Any] = {}
    section: dict[str, Any] | None = None
    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if not line.startswith(" ") and line.endswith(":"):
            key = line[:-1].strip()
            section = {}
            root[key] = section
            continue
        if section is None or ":" not in line:
            continue
        key, raw_value = line.strip().split(":", 1)
        section[key.strip()] = _coerce_scalar(raw_value.strip())
    return root


def _coerce_scalar(value: str) -> Any:
    if value == "":
        return ""
    lowered = value.lower()
    if lowered in {"true", "false"}:
        return lowered == "true"
    if lowered.startswith("0x"):
        try:
            return int(lowered, 16)
        except ValueError:
            return value
    try:
        if any(ch in value for ch in ".eE"):
            return float(value)
        return int(value)
    except ValueError:
        return value

