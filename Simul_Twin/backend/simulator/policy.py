from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

from .config import cfg_float
from .models import CHANNEL_CONFIGS, ChannelState, DemoMode, EnvironmentInput, VehicleMode


def clamp(value: float, lower: float = 0.0, upper: float = 1.0) -> float:
    return max(lower, min(upper, value))


def safe_lux(value: float | None) -> float:
    return max(0.0, float(value)) if value is not None else 0.0


def estimated_transmittance(mi: float) -> float:
    mi = clamp(mi)
    return round(0.12 + 0.83 * (mi**1.18), 3)


def optical_state(mi: float) -> str:
    if mi >= 0.72:
        return "CLEAR"
    if mi >= 0.30:
        return "DIM"
    return "FROST"


def angular_distance(a: float, b: float) -> float:
    diff = abs((a - b + 180.0) % 360.0 - 180.0)
    return diff


def angular_hit(channel_bearing: float, source_bearing: float, config: dict[str, Any]) -> float:
    distance = angular_distance(channel_bearing, source_bearing)
    if distance >= cfg_float(config, "directional", "max_angle_deg"):
        return 0.0
    return clamp(math.cos(math.radians(distance * cfg_float(config, "directional", "angle_scale"))))


def light_vector(env: EnvironmentInput) -> tuple[float, float]:
    x = safe_lux(env.rightLux) - safe_lux(env.leftLux)
    y = safe_lux(env.frontLux) - safe_lux(env.rearLux)
    return x, y


def light_bearing_and_confidence(env: EnvironmentInput, config: dict[str, Any]) -> tuple[float, float]:
    x, y = light_vector(env)
    magnitude = math.hypot(x, y)
    total = sum(
        safe_lux(value)
        for value in (env.frontLux, env.rightLux, env.rearLux, env.leftLux)
    )
    if total <= 1.0 or magnitude <= 1.0:
        return 0.0, 0.0
    bearing = (math.degrees(math.atan2(x, y)) + 360.0) % 360.0
    return bearing, clamp(magnitude / max(total, 1.0) * cfg_float(config, "directional", "confidence_scale"))


def thermal_risk(env: EnvironmentInput, demo_mode: DemoMode, config: dict[str, Any]) -> float:
    lux_total = sum(
        safe_lux(value)
        for value in (env.frontLux, env.rightLux, env.rearLux, env.leftLux, env.topLux)
    )
    temp_component = clamp(
        (max(env.internalTemp, env.weatherTemp) - cfg_float(config, "thermal", "temp_base_c"))
        / cfg_float(config, "thermal", "temp_span_c")
    )
    lux_component = clamp(
        (lux_total - cfg_float(config, "thermal", "lux_base")) / cfg_float(config, "thermal", "lux_span")
    )
    top_component = clamp(
        (safe_lux(env.topLux) - cfg_float(config, "thermal", "top_lux_base"))
        / cfg_float(config, "thermal", "top_lux_span")
    )
    boost = cfg_float(config, "thermal", "demo_boost") if demo_mode == "hot_summer" else 0.0
    return clamp(
        cfg_float(config, "thermal", "temp_weight") * temp_component
        + cfg_float(config, "thermal", "lux_weight") * lux_component
        + cfg_float(config, "thermal", "top_weight") * top_component
        + boost
    )


def front_glare(env: EnvironmentInput, demo_mode: DemoMode, config: dict[str, Any]) -> tuple[float, float, float]:
    left = clamp(env.frontLeftSaturation)
    right = clamp(env.frontRightSaturation)
    front_lux = clamp((safe_lux(env.frontLux) - 520.0) / 1050.0)
    glare = clamp(max(left, right) * 0.75 + front_lux * 0.25)
    if demo_mode == "camera_saturation":
        glare = max(glare, 0.88)
    return left, right, glare


@dataclass(frozen=True)
class PolicyResult:
    targets: dict[int, float]
    reason: str
    fast_attack_channels: set[int]


