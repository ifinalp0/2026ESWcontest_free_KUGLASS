from __future__ import annotations

import json
import time
import unittest
from unittest.mock import patch

from backend.gateway import ESP32AGateway
from backend.protocol import CommandError
from backend.transport import MockTransport


class GatewayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.gateway = ESP32AGateway(transport=MockTransport())
        self.gateway.start()

    def tearDown(self) -> None:
        self.gateway.close()

    def wait_for(self, predicate, timeout: float = 1.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.01)
        self.fail("condition was not reached")

    def test_mock_transport_updates_ui_state_after_high_level_command(self) -> None:
        self.wait_for(lambda: self.gateway.link_snapshot()["hardwareConnected"])
        sequences = self.gateway.submit({"type": "setScenario", "demoMode": "hot_summer"})
        self.assertEqual(len(sequences), 2)
        self.wait_for(lambda: self.gateway.snapshot()["demoMode"] == "hot_summer")
        state = self.gateway.snapshot()
        self.assertEqual(state["link"]["transport"], "mock")
        self.assertTrue(state["link"]["hardwareConnected"])
        self.assertTrue(state["link"]["downstreamHealthy"])
        self.assertEqual(state["link"]["downstreamError"], "NONE")
        self.assertEqual(state["link"]["downstreamBootId"], 1001)
        self.assertEqual(state["link"]["downstreamResetChallenge"], 2001)
        self.assertFalse(state["link"]["downstreamOperationalFault"])
        self.assertEqual(state["link"]["downstreamAdc"]["channels"][0]["currentMv"], 28)
        self.assertLess(state["channels"][3]["targetMi"], state["channels"][0]["targetMi"])

    def test_mock_allows_hil_only_command(self) -> None:
        self.gateway.submit({"type": "setEnvironment", "environment": {"internalTemp": 40}})
        self.wait_for(lambda: self.gateway.snapshot()["environment"]["internalTemp"] == 40)

    def test_camera_stream_request_is_tracked_without_changing_policy(self) -> None:
        before = [channel["targetMi"] for channel in self.gateway.snapshot()["channels"]]
        [sequence] = self.gateway.submit({"type": "setCameraStream", "enabled": True})
        self.wait_for(lambda: self.gateway.link_snapshot()["lastAckSeq"] == sequence)
        self.assertTrue(self.gateway.camera_status()["requested"])
        self.assertEqual(
            [channel["targetMi"] for channel in self.gateway.snapshot()["channels"]],
            before,
        )

    def test_mock_manual_disable_is_reported_and_drives_applied_mi_to_zero(self) -> None:
        self.gateway.submit({
            "type": "setManualChannel",
            "channel": 1,
            "mi": 0.6,
            "enable": False,
            "ttlSeconds": 30,
        })
        self.wait_for(lambda: self.gateway.snapshot()["channels"][1]["commandedEnableKnown"])
        self.wait_for(lambda: not self.gateway.snapshot()["channels"][1]["commandedEnable"])
        self.wait_for(lambda: self.gateway.snapshot()["channels"][1]["appliedMi"] == 0.0)
        channel = self.gateway.snapshot()["channels"][1]
        self.assertEqual(channel["targetMi"], 0.0)
        self.assertIsNotNone(channel["manualUntil"])

    def test_mock_persistent_manual_has_no_expiration(self) -> None:
        self.gateway.submit({
            "type": "setManualChannel",
            "channel": 2,
            "mi": 0.35,
            "persistent": True,
        })
        self.wait_for(lambda: self.gateway.snapshot()["channels"][2]["manualPersistent"])
        channel = self.gateway.snapshot()["channels"][2]
        self.assertIsNone(channel["manualUntil"])
        self.assertEqual(channel["targetMi"], 0.35)

        self.gateway.submit({"type": "returnAuto", "channel": 2})
        self.wait_for(lambda: not self.gateway.snapshot()["channels"][2]["manualPersistent"])

    def test_backend_runtime_can_stop_and_start_without_restarting_http_owner(self) -> None:
        self.wait_for(lambda: self.gateway.link_snapshot()["hardwareConnected"])
        self.assertTrue(self.gateway.link_snapshot()["backendRunning"])

        self.assertTrue(self.gateway.stop())
        stopped = self.gateway.link_snapshot()
        self.assertFalse(stopped["backendRunning"])
        self.assertFalse(stopped["hardwareConnected"])
        self.assertEqual(stopped["downstreamError"], "BACKEND_STOPPED")
        with self.assertRaises(CommandError) as raised:
            self.gateway.submit({"type": "returnAuto"})
        self.assertEqual(raised.exception.status, 503)
        self.assertFalse(self.gateway.stop())

        self.assertTrue(self.gateway.start())
        self.assertFalse(self.gateway.start())
        self.wait_for(lambda: self.gateway.link_snapshot()["hardwareConnected"])
        self.assertTrue(self.gateway.link_snapshot()["backendRunning"])


class DisconnectedTransport:
    mode = "usb"
    port = "/dev/missing"
    connected = False
    error = "missing"

    def write_line(self, _line: str) -> None:
        raise OSError("missing")

    def read_lines(self) -> list[str]:
        return []

    def close(self) -> None:
        return


