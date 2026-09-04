from __future__ import annotations

import json
import time
import uuid
from collections.abc import Mapping
from typing import Any

from . import __version__
from .models import StateSnapshot

PROTOCOL_VERSION = 1
MAX_FRAME_BYTES = 2048
UINT32_MASK = 0xFFFF_FFFF


class ProtocolError(ValueError):
    pass


def encode_frame(message: Mapping[str, Any]) -> bytes:
    payload = json.dumps(
        message,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    if len(payload) > MAX_FRAME_BYTES:
        raise ProtocolError(f"frame is {len(payload)} bytes; limit is {MAX_FRAME_BYTES}")
    return payload + b"\n"


def decode_frame(frame: bytes) -> dict[str, Any]:
    payload = frame.rstrip(b"\r\n")
    if len(payload) > MAX_FRAME_BYTES:
        raise ProtocolError("frame_too_large")
    try:
        decoded = payload.decode("utf-8")
        message = json.loads(decoded)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError("invalid_json") from exc
    if not isinstance(message, dict):
        raise ProtocolError("message must be an object")
    if message.get("v") != PROTOCOL_VERSION:
        raise ProtocolError("unsupported_version")
    if not isinstance(message.get("type"), str):
        raise ProtocolError("missing message type")
    return message


class ProtocolCodec:
    def __init__(self, session: str | None = None) -> None:
        self.session = session or str(uuid.uuid4())
        self._next_id = 1
        self._next_seq = 1

    def _id(self) -> int:
        value = self._next_id
        self._next_id = (self._next_id + 1) & UINT32_MASK
        return value

    def _seq(self) -> int:
        value = self._next_seq
        self._next_seq = (self._next_seq + 1) & UINT32_MASK
        return value

    def hello(self) -> dict[str, Any]:
        return {
            "v": PROTOCOL_VERSION,
            "type": "hello",
            "id": self._id(),
            "session": self.session,
            "role": "host",
            "client": {"name": "saki-mac", "version": __version__},
        }

    def status(self, snapshot: StateSnapshot) -> dict[str, Any]:
        return {
            "v": PROTOCOL_VERSION,
            "type": "status",
            "id": self._id(),
            "session": self.session,
            "seq": self._seq(),
            **snapshot.to_payload(),
        }

    def clear(self) -> dict[str, Any]:
        return {
            "v": PROTOCOL_VERSION,
            "type": "clear",
            "id": self._id(),
            "session": self.session,
            "seq": self._seq(),
        }

    def ping(self) -> dict[str, Any]:
        return {
            "v": PROTOCOL_VERSION,
            "type": "ping",
            "id": self._id(),
            "session": self.session,
            "sent_at": time.time_ns() // 1_000_000,
        }
