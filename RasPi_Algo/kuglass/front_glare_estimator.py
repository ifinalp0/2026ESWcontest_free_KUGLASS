from __future__ import annotations

from .models import CameraMetrics, FrontGlare, ROIStats
from .utils import clamp


class FrontGlareEstimator:
    def __init__(self, strong_threshold: float = 0.46) -> None:
        self.strong_threshold = strong_threshold

    def estimate(self, metrics: CameraMetrics) -> FrontGlare:
        left = metrics.roi("front_left")
        right = metrics.roi("front_right")
        left_score = self._roi_score(left, metrics) if left else 0.0
        right_score = self._roi_score(right, metrics) if right else 0.0
        total = max(left_score, right_score)
        if abs(left_score - right_score) < 0.05:
            dominant = "center"
        else:
            dominant = "left" if left_score > right_score else "right"
        return FrontGlare(left_score, right_score, total, total >= self.strong_threshold, dominant)

    def _roi_score(self, roi: ROIStats | None, metrics: CameraMetrics) -> float:
        if roi is None:
            return 0.0
        ae = metrics.ae
        gain = max(ae.analog_gain * ae.digital_gain, 0.05)
        # AE usually shortens exposure under intense light. This term is only a weak cue.
        exposure_pressure = clamp((12000.0 / max(ae.exposure_us, 100.0) - 0.75) / 3.0, 0.0, 1.0)
        gain_pressure = clamp((1.5 / gain - 0.25), 0.0, 1.0)
        edge_penalty = clamp((0.08 - roi.edge_density) / 0.08, 0.0, 0.25)
        return clamp(
            0.62 * roi.saturation_ratio
            + 0.18 * roi.highlight_area
            + 0.12 * exposure_pressure
            + 0.08 * gain_pressure
            + edge_penalty,
            0.0,
            1.0,
        )

