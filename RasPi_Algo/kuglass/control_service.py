from __future__ import annotations

import argparse
import signal
import time

from .camera_service import make_camera_service
from .command_queue import CommandQueue
from .config import load_config
from .directional_lux_service import make_lux_service
from .front_glare_estimator import FrontGlareEstimator
from .logger import CSVLogWriter
from .lux_vector_fusion import LuxVectorFusion
from .mi_servo import MIServo
from .mode_manager import ModeManager
from .models import ChannelTarget, ControlInputs, DemoMode, PolicyDecision
from .policy_engine import PolicyEngine
from .sensor_service import make_temperature_service
from .serial_gateway import make_serial_gateway
from .state_store import StateStore
from .weather_cache import make_weather_cache


class ControlService:
    def __init__(self, config: dict) -> None:
        self.config = config
        self.camera = make_camera_service(config)
        self.lux = make_lux_service(config)
        self.temperature = make_temperature_service(config)
        self.weather = make_weather_cache(config)
        self.glare = FrontGlareEstimator(float(config.get("policy", {}).get("front_strong_threshold", 0.46)))
        self.fusion = LuxVectorFusion()
        self.policy = PolicyEngine()
        self.servo = MIServo()
        runtime = config.get("runtime", {})
        self.state_store = StateStore(runtime.get("state_path", "/tmp/kuglass_state.json"))
        self.command_queue = CommandQueue(runtime.get("command_queue_path", "/tmp/kuglass_commands.jsonl"))
        self.serial = make_serial_gateway(config)
        self.mode = ModeManager()
        self.logger = CSVLogWriter(runtime.get("log_dir", "logs"))
        self.loop_hz = float(runtime.get("loop_hz", 20))
        self._running = True

    def stop(self, *_args: object) -> None:
        self._running = False

    def run(self, iterations: int | None = None) -> None:
        period = 1.0 / max(self.loop_hz, 1.0)
        count = 0
        while self._running:
            start = time.monotonic()
            self.step()
            count += 1
            if iterations is not None and count >= iterations:
                break
            elapsed = time.monotonic() - start
            time.sleep(max(0.0, period - elapsed))
        self.logger.close()

    def step(self) -> PolicyDecision:
        for payload in self.command_queue.pop_all():
            try:
                self.mode.apply_payload(payload)
            except Exception:
                continue

        front = self.camera.capture_front()
        rear = self.camera.capture_rear()
        glare = self.glare.estimate(front)
        lux = self.lux.read_vector()
        fused = self.fusion.fuse(lux, glare, rear, self.mode.demo_mode == DemoMode.FLASHLIGHT_360)
        weather = self.weather.get()
        internal_temp = self.temperature.read_c()
        inputs = ControlInputs(
            front,
            rear,
            glare,
            lux,
            fused,
            weather,
            internal_temp,
            self.mode.vehicle_mode,
            self.mode.demo_mode,
            self.mode.active_manuals(),
        )

        decision = self.policy.decide(inputs)
        decision = self._apply_manuals(decision, inputs)
        decision = self.servo.update(decision)
        self.serial.send_targets(decision.seq, decision.targets)
        telemetry = self.serial.read_telemetry()
        self.logger.write_decision(inputs, decision)
        self.state_store.write({"inputs": inputs, "decision": decision, "telemetry": telemetry})
        return decision

    def _apply_manuals(self, decision: PolicyDecision, inputs: ControlInputs) -> PolicyDecision:
        if not inputs.manual_overrides:
            return decision
        targets: list[ChannelTarget] = []
        for target in decision.targets:
            override = inputs.manual_overrides.get(target.channel_id)
            if override is None:
                targets.append(target)
            else:
                targets.append(
                    ChannelTarget(
                        target.channel_id,
                        target.target_transmission,
                        override.target_mi,
                        override.enable,
                        target.optical_state,
                        target.score,
                        "manual override TTL",
                    )
                )
        return PolicyDecision(
            decision.seq,
            targets,
            decision.thermal_risk,
            decision.privacy_need,
            decision.strong_front_light,
            decision.mode,
            decision.demo_mode,
            decision.timestamp_ms,
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="config/config.yaml")
    parser.add_argument("--mock", action="store_true")
    parser.add_argument("--iterations", type=int)
    args = parser.parse_args()
    config = load_config(args.config)
    if args.mock:
        config.setdefault("runtime", {})["mode"] = "mock"
        config.setdefault("camera", {})["source"] = "mock"
        config.setdefault("sensors", {})["source"] = "mock"
        config.setdefault("weather", {})["source"] = "mock"
        config.setdefault("serial", {})["enabled"] = False
    service = ControlService(config)
    signal.signal(signal.SIGTERM, service.stop)
    signal.signal(signal.SIGINT, service.stop)
    service.run(args.iterations)


if __name__ == "__main__":
    main()

