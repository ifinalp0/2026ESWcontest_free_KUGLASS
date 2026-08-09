from __future__ import annotations

import math
from typing import Any


MAX_MI = 0.70


def clamp_mi(value: Any) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0.0
    if not math.isfinite(number):
        return 0.0
    return max(0.0, min(MAX_MI, number))


def normalized_mi(value: Any) -> float:
    return clamp_mi(value) / MAX_MI
