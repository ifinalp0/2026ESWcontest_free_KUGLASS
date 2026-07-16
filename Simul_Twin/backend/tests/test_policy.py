from __future__ import annotations

from pathlib import Path

from simulator.config import load_policy_config
from simulator.engine import SimulationEngine
from simulator.models import CHANNEL_CONFIGS


class FakeClock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


def run_steps(engine: SimulationEngine, count: int = 12, dt: float = 0.1) -> None:
    for _ in range(count):
        engine.step(dt)


def test_hot_summer_prioritizes_roof_rear_before_front() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setScenario", "demoMode": "hot_summer"})
    run_steps(engine)
    channels = {channel["channel"]: channel for channel in engine.snapshot()["channels"]}

    assert channels[7]["targetMi"] < channels[6]["targetMi"]
    assert channels[6]["targetMi"] < channels[4]["targetMi"] + 0.09
    assert channels[4]["targetMi"] < channels[2]["targetMi"]
    assert channels[2]["targetMi"] < channels[0]["targetMi"]


def test_camping_and_parked_force_all_channels_to_frost() -> None:
    engine = SimulationEngine()
    for mode in ("camping", "parked"):
        engine.apply_command({"type": "setScenario", "demoMode": mode})
        run_steps(engine)
        for channel in engine.snapshot()["channels"]:
            assert channel["targetMi"] <= 0.05


def test_front_left_glare_fast_attacks_ch0_but_keeps_driving_floor() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setScenario", "demoMode": "camera_saturation"})
    engine.step(0.1)
    channels = {channel["channel"]: channel for channel in engine.snapshot()["channels"]}

    assert channels[0]["targetMi"] >= 0.58
    assert channels[0]["appliedMi"] < 0.90
    assert channels[0]["appliedMi"] >= 0.58


def test_manual_override_expires_and_returns_to_auto() -> None:
    clock = FakeClock()
    engine = SimulationEngine(time_fn=clock)
    engine.apply_command({"type": "setManualChannel", "channel": 3, "mi": 0.22, "ttlSeconds": 0.5})
    engine.step(0.1)
    assert engine.snapshot()["channels"][3]["targetMi"] == 0.22

    clock.advance(0.6)
    engine.step(0.1)
    assert engine.snapshot()["channels"][3]["manualUntil"] is None
    assert engine.snapshot()["channels"][3]["targetMi"] > 0.80


def test_missing_lux_sensor_values_fall_back_without_crashing() -> None:
    engine = SimulationEngine()
    engine.apply_command(
        {
            "type": "setEnvironment",
            "environment": {
                "frontLux": None,
                "rightLux": 850,
                "rearLux": None,
                "leftLux": 120,
                "topLux": 640,
                "internalTemp": 34,
                "weatherTemp": 35,
            },
        }
    )
    run_steps(engine)
    snapshot = engine.snapshot()

    assert "방향성" in snapshot["decisionReason"] or "열부하" in snapshot["decisionReason"]
    assert len(snapshot["channels"]) == 8


def test_unpaired_lux_sensor_does_not_create_a_false_direction() -> None:
    engine = SimulationEngine()
    engine.apply_command(
        {
            "type": "setEnvironment",
            "environment": {
                "frontLux": None,
                "rightLux": 1200,
                "rearLux": None,
                "leftLux": None,
                "topLux": 0,
                "internalTemp": 20,
                "weatherTemp": 20,
            },
        }
    )
    engine.step(0.1)
    snapshot = engine.snapshot()
    channels = {channel["channel"]: channel for channel in snapshot["channels"]}

    assert "방향성" not in snapshot["decisionReason"]
    assert channels[2]["targetMi"] == channels[3]["targetMi"]
    assert channels[4]["targetMi"] == channels[5]["targetMi"]


def test_channel_fault_uses_fail_safe_frost_and_can_recover() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setChannelFault", "channel": 4, "fault": True})
    engine.step(0.1)

    faulted = engine.snapshot()
    assert faulted["channels"][4]["fault"] is True
    assert faulted["channels"][4]["appliedMi"] == 0.0
    assert "fail-safe" in faulted["decisionReason"]

    engine.apply_command({"type": "setChannelFault", "channel": 4, "fault": False})
    engine.step(0.1)
    recovered = engine.snapshot()

    assert recovered["channels"][4]["fault"] is False
    assert recovered["channels"][4]["appliedMi"] > 0.0


