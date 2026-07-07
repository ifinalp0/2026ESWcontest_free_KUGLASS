import json
import unittest

from kuglass.models import ChannelTarget, OpticalState
from kuglass.serial_gateway import build_command_line


class SerialGatewayTest(unittest.TestCase):
    def test_compact_json_command_shape(self):
        line = build_command_line(
            12,
            200,
            [ChannelTarget(0, 0.7, 0.66, True, OpticalState.DIM, 0.2, ""), ChannelTarget(1, 0.9, 0.88, False, OpticalState.CLEAR, 0.0, "")],
        )
        payload = json.loads(line)
        self.assertEqual(payload["seq"], 12)
        self.assertEqual(payload["ttl_ms"], 200)
        self.assertEqual(payload["ch"][0], [0, 0.66, 1])
        self.assertEqual(payload["ch"][1], [1, 0.88, 0])


if __name__ == "__main__":
    unittest.main()

