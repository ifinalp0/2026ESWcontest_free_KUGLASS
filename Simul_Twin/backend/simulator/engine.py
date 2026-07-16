from __future__ import annotations

import math
import threading
import time
from datetime import datetime
from collections.abc import Callable
from pathlib import Path
from typing import Any

from .config import cfg_float, load_policy_config
from .models import (
    CHANNEL_CONFIGS,
    CameraMetrics,
    ChannelState,
    DemoMode,
    EnvironmentInput,
    SimulationState,
    VehicleMode,
)
from .policy import clamp, estimated_transmittance, optical_state
from .policy import PolicyEngine


class SimulationEngine:
    def __init__(
        self,
        time_fn: Callable[[], float] | None = None,
        config: dict[str, Any] | None = None,
        replay_dir: str | Path | None = None,
    ) -> None:
        self._time_fn = time_fn or time.time
        self._lock = threading.RLock()
        self.config = config or load_policy_config()
        self._policy = PolicyEngine(self.config)
        self._replay_dir = Path(replay_dir) if replay_dir is not None else Path(__file__).resolve().parents[1] / "data" / "replays"
        self._run_id = datetime.now().strftime("run_%Y%m%d_%H%M%S")
        now = self._time_fn()
        self.state = SimulationState(
            channels=[
                ChannelState(
                    channel=config.channel,
                    name=config.name,
                    estimatedTransmittance=estimated_transmittance(0.95),
                )
                for config in CHANNEL_CONFIGS
            ],
            timestamp=now,
        )
        self._manual_mi: dict[int, float] = {}
        self._frame_id = 0
        self._flashlight_angle = 0.0
        self._replay: list[dict[str, Any]] = []

    def step(self, dt: float = 0.1) -> None:
        with self._lock:
            now = self._time_fn()
            self._advance_demo_inputs(dt)
            self._expire_manual_overrides(now)
            result = self._policy.compute(
                vehicle_mode=self.state.vehicleMode,
                demo_mode=self.state.demoMode,
                environment=self.state.environment,
                channels=self.state.channels,
                now=now,
            )

            for channel in self.state.channels:
                target = self._manual_mi.get(channel.channel, result.targets[channel.channel])
                channel.targetMi = round(clamp(target), 3)
                self._servo_channel(channel, dt, channel.channel in result.fast_attack_channels)
                channel.estimatedTransmittance = estimated_transmittance(channel.appliedMi)
                channel.opticalState = optical_state(channel.appliedMi)

            self._frame_id += 1
            self.state.cameraMetrics = self._camera_metrics(now)
            reason = self._manual_reason_suffix(result.reason, now)
            self.state.decisionReason = self._fault_reason_suffix(reason)
            self.state.timestamp = now
            self._record_snapshot()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            full = self.state.to_dict()
            full["fast"] = {
                "schemaVersion": self.state.schemaVersion,
                "vehicleMode": self.state.vehicleMode,
                "demoMode": self.state.demoMode,
                "channels": [channel.to_dict() for channel in self.state.channels],
                "timestamp": self.state.timestamp,
            }
            return full

    def replay_events(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._replay)

    def apply_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            command = payload.get("type")
            if command == "setManualChannel":
                channel = self._channel_id(payload["channel"])
                if self.state.channels[channel].fault:
                    return {
                        "ok": False,
                        "type": command,
                        "channel": channel,
                        "error": f"CH{channel} is faulted; manual control is disabled",
                    }
                mi = clamp(float(payload["mi"]))
                ttl = max(0.0, float(payload.get("ttlSeconds", 15.0)))
                self._manual_mi[channel] = mi
                self.state.channels[channel].manualUntil = self._time_fn() + ttl
                return {"ok": True, "type": command, "channel": channel, "mi": mi}

            if command == "returnAuto":
                channel = payload.get("channel")
                if channel is None:
                    self._manual_mi.clear()
                    for state in self.state.channels:
                        state.manualUntil = None
                else:
                    channel_id = self._channel_id(channel)
                    self._manual_mi.pop(channel_id, None)
                    self.state.channels[channel_id].manualUntil = None
                return {"ok": True, "type": command}

            if command == "setChannelFault":
                channel = self._channel_id(payload["channel"])
                fault = bool(payload.get("fault", True))
                self.state.channels[channel].fault = fault
                if fault:
                    self._manual_mi.pop(channel, None)
                    self.state.channels[channel].manualUntil = None
                return {"ok": True, "type": command, "channel": channel, "fault": fault}

            if command == "setScenario":
                demo_mode = str(payload.get("demoMode", "none"))
                self._set_scenario(demo_mode)
                return {"ok": True, "type": command, "demoMode": self.state.demoMode}

            if command == "setEnvironment":
                updates = payload.get("environment", {})
                for key, value in updates.items():
                    if hasattr(self.state.environment, key):
                        setattr(self.state.environment, key, None if value is None else float(value))
                return {"ok": True, "type": command}

            if command == "setFlashlightAngle":
                angle = float(payload.get("angleDeg", self._flashlight_angle)) % 360.0
                self._flashlight_angle = angle
                if self.state.demoMode != "flashlight_360":
                    self.state.demoMode = "flashlight_360"
                    self.state.vehicleMode = self._vehicle_mode_for_demo(self.state.demoMode)
                    self._manual_mi.clear()
                    for channel in self.state.channels:
                        channel.manualUntil = None
                self._apply_flashlight_vector()
                return {"ok": True, "type": command, "angleDeg": round(angle, 1)}

            if command == "resetFault":
                for channel in self.state.channels:
                    channel.fault = False
                return {"ok": True, "type": command}

            if command == "saveReplay":
                path = self.save_replay(str(payload.get("name") or self._run_id))
                return {"ok": True, "type": command, "path": str(path)}

            if command == "loadReplay":
                events = self.load_replay(str(payload["name"]))
                return {"ok": True, "type": command, "events": len(events)}

            return {"ok": False, "error": f"unknown command: {command}"}

    def save_replay(self, name: str | None = None) -> Path:
        with self._lock:
            safe_name = self._safe_replay_name(name or self._run_id)
            self._replay_dir.mkdir(parents=True, exist_ok=True)
            path = self._replay_dir / f"{safe_name}.jsonl"
            path.write_text(
                "".join(f"{self._json_line(event)}\n" for event in self._replay),
                encoding="utf-8",
            )
            return path

    def load_replay(self, name: str) -> list[dict[str, Any]]:
        with self._lock:
            path = self._replay_path(name)
            events: list[dict[str, Any]] = []
            for line in path.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    events.append(self._json_loads(line))
            self._replay = events[-600:]
            if events:
                self._apply_replay_event(events[-1])
            return list(self._replay)

    def replay_files(self) -> list[dict[str, Any]]:
        self._replay_dir.mkdir(parents=True, exist_ok=True)
        files = []
        for path in sorted(self._replay_dir.glob("*.jsonl"), reverse=True):
            files.append({"name": path.stem, "path": str(path), "bytes": path.stat().st_size})
        return files

    def _servo_channel(self, channel: ChannelState, dt: float, fast_attack: bool) -> None:
        if channel.fault:
            channel.appliedMi = 0.0
            return
        delta = channel.targetMi - channel.appliedMi
        if abs(delta) < cfg_float(self.config, "servo", "snap_epsilon"):
            channel.appliedMi = channel.targetMi
            return

        moving_to_frost = delta < 0
        if fast_attack and moving_to_frost:
            max_delta = cfg_float(self.config, "servo", "fast_attack_rate") * dt
        elif moving_to_frost:
            max_delta = cfg_float(self.config, "servo", "frost_rate") * dt
        else:
            max_delta = cfg_float(self.config, "servo", "clear_rate") * dt
        channel.appliedMi = round(channel.appliedMi + math.copysign(min(abs(delta), max_delta), delta), 3)

    def _camera_metrics(self, now: float) -> CameraMetrics:
        env = self.state.environment
        front_mi = (self.state.channels[0].appliedMi + self.state.channels[1].appliedMi) / 2.0
        dimming_effect = clamp((0.95 - front_mi) / 0.75)
        left_sat = clamp(env.frontLeftSaturation * (1.0 - cfg_float(self.config, "camera", "dimming_gain") * dimming_effect))
        right_sat = clamp(env.frontRightSaturation * (1.0 - cfg_float(self.config, "camera", "dimming_gain") * dimming_effect))
        edge = clamp(env.edgeDensity - cfg_float(self.config, "camera", "edge_loss_gain") * dimming_effect)
        glare = clamp(
            max(left_sat, right_sat)
            + max(0.0, (env.frontLux or 0.0) - cfg_float(self.config, "camera", "glare_front_lux_base"))
            / cfg_float(self.config, "camera", "glare_front_lux_span")
        )
        return CameraMetrics(
            frontLeftSaturation=round(left_sat, 3),
            frontRightSaturation=round(right_sat, 3),
            edgeDensity=round(edge, 3),
            glare=round(glare, 3),
            frameId=self._frame_id,
            timestamp=now,
        )

    def _expire_manual_overrides(self, now: float) -> None:
        expired = [
            channel.channel
            for channel in self.state.channels
            if channel.manualUntil is not None and channel.manualUntil <= now
        ]
        for channel_id in expired:
            self._manual_mi.pop(channel_id, None)
            self.state.channels[channel_id].manualUntil = None

    def _manual_reason_suffix(self, reason: str, now: float) -> str:
        active = [
            f"CH{channel.channel} {max(0.0, (channel.manualUntil or now) - now):.0f}초 후 자동 복귀"
            for channel in self.state.channels
            if channel.manualUntil is not None
        ]
        if not active:
            return reason
        return f"{reason} 수동 오버라이드 적용 중: {', '.join(active)}."

    def _fault_reason_suffix(self, reason: str) -> str:
        faulted = [f"CH{channel.channel}" for channel in self.state.channels if channel.fault]
        if not faulted:
            return reason
        return f"{reason} 구동기 고장 {', '.join(faulted)}은 fail-safe 산란 상태입니다."

    def _set_scenario(self, demo_mode: str) -> None:
        allowed: set[DemoMode] = {
            "none",
            "hot_summer",
            "camping",
            "parked",
            "camera_saturation",
            "flashlight_360",
        }
        if demo_mode not in allowed:
            demo_mode = "none"
        self.state.demoMode = demo_mode  # type: ignore[assignment]
        self.state.vehicleMode = self._vehicle_mode_for_demo(self.state.demoMode)
        if self.state.demoMode == "none":
            self.state.environment = EnvironmentInput()
        elif self.state.demoMode == "hot_summer":
            self.state.environment = EnvironmentInput(
                frontLux=760.0,
                rightLux=620.0,
                rearLux=540.0,
                leftLux=600.0,
                topLux=980.0,
                internalTemp=39.0,
                weatherTemp=36.0,
                frontLeftSaturation=0.18,
                frontRightSaturation=0.16,
                edgeDensity=0.84,
            )
        elif self.state.demoMode == "camping":
            self.state.environment = EnvironmentInput(
                frontLux=80.0,
                rightLux=90.0,
                rearLux=70.0,
                leftLux=85.0,
                topLux=50.0,
                internalTemp=24.0,
                weatherTemp=22.0,
                edgeDensity=0.72,
            )
        elif self.state.demoMode == "parked":
            self.state.environment = EnvironmentInput(
                frontLux=500.0,
                rightLux=460.0,
                rearLux=410.0,
                leftLux=420.0,
                topLux=760.0,
                internalTemp=33.0,
                weatherTemp=32.0,
                edgeDensity=0.76,
            )
        elif self.state.demoMode == "camera_saturation":
            self.state.environment = EnvironmentInput(
                frontLux=1180.0,
                rightLux=220.0,
                rearLux=120.0,
                leftLux=340.0,
                topLux=240.0,
                internalTemp=28.0,
                weatherTemp=29.0,
                frontLeftSaturation=0.90,
                frontRightSaturation=0.36,
                edgeDensity=0.83,
            )
        elif self.state.demoMode == "flashlight_360":
            self._flashlight_angle = 0.0
            self._apply_flashlight_vector()
        self._manual_mi.clear()
        for channel in self.state.channels:
            channel.manualUntil = None

    def _vehicle_mode_for_demo(self, demo_mode: DemoMode) -> VehicleMode:
        if demo_mode == "camping":
            return "camping"
        if demo_mode == "parked":
            return "parked"
        if demo_mode == "flashlight_360":
            return "stopped"
        return "driving"

    def _advance_demo_inputs(self, dt: float) -> None:
        del dt
        if self.state.demoMode != "flashlight_360":
            return

    def _apply_flashlight_vector(self) -> None:
        angle = math.radians(self._flashlight_angle)
        base = cfg_float(self.config, "flashlight", "base_lux")
        strength = cfg_float(self.config, "flashlight", "strength_lux")
        front = base + strength * max(0.0, math.cos(angle))
        right = base + strength * max(0.0, math.sin(angle))
        rear = base + strength * max(0.0, -math.cos(angle))
        left = base + strength * max(0.0, -math.sin(angle))
        self.state.environment = EnvironmentInput(
            frontLux=round(front, 1),
            rightLux=round(right, 1),
            rearLux=round(rear, 1),
            leftLux=round(left, 1),
            topLux=210.0,
            internalTemp=26.0,
            weatherTemp=27.0,
            frontLeftSaturation=clamp((front - 650.0) / 720.0),
            frontRightSaturation=clamp((front - 760.0) / 760.0),
            edgeDensity=0.86,
        )

    def _record_snapshot(self) -> None:
        if self._frame_id % 5 != 0:
            return
        self._replay.append(
            {
                "timestamp": self.state.timestamp,
                "vehicleMode": self.state.vehicleMode,
                "demoMode": self.state.demoMode,
                "environment": self.state.environment.to_dict(),
                "channels": [channel.to_dict() for channel in self.state.channels],
                "decisionReason": self.state.decisionReason,
            }
        )
        if len(self._replay) > 600:
            self._replay = self._replay[-600:]

    def _apply_replay_event(self, event: dict[str, Any]) -> None:
        self.state.vehicleMode = event.get("vehicleMode", self.state.vehicleMode)
        self.state.demoMode = event.get("demoMode", self.state.demoMode)
        env = event.get("environment", {})
        for key, value in env.items():
            if hasattr(self.state.environment, key):
                setattr(self.state.environment, key, value)
        channels = event.get("channels", [])
        for channel_payload in channels:
            channel_id = int(channel_payload["channel"])
            if 0 <= channel_id < len(self.state.channels):
                channel = self.state.channels[channel_id]
                for key in ("targetMi", "appliedMi", "estimatedTransmittance", "opticalState", "fault", "manualUntil"):
                    if key in channel_payload:
                        setattr(channel, key, channel_payload[key])
        self.state.decisionReason = event.get("decisionReason", self.state.decisionReason)
        self.state.timestamp = float(event.get("timestamp", self.state.timestamp))

    def _safe_replay_name(self, name: str) -> str:
        safe = "".join(char if char.isalnum() or char in ("-", "_") else "_" for char in name.strip())
        return safe or self._run_id

    def _channel_id(self, value: Any) -> int:
        channel = int(value)
        if not 0 <= channel < len(self.state.channels):
            raise ValueError(f"Channel must be between 0 and {len(self.state.channels) - 1}: {channel}")
        return channel

    def _replay_path(self, name: str) -> Path:
        safe_name = self._safe_replay_name(name)
        path = self._replay_dir / f"{safe_name}.jsonl"
        if not path.exists():
            raise FileNotFoundError(f"Replay does not exist: {safe_name}")
        return path

    def _json_line(self, event: dict[str, Any]) -> str:
        import json

        return json.dumps(event, ensure_ascii=False, separators=(",", ":"))

    def _json_loads(self, line: str) -> dict[str, Any]:
        import json

        value = json.loads(line)
        if not isinstance(value, dict):
            raise ValueError("Replay line must be a JSON object")
        return value
