from __future__ import annotations

import unittest

from backend.state import StateStore


class StateStoreTests(unittest.TestCase):
    def test_full_snake_case_state_is_normalized_for_simul_ui(self) -> None:
        store = StateStore()
        accepted = store.apply_record({
            "type": "state",
            "vehicle_mode": "camping",
            "demo_mode": "camping",
            "environment": {"internal_temp_c": 29.5},
            "camera_metrics": {"front_left_saturation": 0.4, "frame_id": 12},
            "channels": [{
                "channel_id": 0,
                "target_mi": 0.2,
                "commanded_mi": 0.25,
                "applied_mi": 0.3,
                "applied_source": "master_servo_command",
                "fault": False,
            }],
            "decision_reason": "ESP32_A policy",
            "timestamp_ms": 1234000,
        })
        self.assertTrue(accepted)
        state = store.snapshot()
        self.assertEqual(state["vehicleMode"], "camping")
        self.assertEqual(state["environment"]["internalTemp"], 29.5)
        self.assertEqual(state["cameraMetrics"]["frameId"], 12)
        self.assertEqual(state["channels"][0]["targetMi"], 0.2)
        self.assertEqual(state["channels"][0]["commandedMi"], 0.25)
        self.assertEqual(state["channels"][0]["appliedMi"], 0.0)
        self.assertFalse(state["channels"][0]["appliedKnown"])
        self.assertEqual(state["channels"][0]["opticalState"], "FROST")
        self.assertEqual(state["decisionReason"], "ESP32_A policy")
        self.assertEqual(state["timestamp"], 1234.0)

    def test_downstream_status_channel_telemetry_is_merged(self) -> None:
        store = StateStore()
        store.apply_record({
            "type": "status",
            "controller_id": "B",
            "seq": 9,
            "ch": [{"id": 3, "mi": 0.62, "target_mi": 0.55, "fault": True}],
        })
        channel = store.snapshot()["channels"][3]
        self.assertEqual(channel["appliedMi"], 0.62)
        self.assertTrue(channel["appliedKnown"])
        self.assertEqual(channel["targetMi"], 0.0)
        self.assertTrue(channel["fault"])

    def test_camera_metrics_do_not_overwrite_raw_environment_evidence(self) -> None:
        store = StateStore()
        store.apply_record({
            "type": "state",
            "environment": {
                "front_left_saturation": 0.82,
                "front_right_saturation": 0.71,
                "edge_density": 0.64,
            },
            "camera_metrics": {
                "front_left_saturation": 0.43,
                "front_right_saturation": 0.38,
                "edge_density": 0.29,
            },
        })

        state = store.snapshot()
        self.assertEqual(state["environment"]["frontLeftSaturation"], 0.82)
        self.assertEqual(state["environment"]["frontRightSaturation"], 0.71)
        self.assertEqual(state["environment"]["edgeDensity"], 0.64)
        self.assertEqual(state["cameraMetrics"]["frontLeftSaturation"], 0.43)
        self.assertEqual(state["cameraMetrics"]["frontRightSaturation"], 0.38)
        self.assertEqual(state["cameraMetrics"]["edgeDensity"], 0.29)

    def test_only_downstream_status_can_update_applied_mi(self) -> None:
        store = StateStore()
        store.apply_record({
            "type": "state",
            "channels": [{"channel_id": 1, "target_mi": 0.7, "commanded_mi": 0.6, "applied_mi": 0.55}],
        })
        self.assertEqual(store.snapshot()["channels"][1]["appliedMi"], 0.0)

        store.apply_record({
            "type": "status",
            "controller_id": "A",
            "ch": [{"id": 1, "mi": 0.52}],
        })
        self.assertEqual(store.snapshot()["channels"][1]["appliedMi"], 0.0)

        store.apply_record({
            "type": "status",
            "controller_id": "B",
            "ch": [{"id": 1, "mi": 0.48}],
        })
        self.assertEqual(store.snapshot()["channels"][1]["appliedMi"], 0.48)

    def test_downstream_fault_latch_is_not_overwritten_by_master_state(self) -> None:
        store = StateStore()
        store.apply_record({
            "type": "status",
            "controller_id": "B",
            "ch": [{"id": 1, "mi": 0.0, "fault": True}],
        })
        store.apply_record({
            "type": "state",
            "channels": [{"channel_id": 1, "commanded_mi": 0.8, "fault": False}],
        })
        self.assertTrue(store.snapshot()["channels"][1]["fault"])

        store.apply_record({
            "type": "status",
            "controller_id": "B",
            "ch": [{"id": 1, "mi": 0.0}],
        })
        self.assertTrue(store.snapshot()["channels"][1]["fault"])

        store.apply_record({
            "type": "status",
            "controller_id": "B",
            "ch": [{"id": 1, "mi": 0.0, "fault": False}],
        })
        self.assertFalse(store.snapshot()["channels"][1]["fault"])

    def test_manual_remaining_ms_becomes_epoch_deadline_and_zero_clears(self) -> None:
        store = StateStore(time_fn=lambda: 1000.5)
        store.apply_record({
            "type": "state",
            "channels": [{
                "channel_id": 2,
                "target_mi": 0.42,
                "applied_mi": 0.44,
                "manual_remaining_ms": 24500,
            }],
            "downstream": {"controller_id": "B", "healthy": False, "error": "WRITE_FAILED"},
        })
        self.assertAlmostEqual(store.snapshot()["channels"][2]["manualUntil"], 1025.0)
        self.assertFalse(store.downstream_healthy)
        self.assertEqual(store.downstream_error, "WRITE_FAILED")

        store.apply_record({
            "type": "state",
            "channels": [{"channel_id": 2, "manual_remaining_ms": 0}],
        })
        self.assertIsNone(store.snapshot()["channels"][2]["manualUntil"])


if __name__ == "__main__":
    unittest.main()
