from __future__ import annotations

import unittest

from backend.transport import SerialTransport


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


class SerialTransportBufferTests(unittest.TestCase):
    def setUp(self) -> None:
        self.serial = FakeSerial()
        self.transport = SerialTransport("/dev/fake", max_line_bytes=256)
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


if __name__ == "__main__":
    unittest.main()
