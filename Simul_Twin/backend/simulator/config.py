from __future__ import annotations

from copy import deepcopy
from pathlib import Path
from typing import Any


DEFAULT_POLICY_CONFIG: dict[str, Any] = {
    "policy": {
        "clear_mi": 0.95,
        "camping_mi": 0.04,
        "parked_mi": 0.03,
        "front_glare_threshold": 0.62,
        "front_left_saturation_threshold": 0.70,
        "front_right_saturation_threshold": 0.70,
        "front_bias_margin": 0.08,
        "ch0_glare_mi": 0.42,
        "ch1_glare_mi": 0.38,
        "flashlight_min_mi": 0.05,
    },
    "thermal": {
        "trigger": 0.05,
        "reason_threshold": 0.25,
        "temp_base_c": 28.0,
        "temp_span_c": 12.0,
        "lux_base": 550.0,
        "lux_span": 1900.0,
        "top_lux_base": 280.0,
        "top_lux_span": 820.0,
        "demo_boost": 0.35,
        "temp_weight": 0.42,
        "lux_weight": 0.38,
        "top_weight": 0.20,
        "channel_amount": {
            "0": 0.15,
            "1": 0.18,
            "2": 0.36,
            "3": 0.36,
            "4": 0.50,
            "5": 0.50,
            "6": 0.58,
            "7": 0.70,
        },
    },
    "directional": {
        "confidence_gate": 0.12,
        "reason_confidence": 0.20,
        "confidence_scale": 2.4,
        "max_angle_deg": 100.0,
        "angle_scale": 0.9,
        "front_amount": 0.20,
        "front_door_amount": 0.42,
        "rear_amount": 0.50,
    },
    "servo": {
        "snap_epsilon": 0.003,
        "fast_attack_rate": 1.25,
        "frost_rate": 0.45,
        "clear_rate": 0.28,
    },
    "camera": {
        "dimming_gain": 0.52,
        "edge_loss_gain": 0.10,
        "glare_front_lux_base": 650.0,
        "glare_front_lux_span": 2200.0,
    },
    "flashlight": {
        "confidence_floor": 0.35,
        "channel_amount": 0.84,
        "top_lux_base": 250.0,
        "top_lux_span": 850.0,
        "top_amount": 0.78,
        "angular_speed_deg_s": 42.0,
        "base_lux": 110.0,
        "strength_lux": 980.0,
    },
}


def default_config_path() -> Path:
    return Path(__file__).resolve().parents[1] / "config.yaml"


def load_policy_config(path: str | Path | None = None) -> dict[str, Any]:
    config = deepcopy(DEFAULT_POLICY_CONFIG)
    config_path = Path(path) if path is not None else default_config_path()
    if not config_path.exists():
        return config
    loaded = parse_simple_yaml(config_path.read_text(encoding="utf-8"))
    deep_merge(config, loaded)
    return config


def deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            deep_merge(base[key], value)
        else:
            base[key] = value
    return base


def parse_simple_yaml(text: str) -> dict[str, Any]:
    root: dict[str, Any] = {}
    stack: list[tuple[int, dict[str, Any]]] = [(-1, root)]

    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        stripped = line.strip()
        if ":" not in stripped:
            raise ValueError(f"Invalid config line: {raw_line!r}")
        key_part, value_part = stripped.split(":", 1)
        key = _parse_key(key_part.strip())
        value_text = value_part.strip()

        while stack and indent <= stack[-1][0]:
            stack.pop()
        parent = stack[-1][1]
        if value_text == "":
            child: dict[str, Any] = {}
            parent[key] = child
            stack.append((indent, child))
        else:
            parent[key] = _parse_scalar(value_text)

    return root


def _parse_key(value: str) -> str:
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
        return value[1:-1]
    return value


def _parse_scalar(value: str) -> Any:
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
        return value[1:-1]
    lowered = value.lower()
    if lowered in {"null", "none", "~"}:
        return None
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    try:
        if any(char in value for char in (".", "e", "E")):
            return float(value)
        return int(value)
    except ValueError:
        return value


def cfg_float(config: dict[str, Any], section: str, key: str) -> float:
    return float(config[section][key])


def cfg_channel_float(config: dict[str, Any], section: str, key: str, channel: int) -> float:
    return float(config[section][key][str(channel)])