class PolicyEngine:
    """Rule-based twin of the project policy described in Smart_glass_V20_0.md."""

    def __init__(self, config: dict[str, Any]) -> None:
        self.config = config

    def compute(
        self,
        *,
        vehicle_mode: VehicleMode,
        demo_mode: DemoMode,
        environment: EnvironmentInput,
        channels: list[ChannelState],
        now: float,
    ) -> PolicyResult:
        del channels, now
        targets = {config.channel: cfg_float(self.config, "policy", "clear_mi") for config in CHANNEL_CONFIGS}
        fast_attack_channels: set[int] = set()

        if demo_mode == "camping" or vehicle_mode == "camping":
            return PolicyResult(
                targets={config.channel: cfg_float(self.config, "policy", "camping_mi") for config in CHANNEL_CONFIGS},
                reason="Camping privacy: all PDLC channels move to maximum Frost.",
                fast_attack_channels=set(),
            )

        if demo_mode == "parked" or vehicle_mode == "parked":
            return PolicyResult(
                targets={config.channel: cfg_float(self.config, "policy", "parked_mi") for config in CHANNEL_CONFIGS},
                reason="Parked mode: all glass is Frost for theft deterrence and parked heat load.",
                fast_attack_channels=set(),
            )

        if demo_mode == "flashlight_360":
            return self._flashlight_360(environment)

        risk = thermal_risk(environment, demo_mode, self.config)
        if risk > cfg_float(self.config, "thermal", "trigger"):
            thermal_amount = self.config["thermal"]["channel_amount"]
            for channel, amount in thermal_amount.items():
                channel_id = int(channel)
                targets[channel_id] = min(
                    targets[channel_id],
                    cfg_float(self.config, "policy", "clear_mi") - float(amount) * risk,
                )

        bearing, confidence = light_bearing_and_confidence(environment, self.config)
        if confidence > cfg_float(self.config, "directional", "confidence_gate"):
            for config in CHANNEL_CONFIGS:
                if config.bearing_deg is None:
                    continue
                hit = angular_hit(config.bearing_deg, bearing, self.config) * confidence
                if config.channel in (0, 1):
                    amount = cfg_float(self.config, "directional", "front_amount")
                elif config.channel in (2, 3):
                    amount = cfg_float(self.config, "directional", "front_door_amount")
                else:
                    amount = cfg_float(self.config, "directional", "rear_amount")
                targets[config.channel] = min(
                    targets[config.channel],
                    cfg_float(self.config, "policy", "clear_mi") - amount * hit,
                )

        left_sat, right_sat, glare = front_glare(environment, demo_mode, self.config)
        strong_front_light = (
            glare >= cfg_float(self.config, "policy", "front_glare_threshold")
            or left_sat >= cfg_float(self.config, "policy", "front_left_saturation_threshold")
            or right_sat >= cfg_float(self.config, "policy", "front_right_saturation_threshold")
        )
        if strong_front_light:
            front_margin = cfg_float(self.config, "policy", "front_bias_margin")
            if left_sat >= right_sat - front_margin:
                targets[0] = min(targets[0], cfg_float(self.config, "policy", "ch0_glare_mi"))
                fast_attack_channels.add(0)
            if right_sat >= left_sat - front_margin:
                targets[1] = min(targets[1], cfg_float(self.config, "policy", "ch1_glare_mi"))
                fast_attack_channels.add(1)

        if vehicle_mode == "driving":
            for config in CHANNEL_CONFIGS:
                targets[config.channel] = max(targets[config.channel], config.visibility_floor)

        reason_parts: list[str] = []
        if risk > cfg_float(self.config, "thermal", "reason_threshold"):
            reason_parts.append("thermal priority CH7->CH6->CH4/5->CH2/3->CH0/1")
        if confidence > cfg_float(self.config, "directional", "reason_confidence"):
            reason_parts.append(f"directional lux bearing {bearing:.0f} deg")
        if strong_front_light:
            reason_parts.append("front glare fast-attack on CH0/CH1")
        if not reason_parts:
            reason_parts.append("driving baseline keeps windows mostly Clear")

        return PolicyResult(
            targets={channel: round(clamp(mi), 3) for channel, mi in targets.items()},
            reason="; ".join(reason_parts) + ".",
            fast_attack_channels=fast_attack_channels,
        )

    def _flashlight_360(self, environment: EnvironmentInput) -> PolicyResult:
        bearing, confidence = light_bearing_and_confidence(environment, self.config)
        confidence = max(confidence, cfg_float(self.config, "flashlight", "confidence_floor"))
        targets = {config.channel: cfg_float(self.config, "policy", "clear_mi") for config in CHANNEL_CONFIGS}
        for config in CHANNEL_CONFIGS:
            if config.bearing_deg is None:
                top_intensity = clamp(
                    (safe_lux(environment.topLux) - cfg_float(self.config, "flashlight", "top_lux_base"))
                    / cfg_float(self.config, "flashlight", "top_lux_span")
                )
                targets[config.channel] = (
                    cfg_float(self.config, "policy", "clear_mi")
                    - cfg_float(self.config, "flashlight", "top_amount") * top_intensity
                )
                continue
            hit = angular_hit(config.bearing_deg, bearing, self.config)
            targets[config.channel] = (
                cfg_float(self.config, "policy", "clear_mi")
                - cfg_float(self.config, "flashlight", "channel_amount") * hit * confidence
            )
        return PolicyResult(
            targets={
                channel: round(
                    clamp(
                        mi,
                        cfg_float(self.config, "policy", "flashlight_min_mi"),
                        cfg_float(self.config, "policy", "clear_mi"),
                    ),
                    3,
                )
                for channel, mi in targets.items()
            },
            reason=f"360 flashlight mock: strongest direction is {bearing:.0f} deg; nearest glass frosts first.",
            fast_attack_channels={0, 1},
        )
