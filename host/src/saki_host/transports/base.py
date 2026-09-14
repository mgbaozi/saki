from __future__ import annotations

from enum import StrEnum
from typing import Protocol


class TransportKind(StrEnum):
    USB = "usb"
    BLE = "ble"


class ByteTransportError(RuntimeError):
    """Base error raised by an asynchronous byte transport."""


class ByteTransportClosed(ByteTransportError):
    """Raised when the peer disconnects while an operation is in flight."""


class AsyncByteTransport(Protocol):
    kind: TransportKind
    identity_hint: str

    async def connect(self) -> None: ...

    async def read(self, max_bytes: int) -> bytes: ...

    async def write(self, data: bytes) -> None: ...

    async def close(self) -> None: ...
