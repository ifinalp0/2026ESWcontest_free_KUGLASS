from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class LuxCalibration:
    gains: dict[str, float] = field(default_factory=lambda: {"F": 1.0, "R": 1.0, "B": 1.0, "L": 1.0})
    dark_offsets: dict[str, float] = field(default_factory=lambda: {"F": 0.0, "R": 0.0, "B": 0.0, "L": 0.0})

    def apply(self, direction: str, raw_lux: float) -> float:
        gain = self.gains.get(direction, 1.0)
        offset = self.dark_offsets.get(direction, 0.0)
        return max(0.0, raw_lux * gain - offset)

