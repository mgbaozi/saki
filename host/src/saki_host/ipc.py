from __future__ import annotations

import json
import os
import re
import socket
import stat
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from .models import StateSnapshot, snapshot_from_payload

DEFAULT_SOCKET_PATH = Path("/tmp/saki-agent-display.sock")
IPC_VERSION = 1
MAX_DATAGRAM_BYTES = 4096


class HookDeliveryError(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class SnapshotDatagram:
    snapshot: StateSnapshot
    emitted_monotonic_ns: int | None


def encode_snapshot_datagram(
    snapshot: StateSnapshot,
    *,
    emitted_monotonic_ns: int | None = None,
) -> bytes:
    emitted_at = time.monotonic_ns() if emitted_monotonic_ns is None else emitted_monotonic_ns
    data = json.dumps(
        {
            "v": IPC_VERSION,
            "type": "snapshot",
            "emitted_monotonic_ns": emitted_at,
            "snapshot": snapshot.to_payload(),
        },
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    if len(data) > MAX_DATAGRAM_BYTES:
        raise HookDeliveryError("snapshot exceeds local IPC datagram limit")
    return data


def decode_snapshot_envelope(data: bytes) -> SnapshotDatagram:
    if not data or len(data) > MAX_DATAGRAM_BYTES:
        raise ValueError("invalid local IPC datagram size")
    try:
        message = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("invalid local IPC JSON") from exc
    if not isinstance(message, dict) or message.get("v") != IPC_VERSION:
        raise ValueError("unsupported local IPC message")
    if message.get("type") != "snapshot" or not isinstance(message.get("snapshot"), dict):
        raise ValueError("invalid local IPC message")
    emitted_at = message.get("emitted_monotonic_ns")
    if emitted_at is not None and (
        not isinstance(emitted_at, int) or isinstance(emitted_at, bool) or emitted_at < 0
    ):
        raise ValueError("invalid local IPC timestamp")
    return SnapshotDatagram(snapshot_from_payload(message["snapshot"]), emitted_at)


def decode_snapshot_datagram(data: bytes) -> StateSnapshot:
    return decode_snapshot_envelope(data).snapshot


def deliver_snapshot(snapshot: StateSnapshot, socket_path: Path = DEFAULT_SOCKET_PATH) -> None:
    payload = encode_snapshot_datagram(snapshot)
    client = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        client.sendto(payload, str(socket_path))
    except OSError as exc:
        raise HookDeliveryError(f"cannot deliver hook snapshot: {exc}") from exc
    finally:
        client.close()


class HookDatagramProtocol:
    def __init__(
        self,
        receive: Callable[[StateSnapshot, int | None], None],
        receive_event=None,
        forget_session=None,
    ) -> None:
        self._receive = receive
        self._receive_event = receive_event
        self._forget_session = forget_session

    def connection_made(self, transport: object) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, address: object) -> None:
        del address
        if self._forget_session is not None:
            try:
                command = decode_forget(data)
            except (ValueError, TypeError, RecursionError):
                pass
            else:
                self._forget_session(command)
                return
        if self._receive_event is not None:
            try:
                event = decode_source_event(data)
            except (ValueError, TypeError, KeyError, RecursionError):
                pass
            else:
                self._receive_event(event)
                return
        try:
            envelope = decode_snapshot_envelope(data)
        except (ValueError, TypeError, KeyError, RecursionError):
            return
        self._receive(envelope.snapshot, envelope.emitted_monotonic_ns)

    def error_received(self, exc: Exception) -> None:
        del exc

    def connection_lost(self, exc: Exception | None) -> None:
        del exc


def prepare_socket_path(socket_path: Path) -> None:
    try:
        mode = socket_path.lstat().st_mode
    except FileNotFoundError:
        return
    if not stat.S_ISSOCK(mode):
        raise HookDeliveryError(f"refusing to replace non-socket path: {socket_path}")
    socket_path.unlink()


def protect_socket(socket_path: Path) -> None:
    os.chmod(socket_path, 0o600)


def encode_source_event(event) -> bytes:
    from .adapters import SourceEvent

    if not isinstance(event, SourceEvent):
        raise TypeError("invalid source event")
    data = json.dumps(
        {"v": 2, "type": "source_event", "event": event.to_dict()},
        ensure_ascii=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode()
    if len(data) > MAX_DATAGRAM_BYTES:
        raise ValueError("event too large")
    return data


def decode_source_event(data: bytes):
    from .adapters import SourceEvent

    if not 0 < len(data) <= MAX_DATAGRAM_BYTES:
        raise ValueError("invalid event size")
    value = json.loads(data)
    if (
        not isinstance(value, dict)
        or set(value) != {"v", "type", "event"}
        or value.get("v") != 2
        or value.get("type") != "source_event"
    ):
        raise ValueError("invalid event envelope")
    return SourceEvent.from_dict(value["event"])


def deliver_source_event(event, socket_path: Path = DEFAULT_SOCKET_PATH) -> None:
    data = encode_source_event(event)
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
        client.settimeout(0.05)
        client.sendto(data, str(socket_path))


def encode_forget(session_id: str) -> bytes:
    if not isinstance(session_id, str) or not re.fullmatch(r"(?:[0-9a-f]{32}|all)", session_id):
        raise ValueError("use a pseudonymous session ID or all")
    return json.dumps({"v": 2, "type": "forget", "session_id": session_id}).encode()


def decode_forget(data: bytes) -> str:
    if not 0 < len(data) <= MAX_DATAGRAM_BYTES:
        raise ValueError("invalid command size")
    command = json.loads(data)
    if (
        not isinstance(command, dict)
        or set(command) != {"v", "type", "session_id"}
        or command["v"] != 2
        or command["type"] != "forget"
    ):
        raise ValueError("invalid command")
    encode_forget(command["session_id"])
    return command["session_id"]


def forget_session(session_id: str, socket_path: Path = DEFAULT_SOCKET_PATH) -> None:
    data = encode_forget(session_id)
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
        client.settimeout(0.05)
        client.sendto(data, str(socket_path))
