from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Protocol

from .lux_calibrator import LuxCalibration
from .models import DirectionalLuxVector, LuxSample
from .utils import angle_to_unit, low_pass, percentile, unit_to_angle


SENSOR_ANGLES = {"F": 0.0, "R": 90.0, "B": 180.0, "L": 270.0}


class LuxReader(Protocol):
    def read_lux(self) -> dict[str, float | None]:
        ...


@dataclass
class MockLuxReader:
    phase: float = 0.0

    def read_lux(self) -> dict[str, float | None]:
        self.phase = (self.phase + 8.0) % 360.0
        theta = math.radians(self.phase)
        ambient = 650.0
        direct = 14500.0
        values: dict[str, float | None] = {}
        for key, angle in SENSOR_ANGLES.items():
            ax, ay = angle_to_unit(angle)
            hit = max(0.0, math.cos(theta) * ax + math.sin(theta) * ay)
            values[key] = ambient + direct * hit * hit
        return values


class VEML7700TCAReader:
    def __init__(self, bus: int = 1, mux_addr: int = 0x70, sensor_addr: int = 0x10) -> None:
        try:
            from smbus2 import SMBus  # type: ignore
        except Exception as exc:
            raise RuntimeError("smbus2 is required for real VEML7700 reads") from exc
        self._bus = SMBus(bus)
        self._mux_addr = mux_addr
        self._sensor_addr = sensor_addr
        self._channels = {"F": 0, "R": 1, "B": 2, "L": 3}

    def read_lux(self) -> dict[str, float | None]:
        values: dict[str, float | None] = {}
        for direction, mux_channel in self._channels.items():
            try:
                self._select(mux_channel)
                raw = self._bus.read_word_data(self._sensor_addr, 0x04)
                raw_swapped = ((raw & 0xFF) << 8) | (raw >> 8)
                values[direction] = raw_swapped * 0.0576
            except OSError:
                values[direction] = None
        return values

    def _select(self, channel: int) -> None:
        self._bus.write_byte(self._mux_addr, 1 << channel)


@dataclass
class DirectionalLuxService:
    reader: LuxReader = field(default_factory=MockLuxReader)
    calibration: LuxCalibration = field(default_factory=LuxCalibration)
    alpha: float = 0.32
    _filtered: dict[str, float] = field(default_factory=dict)

    def read_samples(self) -> list[LuxSample]:
        raw = self.reader.read_lux()
        samples: list[LuxSample] = []
        for direction, angle in SENSOR_ANGLES.items():
            value = raw.get(direction)
            ok = value is not None
            calibrated = self.calibration.apply(direction, float(value or 0.0))
            filtered = low_pass(self._filtered.get(direction), calibrated, self.alpha)
            self._filtered[direction] = filtered
            samples.append(LuxSample(direction, angle, float(value or 0.0), filtered, ok))
        return samples

    def read_vector(self) -> DirectionalLuxVector:
        samples = self.read_samples()
        filtered = [sample.lux_filtered for sample in samples if sample.ok]
        ambient_floor = percentile(filtered, 20.0) if filtered else 0.0
        vx = 0.0
        vy = 0.0
        total_dir = 0.0
        valid_count = 0
        values = {"F": 0.0, "R": 0.0, "B": 0.0, "L": 0.0}
        directional_values: list[float] = []
        for sample in samples:
            values[sample.direction] = sample.lux_filtered
            if not sample.ok:
                continue
            valid_count += 1
            direct = max(0.0, sample.lux_filtered - ambient_floor)
            directional_values.append(direct)
            ux, uy = angle_to_unit(sample.angle_deg)
            vx += direct * ux
            vy += direct * uy
            total_dir += direct
        theta = unit_to_angle(vx, vy)
        if total_dir <= 1e-6 or not directional_values:
            confidence = 0.0
        else:
            med = percentile(directional_values, 50.0)
            confidence = max(0.0, max(directional_values) - med) / max(sum(directional_values), 1e-6)
        return DirectionalLuxVector(
            values["F"],
            values["R"],
            values["B"],
            values["L"],
            theta,
            min(1.0, confidence * 2.0),
            sum(values.values()),
            degraded=valid_count < 4,
        )


def make_lux_service(config: dict) -> DirectionalLuxService:
    sensor_cfg = config.get("sensors", {})
    source = sensor_cfg.get("source", "mock")
    if source == "veml7700":
        reader = VEML7700TCAReader(
            int(sensor_cfg.get("i2c_bus", 1)),
            int(sensor_cfg.get("tca9548a_address", 0x70)),
            int(sensor_cfg.get("veml7700_address", 0x10)),
        )
    else:
        reader = MockLuxReader()
    return DirectionalLuxService(reader)

