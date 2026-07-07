from __future__ import annotations

from dataclasses import dataclass

from .channels import ChannelSpec
from .models import ControlInputs, DemoMode, VehicleMode
from .utils import angular_kernel, clamp


@dataclass(frozen=True)
class ChannelScore:
    value: float
    mode_need: float
    thermal: float
    direction: float
    camera: float
    privacy: float
    visibility_penalty: float


def score_channel(spec: ChannelSpec, inputs: ControlInputs, thermal_risk: float, privacy_need: float) -> ChannelScore:
    if inputs.demo_mode == DemoMode.FLASHLIGHT_360:
        direction = _direction_hit(spec, inputs)
        roof_bonus = clamp(inputs.lux.total_lux / 40000.0, 0.0, 1.0) if spec.channel_id == 7 else 0.0
        value = clamp(max(direction, roof_bonus * 0.65), 0.0, 1.0)
        return ChannelScore(value, 0.0, 0.0, direction, 0.0, 0.0, 0.0)

    mode_need = 0.0
    if inputs.vehicle_mode == VehicleMode.DRIVING_STOPPED:
        mode_need = 0.08
    direction = _direction_hit(spec, inputs)
    camera = _camera_need(spec, inputs)
    thermal = _thermal_need(spec, thermal_risk)
    visibility_penalty = _visibility_penalty(spec, inputs)
    value = (
        0.22 * mode_need
        + _thermal_weight(spec) * thermal
        + _direction_weight(spec) * direction
        + _camera_weight(spec) * camera
        + _privacy_weight(spec) * privacy_need
        - _visibility_weight(spec) * visibility_penalty
    )
    return ChannelScore(clamp(value, 0.0, 1.0), mode_need, thermal, direction, camera, privacy_need, visibility_penalty)


def _direction_hit(spec: ChannelSpec, inputs: ControlInputs) -> float:
    if spec.channel_id == 7:
        return clamp(inputs.lux.total_lux / 48000.0, 0.0, 1.0) * inputs.fused_light.confidence
    if spec.angle_deg is None:
        return 0.0
    return angular_kernel(inputs.fused_light.theta_deg, spec.angle_deg, spec.direction_width_deg) * inputs.fused_light.confidence


def _camera_need(spec: ChannelSpec, inputs: ControlInputs) -> float:
    if spec.channel_id == 0:
        return inputs.glare.left_score
    if spec.channel_id == 1:
        return inputs.glare.right_score
    if spec.channel_id == 6 and inputs.rear_camera and inputs.rear_camera.rois:
        return max(roi.saturation_ratio for roi in inputs.rear_camera.rois) * 0.4
    return 0.0


def _thermal_need(spec: ChannelSpec, thermal_risk: float) -> float:
    priority_gain = {1: 1.0, 2: 0.86, 3: 0.72, 4: 0.52, 5: 0.30}
    return thermal_risk * priority_gain.get(spec.heat_priority, 0.4)


def _visibility_penalty(spec: ChannelSpec, inputs: ControlInputs) -> float:
    if inputs.vehicle_mode in {VehicleMode.CAMPING, VehicleMode.PARKED}:
        return 0.0
    if spec.channel_id == 0:
        return 0.72
    if spec.channel_id == 1:
        return 0.55
    if spec.channel_id in {2, 3}:
        return 0.22
    return 0.04


def _thermal_weight(spec: ChannelSpec) -> float:
    return {0: 0.22, 1: 0.24, 2: 0.44, 3: 0.44, 4: 0.58, 5: 0.58, 6: 0.68, 7: 0.78}.get(spec.channel_id, 0.4)


def _direction_weight(spec: ChannelSpec) -> float:
    return {0: 0.40, 1: 0.42, 2: 0.68, 3: 0.68, 4: 0.72, 5: 0.72, 6: 0.75, 7: 0.35}.get(spec.channel_id, 0.5)


def _camera_weight(spec: ChannelSpec) -> float:
    return {0: 0.82, 1: 0.78, 6: 0.25}.get(spec.channel_id, 0.05)


def _privacy_weight(spec: ChannelSpec) -> float:
    return 0.95 if spec.channel_id in {0, 1} else 1.0


def _visibility_weight(spec: ChannelSpec) -> float:
    return {0: 0.35, 1: 0.26, 2: 0.10, 3: 0.10}.get(spec.channel_id, 0.02)

