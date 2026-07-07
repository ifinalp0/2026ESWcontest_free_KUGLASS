from __future__ import annotations

from .channel_score import score_channel
from .channels import CHANNELS
from .external_context import weather_to_thermal_factor
from .lut_mapper import LUTMapper
from .models import ChannelTarget, ControlInputs, DemoMode, OpticalState, PolicyDecision, VehicleMode
from .utils import clamp


class PolicyEngine:
    def __init__(self, lut: LUTMapper | None = None) -> None:
        self.lut = lut or LUTMapper()
        self.seq = 0

    def decide(self, inputs: ControlInputs) -> PolicyDecision:
        self.seq += 1
        privacy_need = self._privacy_need(inputs)
        thermal_risk = self._thermal_risk(inputs)
        targets: list[ChannelTarget] = []

        for spec in CHANNELS:
            if privacy_need >= 0.99:
                target_transmission = 0.03
                score = 1.0
                reason = f"{inputs.vehicle_mode.value}: privacy/off state"
            else:
                breakdown = score_channel(spec, inputs, thermal_risk, privacy_need)
                score = breakdown.value
                target_transmission = self._score_to_transmission(spec.channel_id, score, inputs)
                reason = self._reason(spec.channel_id, breakdown, inputs)

            target_mi = self.lut.transmission_to_mi(target_transmission)
            state = self._state_for_transmission(target_transmission)
            targets.append(
                ChannelTarget(spec.channel_id, target_transmission, target_mi, True, state, score, reason)
            )

        return PolicyDecision(
            self.seq,
            targets,
            thermal_risk,
            privacy_need,
            inputs.glare.strong,
            inputs.vehicle_mode,
            inputs.demo_mode,
        )

    def _privacy_need(self, inputs: ControlInputs) -> float:
        if inputs.vehicle_mode in {VehicleMode.CAMPING, VehicleMode.PARKED}:
            return 1.0
        return 0.0

    def _thermal_risk(self, inputs: ControlInputs) -> float:
        weather_factor = weather_to_thermal_factor(inputs.weather)
        lux_factor = clamp(inputs.lux.total_lux / 42000.0, 0.0, 1.0)
        internal_factor = clamp((inputs.internal_temp_c - 27.0) / 12.0, 0.0, 1.0)
        if inputs.demo_mode == DemoMode.HOT_SUMMER:
            return clamp(max(0.55, 0.42 * weather_factor + 0.34 * lux_factor + 0.24 * internal_factor + 0.25), 0.0, 1.0)
        return clamp(0.42 * weather_factor + 0.34 * lux_factor + 0.24 * internal_factor, 0.0, 1.0)

    def _score_to_transmission(self, channel_id: int, score: float, inputs: ControlInputs) -> float:
        if inputs.demo_mode == DemoMode.FLASHLIGHT_360:
            return clamp(0.90 - 0.87 * score, 0.03, 0.95)
        transmission = clamp(0.96 - 0.76 * score, 0.10, 0.96)
        if inputs.vehicle_mode in {VehicleMode.DRIVING, VehicleMode.DRIVING_STOPPED}:
            if channel_id == 0:
                floor = 0.48 if inputs.glare.strong else 0.62
                transmission = max(transmission, floor)
            elif channel_id == 1:
                floor = 0.44 if inputs.glare.strong else 0.55
                transmission = max(transmission, floor)
        return transmission

    def _state_for_transmission(self, transmission: float) -> OpticalState:
        if transmission >= 0.74:
            return OpticalState.CLEAR
        if transmission >= 0.24:
            return OpticalState.DIM
        return OpticalState.SCATTER

    def _reason(self, channel_id: int, breakdown: object, inputs: ControlInputs) -> str:
        parts: list[tuple[str, float]] = []
        for name in ("camera", "direction", "thermal", "privacy"):
            value = float(getattr(breakdown, name, 0.0))
            if value > 0.08:
                parts.append((name, value))
        if not parts:
            return "clear: no strong glare or thermal need"
        parts.sort(key=lambda item: item[1], reverse=True)
        lead = parts[0][0]
        if lead == "camera" and channel_id in {0, 1}:
            return "front fast response: AE glare/saturation"
        if lead == "thermal":
            return "thermal load priority"
        if lead == "direction":
            return f"directional light theta={inputs.fused_light.theta_deg:.0f}"
        if lead == "privacy":
            return "privacy need"
        return lead

