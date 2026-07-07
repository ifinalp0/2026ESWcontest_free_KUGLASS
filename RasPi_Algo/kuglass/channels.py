from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ChannelSpec:
    channel_id: int
    name: str
    controller_id: str
    local_index: int
    angle_deg: float | None
    heat_priority: int
    driving_min_transmission: float
    direction_width_deg: float = 55.0


CHANNELS: tuple[ChannelSpec, ...] = (
    ChannelSpec(0, "front_left", "A", 0, 345.0, 5, 0.62, 42.0),
    ChannelSpec(1, "front_right", "A", 1, 15.0, 5, 0.55, 42.0),
    ChannelSpec(2, "left_front_door", "A", 2, 285.0, 4, 0.45),
    ChannelSpec(3, "right_front_door", "A", 3, 75.0, 4, 0.45),
    ChannelSpec(4, "left_rear_door", "B", 0, 245.0, 3, 0.25),
    ChannelSpec(5, "right_rear_door", "B", 1, 115.0, 3, 0.25),
    ChannelSpec(6, "rear_window", "B", 2, 180.0, 2, 0.20),
    ChannelSpec(7, "sunroof", "B", 3, None, 1, 0.15),
)

CHANNEL_BY_ID = {spec.channel_id: spec for spec in CHANNELS}


def channels_for_controller(controller_id: str) -> list[ChannelSpec]:
    return [spec for spec in CHANNELS if spec.controller_id == controller_id]

