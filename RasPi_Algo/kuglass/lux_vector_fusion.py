from __future__ import annotations

from .models import CameraMetrics, DirectionalLuxVector, FrontGlare, FusedLightVector
from .utils import angle_to_unit, clamp, unit_to_angle


class LuxVectorFusion:
    def fuse(
        self,
        lux: DirectionalLuxVector,
        front_glare: FrontGlare,
        rear_camera: CameraMetrics | None = None,
        flashlight_demo: bool = False,
    ) -> FusedLightVector:
        lx, ly = angle_to_unit(lux.theta_deg)
        vx = lx * lux.confidence
        vy = ly * lux.confidence
        source_parts = ["lux"]

        front_conf = clamp(front_glare.total_score, 0.0, 1.0)
        if front_conf > 0.18:
            if front_glare.dominant_side == "left":
                front_angle = 345.0
            elif front_glare.dominant_side == "right":
                front_angle = 15.0
            else:
                front_angle = 0.0
            ux, uy = angle_to_unit(front_angle)
            weight = 0.75 if not flashlight_demo else 0.45
            vx += ux * front_conf * weight
            vy += uy * front_conf * weight
            source_parts.append("front_camera")

        rear_conf = self._rear_highlight_confidence(rear_camera)
        if rear_conf > 0.20:
            ux, uy = angle_to_unit(180.0)
            weight = 0.42 if flashlight_demo else 0.28
            vx += ux * rear_conf * weight
            vy += uy * rear_conf * weight
            source_parts.append("rear_camera")

        conf = clamp((vx * vx + vy * vy) ** 0.5, 0.0, 1.0)
        theta = unit_to_angle(vx, vy)
        return FusedLightVector(theta, conf, lux.theta_deg, lux.confidence, "+".join(source_parts))

    def _rear_highlight_confidence(self, metrics: CameraMetrics | None) -> float:
        if metrics is None or not metrics.rois:
            return 0.0
        return clamp(max(roi.saturation_ratio for roi in metrics.rois), 0.0, 1.0)

