from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any, Literal


VehicleMode = Literal["driving", "stopped", "camping", "parked"]
DemoMode = Literal["none", "hot_summer", "camping", "parked", "camera_saturation", "flashlight_360"]


@dataclass(frozen=True)
class ChannelConfig:
    channel: int
    name: str
    role: str
    bearing_deg: float | None
    visibility_floor: float


@dataclass
class ChannelState:
    channel: int
    name: str
    targetMi: float = 0.95
    appliedMi: float = 0.95
    estimatedTransmittance: float = 0.92
    opticalState: str = "CLEAR"
    fault: bool = False
    manualUntil: float | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class EnvironmentInput:
    frontLux: float | None = 280.0
    rightLux: float | None = 180.0
    rearLux: float | None = 140.0
    leftLux: float | None = 170.0
    topLux: float | None = 260.0
    internalTemp: float = 27.0
    weatherTemp: float = 28.0
    frontLeftSaturation: float = 0.08
    frontRightSaturation: float = 0.07
    edgeDensity: float = 0.86

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class CameraMetrics:
    frontLeftSaturation: float = 0.08
    frontRightSaturation: float = 0.07
    edgeDensity: float = 0.86
    glare: float = 0.0
    frameId: int = 0
    timestamp: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class SimulationState:
    schemaVersion: int = 1
    vehicleMode: VehicleMode = "driving"
    demoMode: DemoMode = "none"
    environment: EnvironmentInput = field(default_factory=EnvironmentInput)
    channels: list[ChannelState] = field(default_factory=list)
    cameraMetrics: CameraMetrics = field(default_factory=CameraMetrics)
    decisionReason: str = "주행 기본 상태: 전 채널을 투명에 가깝게 유지합니다."
    timestamp: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return {
            "schemaVersion": self.schemaVersion,
            "vehicleMode": self.vehicleMode,
            "demoMode": self.demoMode,
            "environment": self.environment.to_dict(),
            "channels": [channel.to_dict() for channel in self.channels],
            "cameraMetrics": self.cameraMetrics.to_dict(),
            "decisionReason": self.decisionReason,
            "timestamp": self.timestamp,
        }


CHANNEL_CONFIGS: tuple[ChannelConfig, ...] = (
    ChannelConfig(0, "CH0 전면 좌측", "운전자 전방 강광 및 시야 안전 하한", 345.0, 0.58),
    ChannelConfig(1, "CH1 전면 우측", "동승석 전방 강광 대응", 15.0, 0.50),
    ChannelConfig(2, "CH2 좌측 전방 도어", "좌측 측면 일사 및 프라이버시", 285.0, 0.34),
    ChannelConfig(3, "CH3 우측 전방 도어", "우측 측면 일사 및 프라이버시", 75.0, 0.34),
    ChannelConfig(4, "CH4 좌측 후방 도어", "좌측 후방 열부하 및 프라이버시", 245.0, 0.18),
    ChannelConfig(5, "CH5 우측 후방 도어", "우측 후방 열부하 및 프라이버시", 115.0, 0.18),
    ChannelConfig(6, "CH6 후면 유리", "후방 광원 및 열부하", 180.0, 0.16),
    ChannelConfig(7, "CH7 선루프", "상부 일사 및 열부하", None, 0.12),
)
