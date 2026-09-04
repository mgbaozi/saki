from __future__ import annotations

import json
import os
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
    def __init__(self, receive: Callable[[StateSnapshot, int | None], None]) -> None:
        self._receive = receive

    def connection_made(self, transport: object) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, address: object) -> None:
        del address
        try:
            envelope = decode_snapshot_envelope(data)
        except ValueError:
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
