from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterable

from .models import ChannelTarget, ControlInputs, PolicyDecision
from .utils import now_ms


class CSVLogWriter:
    def __init__(self, log_dir: str | Path) -> None:
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.path = self.log_dir / f"kuglass_run_{now_ms()}.csv"
        self._handle = self.path.open("w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(
            self._handle,
            fieldnames=[
                "timestamp_ms",
                "seq",
                "mode",
                "demo_mode",
                "thermal_risk",
                "privacy_need",
                "strong_front_light",
                "theta_lux",
                "theta_fused",
                "lux_total",
                "internal_temp_c",
                "weather_temp_c",
                "channel_id",
                "target_mi",
                "target_transmission",
                "optical_state",
                "score",
                "reason",
            ],
        )
        self._writer.writeheader()

    def write_decision(self, inputs: ControlInputs, decision: PolicyDecision) -> None:
        for target in decision.targets:
            self._writer.writerow(self._row(inputs, decision, target))
        self._handle.flush()

    def close(self) -> None:
        self._handle.close()

    def _row(self, inputs: ControlInputs, decision: PolicyDecision, target: ChannelTarget) -> dict:
        return {
            "timestamp_ms": decision.timestamp_ms,
            "seq": decision.seq,
            "mode": decision.mode.value,
            "demo_mode": decision.demo_mode.value,
            "thermal_risk": f"{decision.thermal_risk:.4f}",
            "privacy_need": f"{decision.privacy_need:.4f}",
            "strong_front_light": int(decision.strong_front_light),
            "theta_lux": f"{inputs.lux.theta_deg:.2f}",
            "theta_fused": f"{inputs.fused_light.theta_deg:.2f}",
            "lux_total": f"{inputs.lux.total_lux:.2f}",
            "internal_temp_c": f"{inputs.internal_temp_c:.2f}",
            "weather_temp_c": f"{inputs.weather.temperature_c:.2f}",
            "channel_id": target.channel_id,
            "target_mi": f"{target.target_mi:.4f}",
            "target_transmission": f"{target.target_transmission:.4f}",
            "optical_state": target.optical_state.value,
            "score": f"{target.score:.4f}",
            "reason": target.reason,
        }

