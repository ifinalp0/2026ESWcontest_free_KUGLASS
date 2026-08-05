from __future__ import annotations

import unittest
from types import SimpleNamespace
from unittest.mock import patch

from backend.transport import UsbCdcTransport, discover_esp32_usb_ports


class FakeSerial:
    def __init__(self) -> None:
        self.is_open = True
        self.buffer = bytearray()

    @property
    def in_waiting(self) -> int:
        return len(self.buffer)

    def feed(self, value: bytes) -> None:
        self.buffer.extend(value)

    def read(self, size: int) -> bytes:
        value = bytes(self.buffer[:size])
        del self.buffer[:size]
        return value

    def close(self) -> None:
        self.is_open = False


class UsbCdcTransportBufferTests(unittest.TestCase):
    def setUp(self) -> None:
        self.serial = FakeSerial()
        self.transport = UsbCdcTransport("/dev/fake", max_line_bytes=256)
        self.transport._serial = self.serial

    def test_partial_json_is_buffered_until_newline(self) -> None:
        self.serial.feed(b'{"type":"sta')
        self.assertEqual(self.transport.read_lines(), [])
        self.serial.feed(b'te","seq":1}\n{"type":"ack"')
        self.assertEqual(self.transport.read_lines(), ['{"type":"state","seq":1}'])
        self.serial.feed(b'}\r\n')
        self.assertEqual(self.transport.read_lines(), ['{"type":"ack"}'])

    def test_oversize_line_is_discarded_without_corrupting_next_line(self) -> None:
        self.serial.feed(b"x" * 300)
        self.assertEqual(self.transport.read_lines(), [])
        self.serial.feed(b"discard-rest\n{\"type\":\"status\"}\n")
        self.assertEqual(self.transport.read_lines(), ['{"type":"status"}'])


class UsbPortDiscoveryTests(unittest.TestCase):
    def test_prefers_macos_callout_usbmodem_node(self) -> None:
        ports = [
            SimpleNamespace(device="/dev/tty.usbmodem1101", description="USB JTAG/serial debug unit"),
            SimpleNamespace(device="/dev/cu.usbmodem1101", description="USB JTAG/serial debug unit"),
            SimpleNamespace(device="/dev/cu.Bluetooth-Incoming-Port", description="Bluetooth"),
        ]
        self.assertEqual(discover_esp32_usb_ports(ports), ["/dev/cu.usbmodem1101"])

    def test_does_not_select_usb_to_uart_bridge_by_name(self) -> None:
        ports = [
            SimpleNamespace(device="/dev/cu.SLAB_USBtoUART", description="CP2102 USB to UART Bridge"),
        ]
        self.assertEqual(discover_esp32_usb_ports(ports), [])

    def test_auto_selection_requires_one_unambiguous_devkit_usb_device(self) -> None:
        transport = UsbCdcTransport()
        with patch("backend.transport.discover_esp32_usb_ports", return_value=[]):
            with self.assertRaisesRegex(OSError, "ESP32_A USB device not found"):
                transport._resolve_port()
        with patch(
            "backend.transport.discover_esp32_usb_ports",
            return_value=["/dev/cu.usbmodem1101", "/dev/cu.usbmodem1201"],
        ):
            with self.assertRaisesRegex(OSError, "multiple ESP32 USB devices"):
                transport._resolve_port()


if __name__ == "__main__":
    unittest.main()
