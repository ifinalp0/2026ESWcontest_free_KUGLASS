from __future__ import annotations

from .models import DemoMode, VehicleMode


DEMO_TO_MODE = {
    DemoMode.FLASHLIGHT_360: VehicleMode.DRIVING_STOPPED,
    DemoMode.CAMERA_SATURATION: VehicleMode.DRIVING,
    DemoMode.HOT_SUMMER: VehicleMode.DRIVING,
}


def mode_for_demo(demo_mode: DemoMode, fallback: VehicleMode) -> VehicleMode:
    return DEMO_TO_MODE.get(demo_mode, fallback)

