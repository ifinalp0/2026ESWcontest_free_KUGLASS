from __future__ import annotations

from dataclasses import dataclass, field

from .command_validator import validate_ui_command
from .models import DemoMode, ManualOverride, VehicleMode
from .utils import now_ms


@dataclass
class ModeManager:
    vehicle_mode: VehicleMode = VehicleMode.DRIVING
    demo_mode: DemoMode = DemoMode.NONE
    manual_overrides: dict[int, ManualOverride] = field(default_factory=dict)

    def apply_payload(self, payload: dict) -> None:
        cmd = validate_ui_command(payload)
        if cmd.command == "set_mode" and cmd.vehicle_mode is not None:
            self.vehicle_mode = cmd.vehicle_mode
            if self.vehicle_mode in {VehicleMode.CAMPING, VehicleMode.PARKED}:
                self.demo_mode = DemoMode.NONE
        elif cmd.command == "set_demo" and cmd.demo_mode is not None:
            self.demo_mode = cmd.demo_mode
            if cmd.demo_mode == DemoMode.HOT_SUMMER:
                self.vehicle_mode = VehicleMode.DRIVING
        elif cmd.command == "manual_channel" and cmd.manual is not None:
            self.manual_overrides[cmd.manual.channel_id] = cmd.manual
        elif cmd.command == "return_auto":
            self.manual_overrides.clear()
            self.demo_mode = DemoMode.NONE
            self.vehicle_mode = VehicleMode.DRIVING

    def active_manuals(self) -> dict[int, ManualOverride]:
        current = now_ms()
        self.manual_overrides = {
            ch: override for ch, override in self.manual_overrides.items() if override.expires_ms > current
        }
        return dict(self.manual_overrides)

