from __future__ import annotations

from dataclasses import dataclass, field

from .models import ChannelTarget, PolicyDecision
from .utils import clamp, now_ms


@dataclass
class ServoConfig:
    attack_rate_per_s: float = 1.6
    release_rate_per_s: float = 0.42
    low_pass_alpha: float = 0.35
    fast_attack_front_delta: float = 0.18


@dataclass
class MIServo:
    config: ServoConfig = field(default_factory=ServoConfig)
    _current: dict[int, float] = field(default_factory=dict)
    _last_ms: int | None = None

    def update(self, decision: PolicyDecision, now: int | None = None) -> PolicyDecision:
        current_ms = now if now is not None else now_ms()
        dt = 1.0 / 20.0 if self._last_ms is None else max(0.001, (current_ms - self._last_ms) / 1000.0)
        self._last_ms = current_ms

        adjusted: list[ChannelTarget] = []
        for target in decision.targets:
            previous = self._current.get(target.channel_id, target.target_mi)
            desired = target.target_mi
            fast_attack = (
                decision.strong_front_light
                and target.channel_id in {0, 1}
                and desired < previous - self.config.fast_attack_front_delta
            )
            if fast_attack:
                limited = desired
            elif desired < previous:
                limited = max(desired, previous - self.config.attack_rate_per_s * dt)
            else:
                limited = min(desired, previous + self.config.release_rate_per_s * dt)
            filtered = limited if fast_attack else previous + self.config.low_pass_alpha * (limited - previous)
            filtered = clamp(filtered, 0.0, 1.0)
            self._current[target.channel_id] = filtered
            adjusted.append(
                ChannelTarget(
                    target.channel_id,
                    target.target_transmission,
                    filtered,
                    target.enable,
                    target.optical_state,
                    target.score,
                    target.reason,
                )
            )
        return PolicyDecision(
            decision.seq,
            adjusted,
            decision.thermal_risk,
            decision.privacy_need,
            decision.strong_front_light,
            decision.mode,
            decision.demo_mode,
            decision.timestamp_ms,
        )

