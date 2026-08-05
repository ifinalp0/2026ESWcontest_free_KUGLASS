from __future__ import annotations

import threading
import time
from dataclasses import dataclass


CAMERA_MAGIC = b"KUGLCAM1"
CAMERA_FORMAT_JPEG = 2
CAMERA_MAX_PAYLOAD = 512 * 1024


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


@dataclass(frozen=True)
class CameraFrame:
    sequence: int
    width: int
    height: int
    payload: bytes
    received_at: float


class CameraFrameStore:
    """Thread-safe latest-frame store shared by the USB gateway and HTTP UI."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._frame: CameraFrame | None = None
        self._good_frames = 0
        self._started_at = time.monotonic()

    def clear(self) -> None:
        with self._lock:
            self._frame = None
            self._good_frames = 0
            self._started_at = time.monotonic()

    def update(self, frame: CameraFrame) -> None:
        with self._lock:
            self._frame = frame
            self._good_frames += 1

    def snapshot(self) -> CameraFrame | None:
        with self._lock:
            return self._frame

    def status(self, *, requested: bool, bad_frames: int = 0) -> dict[str, object]:
        now = time.monotonic()
        with self._lock:
            frame = self._frame
            elapsed = max(now - self._started_at, 0.001)
            return {
                "requested": requested,
                "frameReady": frame is not None,
                "sequence": frame.sequence if frame else None,
                "width": frame.width if frame else None,
                "height": frame.height if frame else None,
                "format": "jpeg" if frame else None,
                "payloadBytes": len(frame.payload) if frame else None,
                "frameAgeSeconds": round(now - frame.received_at, 3) if frame else None,
                "goodFrames": self._good_frames,
                "badFrames": bad_frames,
                "averageFps": round(self._good_frames / elapsed, 2),
            }
