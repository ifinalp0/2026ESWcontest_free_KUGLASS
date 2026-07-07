from __future__ import annotations

from collections.abc import Sequence

from .models import ROIStats
from .utils import now_ms


Pixel = int | tuple[int, int, int] | list[int]
Frame = Sequence[Sequence[Pixel]]


class ROIAnalyzer:
    def __init__(self, saturation_threshold: int = 245, edge_threshold: int = 22) -> None:
        self.saturation_threshold = saturation_threshold
        self.edge_threshold = edge_threshold

    def analyze(self, frame: object, rois: dict[str, tuple[int, int, int, int]], frame_id: int = 0) -> list[ROIStats]:
        if _has_numpy(frame):
            return self._analyze_numpy(frame, rois, frame_id)
        return [self._analyze_pure(frame, name, rect, frame_id) for name, rect in rois.items()]  # type: ignore[arg-type]

    def _analyze_numpy(self, frame: object, rois: dict[str, tuple[int, int, int, int]], frame_id: int) -> list[ROIStats]:
        import numpy as np  # type: ignore

        data = frame
        out: list[ROIStats] = []
        for name, (x, y, w, h) in rois.items():
            crop = data[y : y + h, x : x + w]
            if crop.size == 0:
                out.append(ROIStats(name, x, y, w, h, 0.0, 0.0, 0.0, 0.0, frame_id))
                continue
            if len(crop.shape) == 3:
                gray = crop.mean(axis=2)
                saturated = (crop >= self.saturation_threshold).any(axis=2)
            else:
                gray = crop.astype(float)
                saturated = crop >= self.saturation_threshold
            mean = float(gray.mean()) / 255.0
            sat_ratio = float(saturated.mean())
            highlight = sat_ratio
            dx = np.abs(np.diff(gray, axis=1))
            dy = np.abs(np.diff(gray, axis=0))
            edge_pixels = 0
            edge_total = 0
            if dx.size:
                edge_pixels += int((dx >= self.edge_threshold).sum())
                edge_total += int(dx.size)
            if dy.size:
                edge_pixels += int((dy >= self.edge_threshold).sum())
                edge_total += int(dy.size)
            edge_density = edge_pixels / edge_total if edge_total else 0.0
            out.append(ROIStats(name, x, y, w, h, mean, sat_ratio, edge_density, highlight, frame_id))
        return out

    def _analyze_pure(self, frame: Frame, name: str, rect: tuple[int, int, int, int], frame_id: int) -> ROIStats:
        x, y, w, h = rect
        values: list[int] = []
        for row in frame[y : y + h]:
            for pixel in row[x : x + w]:
                values.append(_brightness(pixel))
        if not values:
            return ROIStats(name, x, y, w, h, 0.0, 0.0, 0.0, 0.0, frame_id, now_ms())

        saturated = sum(1 for v in values if v >= self.saturation_threshold)
        mean = sum(values) / (len(values) * 255.0)
        sat_ratio = saturated / len(values)
        edge_count = 0
        edge_total = 0
        for row_idx in range(y, min(y + h, len(frame))):
            row = frame[row_idx]
            for col_idx in range(x, min(x + w - 1, len(row) - 1)):
                if abs(_brightness(row[col_idx + 1]) - _brightness(row[col_idx])) >= self.edge_threshold:
                    edge_count += 1
                edge_total += 1
        for row_idx in range(y, min(y + h - 1, len(frame) - 1)):
            row = frame[row_idx]
            next_row = frame[row_idx + 1]
            for col_idx in range(x, min(x + w, len(row), len(next_row))):
                if abs(_brightness(next_row[col_idx]) - _brightness(row[col_idx])) >= self.edge_threshold:
                    edge_count += 1
                edge_total += 1
        edge_density = edge_count / edge_total if edge_total else 0.0
        return ROIStats(name, x, y, w, h, mean, sat_ratio, edge_density, sat_ratio, frame_id, now_ms())


def default_front_rois(width: int, height: int) -> dict[str, tuple[int, int, int, int]]:
    return {
        "front_left": (0, 0, width // 2, height),
        "front_right": (width // 2, 0, width - width // 2, height),
    }


def default_rear_rois(width: int, height: int) -> dict[str, tuple[int, int, int, int]]:
    third = width // 3
    return {
        "rear_left": (0, 0, third, height),
        "rear_center": (third, 0, third, height),
        "rear_right": (third * 2, 0, width - third * 2, height),
    }


def _brightness(pixel: Pixel) -> int:
    if isinstance(pixel, int):
        return pixel
    if not pixel:
        return 0
    return int(sum(int(v) for v in pixel[:3]) / min(len(pixel), 3))


def _has_numpy(frame: object) -> bool:
    return hasattr(frame, "shape") and hasattr(frame, "__getitem__")

