from __future__ import annotations

import asyncio
import time
from collections.abc import Mapping
from typing import Any

from .models import StateSnapshot
from .protocol import MAX_FRAME_BYTES, ProtocolCodec, ProtocolError, decode_frame, encode_frame
from .transports.base import AsyncByteTransport, ByteTransportError


class ProtocolSessionError(RuntimeError):
    """A transport or protocol failure in a shared asynchronous session."""


class ProtocolSessionTimeout(ProtocolSessionError):
    """The peer did not provide the correlated reply before the deadline."""


class DeviceReplyError(ProtocolSessionError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"device error {code}: {message}")
        self.code = code
        self.message = message


class ProtocolSession:
    """Request/reply protocol over any ordered asynchronous byte transport.

    Requests are serialized so complete NDJSON frames cannot be interleaved.
    Retries resend the exact same encoded message and therefore preserve id/seq.
    """

    def __init__(
        self,
        transport: AsyncByteTransport,
        codec: ProtocolCodec | None = None,
        *,
        response_timeout: float = 2.0,
        retry_count: int = 2,
    ) -> None:
        if response_timeout <= 0:
            raise ValueError("response_timeout must be positive")
        if retry_count < 0:
            raise ValueError("retry_count cannot be negative")
        self.transport = transport
        self.codec = codec or ProtocolCodec()
        self.response_timeout = response_timeout
        self.retry_count = retry_count
        self._receive_buffer = bytearray()
        self._request_lock = asyncio.Lock()
        self._connected = False
        self.multi_session = False
        self.capabilities: frozenset[str] = frozenset()
        self.generic_source = False

    async def connect(self) -> None:
        if self._connected:
            return
        self._receive_buffer.clear()
        self.multi_session = False
        self.capabilities = frozenset()
        self.generic_source = False
        try:
            await self.transport.connect()
        except ByteTransportError as exc:
            raise ProtocolSessionError(str(exc)) from exc
        self._connected = True

    async def close(self) -> None:
        self._connected = False
        self._receive_buffer.clear()
        self.multi_session = False
        self.capabilities = frozenset()
        self.generic_source = False
        try:
            await self.transport.close()
        except ByteTransportError as exc:
            raise ProtocolSessionError(str(exc)) from exc

    async def write_raw(self, data: bytes) -> None:
        """Write bounded diagnostic bytes without assigning protocol semantics."""
        if not self._connected:
            raise ProtocolSessionError("protocol session is not connected")
        if not data:
            return
        async with self._request_lock:
            try:
                await self.transport.write(data)
            except ByteTransportError as exc:
                raise ProtocolSessionError(str(exc)) from exc

    async def drain_input(self, *, quiet_seconds: float = 0.25, timeout: float = 2.0) -> int:
        """Discard pending diagnostic replies until the byte stream is quiet."""
        if quiet_seconds <= 0 or timeout <= 0:
            raise ValueError("quiet_seconds and timeout must be positive")
        if not self._connected:
            raise ProtocolSessionError("protocol session is not connected")

        async with self._request_lock:
            drained = len(self._receive_buffer)
            self._receive_buffer.clear()
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return drained
                try:
                    chunk = await asyncio.wait_for(
                        self.transport.read(256),
                        timeout=min(quiet_seconds, remaining),
                    )
                except TimeoutError:
                    return drained
                except ByteTransportError as exc:
                    raise ProtocolSessionError(str(exc)) from exc
                if not chunk:
                    raise ProtocolSessionError("transport returned an empty read")
                drained += len(chunk)

    async def _read_message(self, deadline: float) -> dict[str, Any]:
        while True:
            newline = self._receive_buffer.find(b"\n")
            if newline >= 0:
                frame = bytes(self._receive_buffer[: newline + 1])
                del self._receive_buffer[: newline + 1]
                if frame.rstrip(b"\r\n"):
                    try:
                        return decode_frame(frame)
                    except ProtocolError as exc:
                        raise ProtocolSessionError(f"invalid device frame: {exc}") from exc

            if len(self._receive_buffer) > MAX_FRAME_BYTES:
                self._receive_buffer.clear()
                raise ProtocolSessionError("device frame exceeds protocol limit")

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolSessionTimeout("timed out waiting for device response")
            try:
                chunk = await asyncio.wait_for(self.transport.read(256), timeout=remaining)
            except TimeoutError as exc:
                raise ProtocolSessionTimeout("timed out waiting for device response") from exc
            except ByteTransportError as exc:
                raise ProtocolSessionError(str(exc)) from exc
            if not chunk:
                raise ProtocolSessionError("transport returned an empty read")
            self._receive_buffer.extend(chunk)

    async def request(
        self,
        message: Mapping[str, Any],
        expected_type: str,
        *,
        timeout: float | None = None,
        retry_count: int | None = None,
    ) -> dict[str, Any]:
        request_id = message.get("id")
        if not isinstance(request_id, int) or isinstance(request_id, bool):
            raise ProtocolSessionError("outgoing request has no integer id")
        if not self._connected:
            raise ProtocolSessionError("protocol session is not connected")

        response_timeout = self.response_timeout if timeout is None else timeout
        retries = self.retry_count if retry_count is None else retry_count
        if response_timeout <= 0:
            raise ValueError("timeout must be positive")
        if retries < 0:
            raise ValueError("retry_count cannot be negative")

        frame = encode_frame(message)
        async with self._request_lock:
            for attempt in range(retries + 1):
                try:
                    await self.transport.write(frame)
                except ByteTransportError as exc:
                    raise ProtocolSessionError(str(exc)) from exc
                deadline = time.monotonic() + response_timeout
                try:
                    while True:
                        response = await self._read_message(deadline)
                        if response.get("reply_to") != request_id:
                            continue
                        if response.get("type") == "error":
                            raise DeviceReplyError(
                                str(response.get("code", "unknown_error")),
                                str(response.get("message", "no diagnostic message")),
                            )
                        if response.get("type") != expected_type:
                            raise ProtocolSessionError(
                                f"request {request_id} expected {expected_type}, "
                                f"received {response.get('type')!r}"
                            )
                        return response
                except ProtocolSessionTimeout:
                    if attempt == retries:
                        raise
        raise AssertionError("request retry loop exhausted unexpectedly")

    async def handshake(
        self,
        *,
        timeout: float = 3.0,
        required_capabilities: frozenset[str] = frozenset(),
    ) -> dict[str, Any]:
        response = await self.request(self.codec.hello(), "hello", timeout=timeout)
        if response.get("role") != "device":
            raise ProtocolSessionError("hello response does not identify a device")
        device = response.get("device")
        screen = response.get("screen")
        capabilities = response.get("capabilities")
        if not isinstance(device, dict) or not isinstance(device.get("id"), str):
            raise ProtocolSessionError("hello response has invalid device metadata")
        if not isinstance(screen, dict) or not all(
            isinstance(screen.get(key), int) and not isinstance(screen.get(key), bool)
            for key in ("width", "height")
        ):
            raise ProtocolSessionError("hello response has invalid screen metadata")
        if not isinstance(capabilities, list) or not all(
            isinstance(item, str) for item in capabilities
        ):
            raise ProtocolSessionError("hello response has invalid capabilities")
        missing = required_capabilities.difference(capabilities)
        if missing:
            raise ProtocolSessionError(
                f"device is missing required capabilities: {', '.join(sorted(missing))}"
            )
        self.capabilities = frozenset(capabilities)
        return response

    async def enable_multi_session(self) -> None:
        message = self.codec.hello()
        message["mode"] = "multi-session"
        response = await self.request(message, "hello")
        if response.get("mode") != "multi-session":
            raise ProtocolSessionError("device did not select multi-session mode")
        capabilities = response.get("capabilities")
        if isinstance(capabilities, list) and all(isinstance(item, str) for item in capabilities):
            self.capabilities = frozenset(capabilities)
        self.multi_session = True
        from .display import GENERIC_SOURCE_CAPABILITY

        self.generic_source = GENERIC_SOURCE_CAPABILITY in self.capabilities

    async def apply_projection(self, projection: dict) -> dict:
        from .display import encode_projection

        message = encode_projection(
            self.codec, projection, generic_source=self.multi_session and self.generic_source
        )
        response = await self.request(message, "ack")
        if (
            response.get("ok") is not True
            or type(response.get("last_seq")) is not int
            or response["last_seq"] != message["seq"]
            or not (response.get("applied") is True or response.get("committed") is True)
        ):
            raise ProtocolSessionError("display projection was not committed")
        return response

    async def apply_status(self, snapshot: StateSnapshot) -> dict[str, Any]:
        response = await self.request(self.codec.status(snapshot), "ack")
        if response.get("ok") is not True:
            raise ProtocolSessionError("device returned a negative acknowledgement")
        return response

    async def apply_clear(self) -> dict[str, Any]:
        response = await self.request(self.codec.clear(), "ack")
        if response.get("ok") is not True:
            raise ProtocolSessionError("device returned a negative acknowledgement")
        return response

    async def ping(self) -> dict[str, Any]:
        return await self.request(self.codec.ping(), "pong")
