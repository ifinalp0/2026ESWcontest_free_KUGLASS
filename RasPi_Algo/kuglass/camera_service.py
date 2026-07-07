from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Protocol

from .models import AEMetadata, CameraMetrics
from .roi_analyzer import ROIAnalyzer, default_front_rois, default_rear_rois
from .utils import clamp, now_ms


class CameraSource(Protocol):
    camera_id: str

    def capture(self) -> tuple[object, AEMetadata, int]:
        ...


@dataclass
class MockCameraSource:
    camera_id: str
    width: int = 320
    height: int = 180
    phase: float = 0.0
    frame_id: int = 0

    def capture(self) -> tuple[object, AEMetadata, int]:
        self.frame_id += 1
        self.phase += 0.17 if self.camera_id == "front" else 0.11
        hotspot_x = int((math.sin(self.phase) * 0.45 + 0.5) * (self.width - 1))
        hotspot_y = int(self.height * (0.35 if self.camera_id == "front" else 0.55))
        frame: list[list[int]] = []
        for y in range(self.height):
            row: list[int] = []
            for x in range(self.width):
                base = 72 + int(35 * (x / max(self.width - 1, 1)))
                dist2 = (x - hotspot_x) ** 2 + (y - hotspot_y) ** 2
                highlight = int(210 * math.exp(-dist2 / 380.0))
                row.append(int(clamp(base + highlight, 0, 255)))
            frame.append(row)
        exposure = 8500.0 - min(5000.0, 18.0 * max(0, frame[hotspot_y][hotspot_x] - 180))
        ae = AEMetadata(exposure_us=max(1200.0, exposure), analog_gain=1.0, digital_gain=1.0, ae_enabled=True)
        return frame, ae, self.frame_id


class OpenCVCameraSource:
    def __init__(self, camera_id: str, index: int = 0, width: int = 640, height: int = 360) -> None:
        import cv2  # type: ignore

        self.camera_id = camera_id
        self.frame_id = 0
        self._cv2 = cv2
        self._cap = cv2.VideoCapture(index)
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)

    def capture(self) -> tuple[object, AEMetadata, int]:
        ok, frame = self._cap.read()
        if not ok:
            raise RuntimeError(f"camera {self.camera_id} capture failed")
        self.frame_id += 1
        return frame, AEMetadata(), self.frame_id


class CameraService:
    def __init__(
        self,
        front: CameraSource,
        rear: CameraSource | None,
        analyzer: ROIAnalyzer,
        width: int,
        height: int,
    ) -> None:
        self.front = front
        self.rear = rear
        self.analyzer = analyzer
        self.width = width
        self.height = height

    def capture_front(self) -> CameraMetrics:
        frame, ae, frame_id = self.front.capture()
        rois = self.analyzer.analyze(frame, default_front_rois(self.width, self.height), frame_id)
        return CameraMetrics("front", rois, ae, frame_id, now_ms())

    def capture_rear(self) -> CameraMetrics | None:
        if self.rear is None:
            return None
        frame, ae, frame_id = self.rear.capture()
        rois = self.analyzer.analyze(frame, default_rear_rois(self.width, self.height), frame_id)
        return CameraMetrics("rear", rois, ae, frame_id, now_ms())


def make_camera_service(config: dict) -> CameraService:
    camera_cfg = config.get("camera", {})
    width = int(camera_cfg.get("width", 320))
    height = int(camera_cfg.get("height", 180))
    analyzer = ROIAnalyzer(
        int(camera_cfg.get("saturation_threshold", 245)),
        int(camera_cfg.get("edge_threshold", 22)),
    )
    source = camera_cfg.get("source", "mock")
    if source == "opencv":
        front = OpenCVCameraSource("front", int(camera_cfg.get("front_index", 0)), width, height)
        rear = OpenCVCameraSource("rear", int(camera_cfg.get("rear_index", 1)), width, height)
    else:
        front = MockCameraSource("front", width, height)
        rear = MockCameraSource("rear", width, height)
    return CameraService(front, rear, analyzer, width, height)

