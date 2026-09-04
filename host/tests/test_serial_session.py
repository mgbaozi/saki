from __future__ import annotations

import json
import unittest
from collections import deque
from unittest.mock import patch

from saki_host.models import AgentState, StateSnapshot
from saki_host.protocol import ProtocolCodec
from saki_host.transports.serial import DeviceError, SerialSession, SerialSessionError


class FakeSerial:
    def __init__(self, responses: list[dict[str, object]]) -> None:
        self.dtr = False
        self._responses = deque(
            json.dumps(response, separators=(",", ":")).encode() + b"\n"
            for response in responses
        )
        self.writes: list[bytes] = []
        self.closed = False
        self.reset_count = 0

    def read(self, size: int = 1) -> bytes:
        if not self._responses:
            return b""
        response = self._responses.popleft()
        if len(response) <= size:
            return response
        self._responses.appendleft(response[size:])
        return response[:size]

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        pass

    def reset_input_buffer(self) -> None:
        self.reset_count += 1

    def close(self) -> None:
        self.closed = True


class SerialSessionTests(unittest.TestCase):
    def test_drain_input_consumes_raw_responses_until_quiet(self) -> None:
        stream = FakeSerial([])
        stream._responses.extend([b"error-one\n", b"error-two\n"])
        session = SerialSession(stream)

        with patch(
            "saki_host.transports.serial.time.monotonic",
            side_effect=[1.0, 1.0, 1.01, 1.01, 1.02, 1.02, 1.3],
        ):
            byte_count = session.drain_input(quiet_seconds=0.25, timeout=2.0)

        self.assertEqual(byte_count, 20)
        self.assertEqual(stream.reset_count, 2)

    def test_handshake_validates_correlated_device_response(self) -> None:
        stream = FakeSerial(
            [
                {"v": 1, "type": "pong", "id": 9, "reply_to": 77},
                {
                    "v": 1,
                    "type": "hello",
                    "id": 1,
                    "reply_to": 1,
                    "role": "device",
                    "device": {"name": "saki-box3", "fw": "0.1.0", "id": "0123456789ab"},
                    "screen": {"width": 320, "height": 240},
                    "capabilities": ["status"],
                },
            ]
        )
        session = SerialSession(stream)

        response = session.handshake(ProtocolCodec().hello())

        self.assertEqual(response["role"], "device")
        self.assertEqual(response["screen"], {"width": 320, "height": 240})
        self.assertEqual(len(stream.writes), 1)

    def test_status_requires_positive_ack(self) -> None:
        stream = FakeSerial(
            [
                {
                    "v": 1,
                    "type": "ack",
                    "id": 4,
                    "reply_to": 1,
                    "ok": True,
                    "applied": True,
                    "last_seq": 1,
                }
            ]
        )
        session = SerialSession(stream)
        message = ProtocolCodec().status(StateSnapshot(state=AgentState.IDLE))

        response = session.apply_status(message)

        self.assertTrue(response["applied"])

    def test_clear_requires_positive_ack(self) -> None:
        stream = FakeSerial(
            [
                {
                    "v": 1,
                    "type": "ack",
                    "id": 4,
                    "reply_to": 1,
                    "ok": True,
                    "applied": True,
                    "last_seq": 1,
                }
            ]
        )
        session = SerialSession(stream)

        response = session.apply_clear(ProtocolCodec().clear())

        self.assertTrue(response["applied"])

    def test_device_error_is_exposed(self) -> None:
        stream = FakeSerial(
            [
                {
                    "v": 1,
                    "type": "error",
                    "id": 2,
                    "reply_to": 1,
                    "code": "handshake_required",
                    "message": "hello is required",
                }
            ]
        )
        session = SerialSession(stream)

        with self.assertRaisesRegex(DeviceError, "handshake_required"):
            session.ping(ProtocolCodec().ping())

    def test_unexpected_response_type_fails(self) -> None:
        stream = FakeSerial(
            [{"v": 1, "type": "pong", "id": 2, "reply_to": 1, "uptime_ms": 1, "last_seq": 0}]
        )
        session = SerialSession(stream)

        with self.assertRaisesRegex(SerialSessionError, "expected hello"):
            session.handshake(ProtocolCodec().hello())


if __name__ == "__main__":
    unittest.main()
