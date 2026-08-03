from __future__ import annotations

import unittest

from backend.protocol import CommandError, translate_ui_command


class Sequence:
    def __init__(self) -> None:
        self.value = 0

    def next(self) -> int:
        self.value += 1
        return self.value


class ProtocolTests(unittest.TestCase):
    def test_scenario_becomes_mode_then_demo(self) -> None:
        sequence = Sequence()
        messages = translate_ui_command(
            {"type": "setScenario", "demoMode": "camping"},
            sequence.next,
            diagnostics_enabled=False,
        )
        self.assertEqual(messages, [
            {"v": 1, "type": "ui_command", "seq": 1, "command": "set_mode", "mode": "camping"},
            {"v": 1, "type": "ui_command", "seq": 2, "command": "set_demo", "demo_mode": "camping"},
        ])

    def test_manual_command_uses_snake_case_and_milliseconds(self) -> None:
        sequence = Sequence()
        [message] = translate_ui_command(
            {"type": "setManualChannel", "channel": 3, "mi": 0.12345, "ttlSeconds": 30},
            sequence.next,
            diagnostics_enabled=False,
        )
        self.assertEqual(message["v"], 1)
        self.assertEqual(message["command"], "manual_channel")
        self.assertEqual(message["channel_id"], 3)
        self.assertEqual(message["target_mi"], 0.1235)
        self.assertEqual(message["ttl_ms"], 30000)
        self.assertNotIn("ch", message)

    def test_live_sensor_override_is_rejected(self) -> None:
        with self.assertRaises(CommandError) as raised:
            translate_ui_command(
                {"type": "setEnvironment", "environment": {"internalTemp": 35}},
                Sequence().next,
                diagnostics_enabled=False,
            )
        self.assertEqual(raised.exception.status, 409)

    def test_invalid_channel_is_rejected(self) -> None:
        with self.assertRaises(CommandError):
            translate_ui_command(
                {"type": "setManualChannel", "channel": 4, "mi": 0.5},
                Sequence().next,
                diagnostics_enabled=False,
            )


if __name__ == "__main__":
    unittest.main()
