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
    decisionReason: str = "Driving baseline: all channels remain mostly clear."
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
    ChannelConfig(0, "CH0 Front Left", "driver forward glare and safety floor", 340.0, 0.58),
    ChannelConfig(1, "CH1 Front Right", "front passenger glare", 20.0, 0.50),
    ChannelConfig(2, "CH2 Left Front Door", "left side glare and privacy", 290.0, 0.34),
    ChannelConfig(3, "CH3 Right Front Door", "right side glare and privacy", 70.0, 0.34),
    ChannelConfig(4, "CH4 Left Rear Door", "left rear thermal and privacy", 235.0, 0.18),
    ChannelConfig(5, "CH5 Right Rear Door", "right rear thermal and privacy", 125.0, 0.18),
    ChannelConfig(6, "CH6 Rear Glass", "rear glare and thermal load", 180.0, 0.16),
    ChannelConfig(7, "CH7 Sunroof", "top solar thermal load", None, 0.12),
)
