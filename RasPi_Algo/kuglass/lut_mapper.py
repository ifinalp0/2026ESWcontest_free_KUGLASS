from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

from .utils import clamp, lerp


@dataclass(frozen=True)
class LUTPoint:
    transmission: float
    mi: float


class LUTMapper:
    def __init__(self, points: list[LUTPoint] | None = None) -> None:
        self.points = sorted(
            points
            or [
                LUTPoint(0.00, 0.00),
                LUTPoint(0.15, 0.18),
                LUTPoint(0.35, 0.40),
                LUTPoint(0.65, 0.68),
                LUTPoint(0.85, 0.84),
                LUTPoint(1.00, 0.95),
            ],
            key=lambda p: p.transmission,
        )

    @classmethod
    def from_csv(cls, path: str | Path) -> "LUTMapper":
        points: list[LUTPoint] = []
        with Path(path).open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                points.append(LUTPoint(float(row["transmission"]), float(row["mi"])))
        return cls(points)

    def transmission_to_mi(self, transmission: float) -> float:
        value = clamp(transmission, 0.0, 1.0)
        points = self.points
        if value <= points[0].transmission:
            return points[0].mi
        for left, right in zip(points, points[1:]):
            if value <= right.transmission:
                span = max(right.transmission - left.transmission, 1e-9)
                return clamp(lerp(left.mi, right.mi, (value - left.transmission) / span), 0.0, 1.0)
        return clamp(points[-1].mi, 0.0, 1.0)