def test_faulted_channel_rejects_manual_override() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setChannelFault", "channel": 2, "fault": True})

    response = engine.apply_command(
        {"type": "setManualChannel", "channel": 2, "mi": 0.8, "ttlSeconds": 30}
    )
    engine.step(0.1)

    assert response["ok"] is False
    assert engine.snapshot()["channels"][2]["manualUntil"] is None
    assert engine.snapshot()["channels"][2]["appliedMi"] == 0.0


def test_flashlight_360_uses_manual_angle_without_auto_rotation() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setScenario", "demoMode": "flashlight_360"})
    engine.apply_command({"type": "setFlashlightAngle", "angleDeg": 90})
    run_steps(engine, count=3, dt=0.5)
    first_snapshot = engine.snapshot()
    first_environment = first_snapshot["environment"]

    assert first_snapshot["vehicleMode"] == "stopped"
    assert first_environment["rightLux"] > first_environment["frontLux"]
    assert first_environment["rightLux"] > first_environment["rearLux"]
    assert first_environment["rightLux"] > first_environment["leftLux"]

    run_steps(engine, count=8, dt=0.5)
    later_environment = engine.snapshot()["environment"]

    assert later_environment["frontLux"] == first_environment["frontLux"]
    assert later_environment["rightLux"] == first_environment["rightLux"]
    assert later_environment["rearLux"] == first_environment["rearLux"]
    assert later_environment["leftLux"] == first_environment["leftLux"]


def test_channel_layout_matches_the_project_channel_manifest() -> None:
    expected = [
        (0, "CH0 전면 좌측", 345.0),
        (1, "CH1 전면 우측", 15.0),
        (2, "CH2 좌측 전방 도어", 285.0),
        (3, "CH3 우측 전방 도어", 75.0),
        (4, "CH4 좌측 후방 도어", 245.0),
        (5, "CH5 우측 후방 도어", 115.0),
        (6, "CH6 후면 유리", 180.0),
        (7, "CH7 선루프", None),
    ]

    assert [
        (config.channel, config.name, config.bearing_deg)
        for config in CHANNEL_CONFIGS
    ] == expected


def test_flashlight_360_reacts_in_canonical_clockwise_channel_order() -> None:
    engine = SimulationEngine()
    engine.apply_command({"type": "setScenario", "demoMode": "flashlight_360"})

    def targets_at(angle: float) -> dict[int, float]:
        engine.apply_command({"type": "setFlashlightAngle", "angleDeg": angle})
        engine.step(0.1)
        return {
            channel["channel"]: channel["targetMi"]
            for channel in engine.snapshot()["channels"]
        }

    front = targets_at(0)
    assert front[0] < front[2]
    assert front[1] < front[3]

    right = targets_at(90)
    assert right[3] < right[2]
    assert right[5] < right[4]

    rear = targets_at(180)
    assert rear[6] < rear[4]
    assert rear[6] < rear[5]

    left = targets_at(270)
    assert left[2] < left[3]
    assert left[4] < left[5]

    seam = targets_at(359)
    assert seam[0] < seam[2]
    assert seam[1] < seam[3]


def test_policy_config_override_changes_camping_target(tmp_path: Path) -> None:
    config_path = tmp_path / "config.yaml"
    config_path.write_text(
        """
policy:
  camping_mi: 0.12
""".strip(),
        encoding="utf-8",
    )
    config = load_policy_config(config_path)
    engine = SimulationEngine(config=config)

    engine.apply_command({"type": "setScenario", "demoMode": "camping"})
    engine.step(0.1)

    assert all(channel["targetMi"] == 0.12 for channel in engine.snapshot()["channels"])


def test_replay_save_and_load_roundtrip(tmp_path: Path) -> None:
    engine = SimulationEngine(replay_dir=tmp_path)
    engine.apply_command({"type": "setScenario", "demoMode": "hot_summer"})
    run_steps(engine, count=8)
    original_last = engine.replay_events()[-1]

    path = engine.save_replay("hot_summer_roundtrip")
    assert path.exists()

    restored = SimulationEngine(replay_dir=tmp_path)
    events = restored.load_replay("hot_summer_roundtrip")

    assert len(events) > 0
    assert restored.snapshot()["demoMode"] == original_last["demoMode"]
    assert restored.snapshot()["channels"][7]["targetMi"] == original_last["channels"][7]["targetMi"]
