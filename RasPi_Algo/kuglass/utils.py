from __future__ import annotations

import math
import time
from typing import Iterable


EPS = 1e-9


def now_ms() -> int:
    return int(time.time() * 1000)


def clamp(value: float, low: float, high: float) -> float:
    if value < low:
        return low
    if value > high:
        return high
    return value


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * clamp(t, 0.0, 1.0)


def percentile(values: Iterable[float], p: float) -> float:
    data = sorted(float(v) for v in values)
    if not data:
        return 0.0
    idx = clamp(p / 100.0, 0.0, 1.0) * (len(data) - 1)
    lo = int(math.floor(idx))
    hi = int(math.ceil(idx))
    if lo == hi:
        return data[lo]
    return lerp(data[lo], data[hi], idx - lo)


def angle_to_unit(theta_deg: float) -> tuple[float, float]:
    rad = math.radians(theta_deg)
    return math.cos(rad), math.sin(rad)


def unit_to_angle(x: float, y: float) -> float:
    if abs(x) < EPS and abs(y) < EPS:
        return 0.0
    return math.degrees(math.atan2(y, x)) % 360.0


def angular_distance_deg(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def angular_kernel(theta: float, center: float, width_deg: float = 55.0) -> float:
    dist = angular_distance_deg(theta, center)
    if dist >= width_deg * 2.0:
        return 0.0
    # Smooth triangular-ish kernel without requiring numpy.
    normalized = dist / max(width_deg, EPS)
    return math.exp(-0.5 * normalized * normalized)


def low_pass(previous: float | None, current: float, alpha: float) -> float:
    if previous is None:
        return current
    return previous + clamp(alpha, 0.0, 1.0) * (current - previous)

