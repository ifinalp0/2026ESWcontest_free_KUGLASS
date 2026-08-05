from __future__ import annotations

import unittest
from types import SimpleNamespace
from unittest.mock import patch

from backend.camera import CAMERA_FORMAT_JPEG, CAMERA_MAGIC, fnv1a
from backend.transport import CAMERA_HEADER, UsbCdcTransport, discover_esp32_usb_ports


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

    def test_json_lines_and_camera_frames_share_the_usb_stream(self) -> None:
        jpeg = b"\xff\xd8camera-payload\x0a-with-newline\xff\xd9"
        header = CAMERA_HEADER.pack(
            CAMERA_MAGIC,
            42,
            640,
            480,
            CAMERA_FORMAT_JPEG,
            len(jpeg),
            fnv1a(jpeg),
        )
        self.serial.feed(b'{"type":"state","seq":1}\n' + header[:12])
        self.assertEqual(self.transport.read_lines(), ['{"type":"state","seq":1}'])
        self.assertEqual(self.transport.read_camera_frames(), [])

        self.serial.feed(header[12:] + jpeg + b'{"type":"ack","seq":2}\n')
        self.assertEqual(self.transport.read_lines(), ['{"type":"ack","seq":2}'])
        [frame] = self.transport.read_camera_frames()
        self.assertEqual(frame.sequence, 42)
        self.assertEqual((frame.width, frame.height), (640, 480))
        self.assertEqual(frame.payload, jpeg)

    def test_bad_camera_checksum_is_counted_and_parser_resynchronizes(self) -> None:
        jpeg = b"\xff\xd8broken\xff\xd9"
        header = CAMERA_HEADER.pack(
            CAMERA_MAGIC,
            7,
            640,
            480,
            CAMERA_FORMAT_JPEG,
            len(jpeg),
            fnv1a(jpeg) ^ 0x1,
        )
        self.serial.feed(header + jpeg + b'\n{"type":"state","seq":8}\n')
        lines = self.transport.read_lines()
        self.assertIn('{"type":"state","seq":8}', lines)
        self.assertEqual(self.transport.read_camera_frames(), [])
        self.assertGreaterEqual(self.transport.bad_camera_frames, 1)


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
