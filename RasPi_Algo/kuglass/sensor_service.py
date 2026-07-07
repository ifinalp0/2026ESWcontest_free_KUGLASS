from __future__ import annotations

import math
import time
from dataclasses import dataclass


@dataclass
class InternalTemperatureService:
    source: str = "mock"
    ds18b20_path: str = ""

    def read_c(self) -> float:
        if self.source == "ds18b20" and self.ds18b20_path:
            return self._read_ds18b20()
        return 29.0 + 3.0 * math.sin(time.time() / 18.0)

    def _read_ds18b20(self) -> float:
        with open(self.ds18b20_path, "r", encoding="utf-8") as handle:
            text = handle.read()
        marker = "t="
        if marker not in text:
            raise RuntimeError("DS18B20 temperature marker not found")
        return float(text.split(marker, 1)[1].strip()) / 1000.0


def make_temperature_service(config: dict) -> InternalTemperatureService:
    sensors = config.get("sensors", {})
    return InternalTemperatureService(str(sensors.get("temperature_source", "mock")), str(sensors.get("ds18b20_path", "")))