class LiveBoundaryTests(unittest.TestCase):
    def test_disconnected_live_command_is_not_queued(self) -> None:
        gateway = ESP32AGateway(transport=DisconnectedTransport())
        with self.assertRaises(CommandError) as raised:
            gateway.submit({"type": "setScenario", "demoMode": "none"})
        self.assertEqual(raised.exception.status, 503)

    def test_open_usb_requires_fresh_telemetry(self) -> None:
        transport = RecordingTransport(mode="usb")
        gateway = ESP32AGateway(transport=transport, telemetry_timeout_seconds=0.03)
        with self.assertRaises(CommandError):
            gateway.submit({"type": "returnAuto"})

        transport.incoming.append('{"type":"state","channels":[]}')
        gateway._read_transport()
        self.assertEqual(len(gateway.submit({"type": "returnAuto"})), 1)

        time.sleep(0.04)
        with self.assertRaises(CommandError):
            gateway.submit({"type": "returnAuto"})

    def test_malformed_b_status_does_not_refresh_hardware_connection(self) -> None:
        transport = RecordingTransport(mode="usb")
        gateway = ESP32AGateway(transport=transport)
        transport.incoming.append(
            '{"v":1,"type":"status","controller_id":"B","seq":1,"ch":[]}'
        )
        gateway._read_transport()
        self.assertFalse(gateway.link_snapshot()["hardwareConnected"])

    def test_mock_reset_exposes_correlated_b_result_and_final_ack(self) -> None:
        gateway = ESP32AGateway(transport=MockTransport(), sequence_seed=700)
        gateway.start()
        try:
            [sequence] = gateway.submit({"type": "resetFault"})
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                link = gateway.link_snapshot()
                result = link["downstreamControlResult"]
                if result is not None and link["lastAckSeq"] == sequence:
                    break
                time.sleep(0.01)
            else:
                self.fail("correlated reset result was not observed")
            self.assertEqual(result["seq"], sequence)
            self.assertEqual(result["command"], "reset_fault")
            self.assertTrue(result["ok"])
            self.assertEqual(link["lastAckCommand"], "reset_fault")
            self.assertTrue(link["lastAckOk"])
        finally:
            gateway.close()


class RecordingTransport:
    def __init__(self, mode: str = "mock") -> None:
        self.mode = mode
        self.port = None if mode == "mock" else "/dev/fake"
        self.connected = True
        self.error = None
        self.incoming: list[str] = []
        self.written: list[tuple[float, str]] = []
        self.reconnect_count = 0

    def write_line(self, line: str) -> None:
        self.written.append((time.monotonic(), line))

    def read_lines(self) -> list[str]:
        records = list(self.incoming)
        self.incoming.clear()
        return records

    def close(self) -> None:
        self.connected = False

    def reconnect(self) -> bool:
        self.reconnect_count += 1
        self.connected = True
        self.error = None
        return True


class SequencingAndCoalescingTests(unittest.TestCase):
    def wait_for(self, predicate, timeout: float = 1.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.005)
        self.fail("condition was not reached")

    def test_default_sequence_is_seeded_from_epoch_milliseconds(self) -> None:
        with patch("backend.gateway.time.time", return_value=1234.567):
            gateway = ESP32AGateway(transport=RecordingTransport())
        [sequence] = gateway.submit({"type": "returnAuto"})
        self.assertEqual(sequence, 1234568)
        self.assertNotEqual(sequence, 1)

    def test_controller_reconnect_resets_freshness_until_new_telemetry(self) -> None:
        transport = RecordingTransport(mode="usb")
        gateway = ESP32AGateway(transport=transport)
        transport.incoming.append('{"type":"state","channels":[]}')
        gateway._read_transport()
        self.assertTrue(gateway.link_snapshot()["hardwareConnected"])

        result = gateway.reconnect_controller()

        self.assertEqual(transport.reconnect_count, 1)
        self.assertTrue(result["requested"])
        self.assertTrue(result["portOpen"])
        self.assertFalse(result["link"]["hardwareConnected"])

    def test_controller_reconnect_drops_queued_command_until_new_telemetry(self) -> None:
        transport = RecordingTransport(mode="usb")
        gateway = ESP32AGateway(transport=transport)
        transport.incoming.append('{"type":"state","channels":[]}')
        gateway._read_transport()
        gateway.submit({"type": "returnAuto"})

        gateway.reconnect_controller()
        gateway._flush_outbox()

        self.assertEqual(transport.written, [])

    def test_mock_controller_reconnect_is_rejected(self) -> None:
        gateway = ESP32AGateway(transport=MockTransport())
        with self.assertRaises(CommandError) as raised:
            gateway.reconnect_controller()
        self.assertEqual(raised.exception.status, 409)

    def test_manual_commands_are_latest_wins_and_rate_limited(self) -> None:
        transport = RecordingTransport()
        gateway = ESP32AGateway(
            transport=transport,
            sequence_seed=100,
            manual_command_interval_seconds=0.075,
        )
        gateway.start()
        try:
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.6})
            self.wait_for(lambda: len(transport.written) == 1)
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.5})
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.2})
            self.wait_for(lambda: len(transport.written) == 2)
            first_at, first_line = transport.written[0]
            second_at, second_line = transport.written[1]
            self.assertGreaterEqual(second_at - first_at, 0.05)
            self.assertEqual(json.loads(first_line)["target_mi"], 0.6)
            self.assertEqual(json.loads(second_line)["target_mi"], 0.2)
            self.assertEqual(json.loads(second_line)["seq"], 103)
        finally:
            gateway.close()


if __name__ == "__main__":
    unittest.main()
