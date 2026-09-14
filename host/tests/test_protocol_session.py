from __future__ import annotations

import asyncio
import json
import unittest

from saki_host.models import AgentState, StateSnapshot
from saki_host.protocol import ProtocolCodec
from saki_host.protocol_session import ProtocolSession, ProtocolSessionError
from saki_host.transports.base import TransportKind


class FakeAsyncTransport:
    kind = TransportKind.USB
    identity_hint = "fake-usb"

    def __init__(self) -> None:
        self.responses: asyncio.Queue[bytes] = asyncio.Queue()
        self.writes: list[bytes] = []
        self.connected = False

    async def connect(self) -> None:
        self.connected = True

    async def read(self, max_bytes: int) -> bytes:
        data = await self.responses.get()
        if len(data) <= max_bytes:
            return data
        await self.responses.put(data[max_bytes:])
        return data[:max_bytes]

    async def write(self, data: bytes) -> None:
        self.writes.append(data)

    async def close(self) -> None:
        self.connected = False

    async def respond(self, message: dict[str, object]) -> None:
        await self.responses.put(json.dumps(message, separators=(",", ":")).encode() + b"\n")


class ProtocolSessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_handshake_validates_capabilities(self) -> None:
        transport = FakeAsyncTransport()
        session = ProtocolSession(
            transport,
            ProtocolCodec("00000000-0000-4000-8000-000000000001"),
            retry_count=0,
        )
        await session.connect()
        await transport.respond(
            {
                "v": 1,
                "type": "hello",
                "id": 7,
                "reply_to": 1,
                "role": "device",
                "device": {"name": "saki-box3", "fw": "0.3.0", "id": "0123456789ab"},
                "screen": {"width": 320, "height": 240},
                "capabilities": ["status", "ble"],
            }
        )

        reply = await session.handshake(required_capabilities=frozenset({"ble"}))

        self.assertEqual(reply["role"], "device")
        self.assertEqual(len(transport.writes), 1)

    async def test_missing_required_capability_fails(self) -> None:
        transport = FakeAsyncTransport()
        session = ProtocolSession(transport, retry_count=0)
        await session.connect()
        await transport.respond(
            {
                "v": 1,
                "type": "hello",
                "id": 7,
                "reply_to": 1,
                "role": "device",
                "device": {"id": "0123456789ab"},
                "screen": {"width": 320, "height": 240},
                "capabilities": ["status"],
            }
        )

        with self.assertRaisesRegex(ProtocolSessionError, "ble"):
            await session.handshake(required_capabilities=frozenset({"ble"}))

    async def test_retry_reuses_exact_frame(self) -> None:
        transport = FakeAsyncTransport()
        session = ProtocolSession(transport, response_timeout=0.01, retry_count=1)
        await session.connect()
        snapshot = StateSnapshot(state=AgentState.IDLE)

        async def delayed_reply() -> None:
            while len(transport.writes) < 2:
                await asyncio.sleep(0)
            message = json.loads(transport.writes[-1])
            await transport.respond(
                {
                    "v": 1,
                    "type": "ack",
                    "id": 5,
                    "reply_to": message["id"],
                    "ok": True,
                    "applied": True,
                    "last_seq": message["seq"],
                }
            )

        responder = asyncio.create_task(delayed_reply())
        reply = await session.apply_status(snapshot)
        await responder

        self.assertTrue(reply["applied"])
        self.assertEqual(transport.writes[0], transport.writes[1])

    async def test_raw_diagnostic_write_and_drain_preserve_session_bounds(self) -> None:
        transport = FakeAsyncTransport()
        session = ProtocolSession(transport, retry_count=0)
        await session.connect()

        await session.write_raw(b"{invalid}\n")
        await transport.responses.put(b"error-one\nerror-two\n")
        drained = await session.drain_input(quiet_seconds=0.001, timeout=0.02)

        self.assertEqual(transport.writes, [b"{invalid}\n"])
        self.assertEqual(drained, len(b"error-one\nerror-two\n"))

    async def test_reconnect_discards_a_partial_received_frame(self) -> None:
        transport = FakeAsyncTransport()
        session = ProtocolSession(
            transport,
            ProtocolCodec("00000000-0000-4000-8000-000000000001"),
            retry_count=0,
        )
        await session.connect()
        session._receive_buffer.extend(b'{"v":1,"type":"hello"')
        await session.close()
        await session.connect()
        await transport.respond(
            {
                "v": 1,
                "type": "hello",
                "id": 7,
                "reply_to": 1,
                "role": "device",
                "device": {"id": "0123456789ab"},
                "screen": {"width": 320, "height": 240},
                "capabilities": ["status"],
            }
        )

        reply = await session.handshake()

        self.assertEqual(reply["role"], "device")


if __name__ == "__main__":
    unittest.main()
