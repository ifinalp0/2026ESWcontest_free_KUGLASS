from __future__ import annotations

import os
import sys
import unittest
from unittest.mock import patch

from server import parse_args


class ServerDefaultsTests(unittest.TestCase):
    def test_live_defaults_to_macos_usb_auto_detection(self) -> None:
        with patch.dict(os.environ, {}, clear=True), patch.object(sys, "argv", ["server.py"]):
            args = parse_args()

        self.assertEqual(args.transport, "usb")
        self.assertEqual(args.usb_port, "auto")
        self.assertEqual(args.port, 8080)


if __name__ == "__main__":
    unittest.main()
