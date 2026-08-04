from __future__ import annotations

import unittest

from backend.state import StateStore


def b_status(*channel_updates: dict, seq: int = 1, **overrides: object) -> dict:
    channels = [
        {"id": channel, "mi": 0.0, "fault": False}
        for channel in range(4)
    ]
    for update in channel_updates:
        channels[int(update["id"])].update(update)
    record = {
        "v": 1,
        "type": "status",
        "controller_id": "B",
        "seq": seq,
        "boot_id": 101,
        "reset_challenge": 202,
        "estop": False,
        "fault_code": "NONE",
        "ch": channels,
        "adc": {
            "initialized": True,
            "i_cali": True,
            "t_cali": True,
            "raw_valid_mask": 255,
            "mv_valid_mask": 255,
            "i_raw": [100, 101, 102, 103],
            "t_raw": [200, 201, 202, 203],
            "i_mv": [10, 11, 12, 13],
            "t_mv": [1000, 1001, 1002, 1003],
        },
    }
    record.update(overrides)
    return record


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
        store.apply_record(b_status(
            {"id": 3, "mi": 0.62, "target_mi": 0.55, "fault": True},
            seq=9,
        ))
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

        store.apply_record(b_status({"id": 1, "mi": 0.48, "fault": False}))
        self.assertEqual(store.snapshot()["channels"][1]["appliedMi"], 0.48)

    def test_downstream_fault_latch_is_not_overwritten_by_master_state(self) -> None:
        store = StateStore()
        store.apply_record(b_status({"id": 1, "mi": 0.0, "fault": True}))
        store.apply_record({
            "type": "state",
            "channels": [{"channel_id": 1, "commanded_mi": 0.8, "fault": False}],
        })
        self.assertTrue(store.snapshot()["channels"][1]["fault"])

        store.apply_record(b_status({"id": 1, "mi": 0.0, "fault": True}, seq=2))
        self.assertTrue(store.snapshot()["channels"][1]["fault"])

        store.apply_record(b_status({"id": 1, "mi": 0.0, "fault": False}, seq=3))
        self.assertFalse(store.snapshot()["channels"][1]["fault"])

    def test_downstream_diagnostics_adc_masks_and_control_result_are_normalized(self) -> None:
        store = StateStore(time_fn=lambda: 1234.0)
        record = b_status(
            {"id": 2, "mi": 0.0, "fault": True},
            seq=55,
            estop=True,
            fault_code="ESTOP",
            diagnostic="RESET_UNSAFE",
            control_result={
                "command": "reset_fault",
                "seq": 900,
                "source_session_id": 77,
                "ok": False,
                "error": "RESET_UNSAFE",
            },
        )
        record["adc"]["raw_valid_mask"] = 0b00100001
        record["adc"]["mv_valid_mask"] = 0b00000001

        self.assertTrue(store.apply_record(record))
        diagnostics = store.snapshot()["downstreamDiagnostics"]
        self.assertEqual(diagnostics["bootId"], 101)
        self.assertEqual(diagnostics["statusSeq"], 55)
        self.assertTrue(diagnostics["estopActive"])
        self.assertTrue(diagnostics["operationalFault"])
        self.assertEqual(diagnostics["faultCode"], "ESTOP")
        self.assertEqual(diagnostics["controlResult"]["sourceSessionId"], 77)
        self.assertEqual(diagnostics["adc"]["channels"][0]["currentRaw"], 100)
        self.assertIsNone(diagnostics["adc"]["channels"][0]["temperatureRaw"])
        self.assertEqual(diagnostics["adc"]["channels"][1]["temperatureRaw"], 201)
        self.assertIsNone(diagnostics["adc"]["channels"][1]["temperatureMv"])

    def test_malformed_downstream_status_is_rejected_without_mutation(self) -> None:
        store = StateStore()
        malformed = b_status()
        malformed["ch"] = malformed["ch"][:3]
        self.assertFalse(store.apply_record(malformed))
        self.assertIsNone(store.downstream_healthy)
        self.assertIsNone(store.snapshot()["downstreamDiagnostics"]["statusSeq"])

        wrong_identifier = b_status(boot_id=0)
        self.assertFalse(store.apply_record(wrong_identifier))

        wrong_adc = b_status()
        wrong_adc["adc"]["i_mv"] = [1, 2, 3]
        self.assertFalse(store.apply_record(wrong_adc))

        out_of_range_adc = b_status()
        out_of_range_adc["adc"]["i_raw"][0] = 4096
        self.assertFalse(store.apply_record(out_of_range_adc))

        wrong_fault = b_status(fault_code="UNKNOWN_FAULT")
        self.assertFalse(store.apply_record(wrong_fault))

        contradictory_result = b_status(control_result={
            "command": "reset_fault",
            "seq": 1,
            "source_session_id": 9,
            "ok": True,
            "error": "RESET_UNSAFE",
        })
        self.assertFalse(store.apply_record(contradictory_result))

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
