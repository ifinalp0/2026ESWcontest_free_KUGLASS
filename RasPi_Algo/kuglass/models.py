from __future__ import annotations

from dataclasses import asdict, dataclass, field
from enum import Enum
from typing import Any

from .utils import now_ms


class OpticalState(str, Enum):
    CLEAR = "CLEAR"
    DIM = "DIM"
    SCATTER = "SCATTER"
    OFF = "OFF"


class VehicleMode(str, Enum):
    DRIVING = "driving"
    DRIVING_STOPPED = "driving_stopped"
    CAMPING = "camping"
    PARKED = "parked"


class DemoMode(str, Enum):
    NONE = "none"
    FLASHLIGHT_360 = "flashlight_360"
    CAMERA_SATURATION = "camera_saturation"
    HOT_SUMMER = "hot_summer"


@dataclass(frozen=True)
class AEMetadata:
    exposure_us: float = 10000.0
    analog_gain: float = 1.0
    digital_gain: float = 1.0
    lux_estimate: float | None = None
    ae_enabled: bool = True


@dataclass(frozen=True)
class ROIStats:
    name: str
    x: int
    y: int
    w: int
    h: int
    mean_brightness: float
    saturation_ratio: float
    edge_density: float
    highlight_area: float
    frame_id: int = 0
    timestamp_ms: int = field(default_factory=now_ms)


@dataclass(frozen=True)
class CameraMetrics:
    camera_id: str
    rois: list[ROIStats]
    ae: AEMetadata = field(default_factory=AEMetadata)
    frame_id: int = 0
    timestamp_ms: int = field(default_factory=now_ms)
    ok: bool = True
    degraded: bool = False

    def roi(self, name: str) -> ROIStats | None:
        for item in self.rois:
            if item.name == name:
                return item
        return None


@dataclass(frozen=True)
class FrontGlare:
    left_score: float
    right_score: float
    total_score: float
    strong: bool
    dominant_side: str


@dataclass(frozen=True)
class LuxSample:
    direction: str
    angle_deg: float
    lux_raw: float
    lux_filtered: float
    ok: bool = True


@dataclass(frozen=True)
class DirectionalLuxVector:
    lux_f: float
    lux_r: float
    lux_b: float
    lux_l: float
    theta_deg: float
    confidence: float
    total_lux: float
    degraded: bool = False
    timestamp_ms: int = field(default_factory=now_ms)


@dataclass(frozen=True)
class FusedLightVector:
    theta_deg: float
    confidence: float
    lux_theta_deg: float
    lux_confidence: float
    source: str


@dataclass(frozen=True)
class WeatherContext:
    temperature_c: float = 25.0
    cloud_cover: float = 0.4
    precipitation_mm: float = 0.0
    uv_index: float = 3.0
    stale: bool = True
    source: str = "mock"
    updated_ms: int = field(default_factory=now_ms)


@dataclass(frozen=True)
class ManualOverride:
    channel_id: int
    target_mi: float
    expires_ms: int
    enable: bool = True


@dataclass(frozen=True)
class ControlInputs:
    front_camera: CameraMetrics
    rear_camera: CameraMetrics | None
    glare: FrontGlare
    lux: DirectionalLuxVector
    fused_light: FusedLightVector
    weather: WeatherContext
    internal_temp_c: float
    vehicle_mode: VehicleMode
    demo_mode: DemoMode = DemoMode.NONE
    manual_overrides: dict[int, ManualOverride] = field(default_factory=dict)


@dataclass(frozen=True)
class ChannelTarget:
    channel_id: int
    target_transmission: float
    target_mi: float
    enable: bool
    optical_state: OpticalState
    score: float
    reason: str


@dataclass(frozen=True)
class PolicyDecision:
    seq: int
    targets: list[ChannelTarget]
    thermal_risk: float
    privacy_need: float
    strong_front_light: bool
    mode: VehicleMode
    demo_mode: DemoMode
    timestamp_ms: int = field(default_factory=now_ms)


@dataclass(frozen=True)
class ChannelTelemetry:
    channel_id: int
    applied_mi: float
    target_mi: float
    enable: bool
    vrms: float = 0.0
    irms: float = 0.0
    temp_c: float = 0.0
    fault: bool = False
    fault_code: str = ""


@dataclass(frozen=True)
class ControllerTelemetry:
    controller_id: str
    seq: int
    fault: bool
    mode: str
    dc_v: float
    channels: list[ChannelTelemetry]
    timestamp_ms: int = field(default_factory=now_ms)


def to_jsonable(value: Any) -> Any:
    if isinstance(value, Enum):
        return value.value
    if hasattr(value, "__dataclass_fields__"):
        return {k: to_jsonable(v) for k, v in asdict(value).items()}
    if isinstance(value, dict):
        return {str(k): to_jsonable(v) for k, v in value.items()}
    if isinstance(value, list):
        return [to_jsonable(v) for v in value]
    return value

