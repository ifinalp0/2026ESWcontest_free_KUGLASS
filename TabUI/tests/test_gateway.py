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
        self.assertLess(state["channels"][3]["targetMi"], state["channels"][0]["targetMi"])

    def test_mock_allows_hil_only_command(self) -> None:
        self.gateway.submit({"type": "setEnvironment", "environment": {"internalTemp": 40}})
        self.wait_for(lambda: self.gateway.snapshot()["environment"]["internalTemp"] == 40)


class DisconnectedTransport:
    mode = "serial"
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

    def test_open_serial_requires_fresh_telemetry(self) -> None:
        transport = RecordingTransport(mode="serial")
        gateway = ESP32AGateway(transport=transport, telemetry_timeout_seconds=0.03)
        with self.assertRaises(CommandError):
            gateway.submit({"type": "returnAuto"})

        transport.incoming.append('{"type":"status","ch":[]}')
        gateway._read_transport()
        self.assertEqual(len(gateway.submit({"type": "returnAuto"})), 1)

        time.sleep(0.04)
        with self.assertRaises(CommandError):
            gateway.submit({"type": "returnAuto"})


class RecordingTransport:
    def __init__(self, mode: str = "mock") -> None:
        self.mode = mode
        self.port = None if mode == "mock" else "/dev/fake"
        self.connected = True
        self.error = None
        self.incoming: list[str] = []
        self.written: list[tuple[float, str]] = []

    def write_line(self, line: str) -> None:
        self.written.append((time.monotonic(), line))

    def read_lines(self) -> list[str]:
        records = list(self.incoming)
        self.incoming.clear()
        return records

    def close(self) -> None:
        self.connected = False


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

    def test_manual_commands_are_latest_wins_and_rate_limited(self) -> None:
        transport = RecordingTransport()
        gateway = ESP32AGateway(
            transport=transport,
            sequence_seed=100,
            manual_command_interval_seconds=0.075,
        )
        gateway.start()
        try:
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.8})
            self.wait_for(lambda: len(transport.written) == 1)
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.6})
            gateway.submit({"type": "setManualChannel", "channel": 0, "mi": 0.2})
            self.wait_for(lambda: len(transport.written) == 2)
            first_at, first_line = transport.written[0]
            second_at, second_line = transport.written[1]
            self.assertGreaterEqual(second_at - first_at, 0.05)
            self.assertEqual(json.loads(first_line)["target_mi"], 0.8)
            self.assertEqual(json.loads(second_line)["target_mi"], 0.2)
            self.assertEqual(json.loads(second_line)["seq"], 103)
        finally:
            gateway.close()


if __name__ == "__main__":
    unittest.main()
