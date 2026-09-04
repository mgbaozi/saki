from __future__ import annotations

import asyncio
import socket
import time
from collections.abc import Mapping
from dataclasses import dataclass, replace
from pathlib import Path

from .ipc import (
    DEFAULT_SOCKET_PATH,
    HookDatagramProtocol,
    prepare_socket_path,
    protect_socket,
)
from .models import Activity, ActivityKind, AgentState, Progress, ProgressMode, StateSnapshot
from .protocol import ProtocolCodec
from .transports.serial import SerialSession, SerialSessionError, select_saki_port

_DEVICE_DIAGNOSTIC_KEYS = (
    "valid_frames",
    "invalid_frames",
    "oversized_frames",
    "old_sequences",
    "ui_queue_overwrites",
    "tx_drops",
    "heartbeat_timeouts",
)
_DEVICE_ANOMALY_KEYS = _DEVICE_DIAGNOSTIC_KEYS[1:]
_DEVICE_RUNTIME_KEYS = (
    "heap_free_bytes",
    "heap_min_bytes",
    "internal_free_bytes",
    "internal_min_bytes",
    "app_stack_min_bytes",
    "ui_stack_min_bytes",
    "usb_stack_min_bytes",
)


def parse_device_diagnostics(message: Mapping[str, object]) -> dict[str, int] | None:
    raw = message.get("diagnostics")
    if not isinstance(raw, Mapping):
        return None
    diagnostics: dict[str, int] = {}
    for key in _DEVICE_DIAGNOSTIC_KEYS:
        value = raw.get(key)
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
            or value > 0xFFFF_FFFF
        ):
            return None
        diagnostics[key] = value
    return diagnostics


def parse_device_runtime(message: Mapping[str, object]) -> dict[str, int] | None:
    raw = message.get("runtime")
    if not isinstance(raw, Mapping):
        return None
    runtime: dict[str, int] = {}
    for key in _DEVICE_RUNTIME_KEYS:
        value = raw.get(key)
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
            or value > 0xFFFF_FFFF
        ):
            return None
        runtime[key] = value
    return runtime


class SnapshotMailbox:
    def __init__(self, initial: StateSnapshot) -> None:
        self.latest = initial
        self.generation = 0
        self._changed = asyncio.Event()

    def publish(self, snapshot: StateSnapshot) -> None:
        self.latest = snapshot
        self.generation += 1
        self._changed.set()

    async def wait_after(self, generation: int) -> tuple[StateSnapshot, int]:
        while self.generation <= generation:
            self._changed.clear()
            if self.generation > generation:
                break
            await self._changed.wait()
        return self.latest, self.generation


class SnapshotTimeline:
    def __init__(self) -> None:
        self._task_id: str | None = None
        self._started_at: float | None = None

    def enrich(self, snapshot: StateSnapshot) -> StateSnapshot:
        now = time.monotonic()
        task_id = snapshot.task.id if snapshot.task else None
        if snapshot.state is AgentState.IDLE:
            self._task_id = None
            self._started_at = None
            return snapshot
        if snapshot.state is AgentState.STARTING or task_id != self._task_id:
            self._task_id = task_id
            self._started_at = now
        if self._started_at is None:
            self._started_at = now
        elapsed_ms = max(0, int((now - self._started_at) * 1000))
        return replace(snapshot, elapsed_ms=elapsed_ms)


@dataclass(frozen=True, slots=True)
class ServiceConfig:
    port: str | None = None
    socket_path: Path = DEFAULT_SOCKET_PATH
    heartbeat_seconds: float = 5.0
    stale_after_seconds: float = 120.0
    reconnect_max_seconds: float = 5.0
    reconnect_fast_window_seconds: float = 10.0
    reconnect_fast_interval_seconds: float = 0.1
    connected_port_check_seconds: float = 0.1
    connected_port_missing_samples: int = 2


class ReconnectPolicy:
    def __init__(self, config: ServiceConfig) -> None:
        self._config = config
        self._backoff = 0.25
        self._fast_until: float | None = None

    def on_connected(self) -> None:
        self._backoff = 0.25
        self._fast_until = None

    def next_delay(self, *, was_connected: bool, now: float) -> tuple[float, bool]:
        if was_connected and self._config.reconnect_fast_window_seconds > 0:
            self._fast_until = now + self._config.reconnect_fast_window_seconds
            self._backoff = 0.25

        if self._fast_until is not None and now < self._fast_until:
            remaining = self._fast_until - now
            return min(self._config.reconnect_fast_interval_seconds, remaining), True

        self._fast_until = None
        delay = self._backoff
        self._backoff = min(self._config.reconnect_max_seconds, self._backoff * 2)
        return delay, False


class SakiHostService:
    def __init__(self, config: ServiceConfig) -> None:
        self.config = config
        self.mailbox = SnapshotMailbox(StateSnapshot(state=AgentState.IDLE))
        self.timeline = SnapshotTimeline()
        self._transport: asyncio.DatagramTransport | None = None
        self._last_source_at = time.monotonic()
        self._source_generation = 0
        self._stale_source_generation: int | None = None
        self._last_device_anomalies: tuple[int, ...] | None = None
        self._source_emitted: tuple[int, int] | None = None

    def accept_snapshot(
        self,
        snapshot: StateSnapshot,
        emitted_monotonic_ns: int | None = None,
    ) -> None:
        self._last_source_at = time.monotonic()
        self._source_generation += 1
        self._stale_source_generation = None
        self.mailbox.publish(self.timeline.enrich(snapshot))
        self._source_emitted = (
            (self.mailbox.generation, emitted_monotonic_ns)
            if emitted_monotonic_ns is not None
            else None
        )

    def _take_source_emitted(self, generation: int) -> int | None:
        latest = self._source_emitted
        if latest is None or latest[0] != generation:
            return None
        self._source_emitted = None
        return latest[1]

    def publish_stale_if_needed(self, *, now: float | None = None) -> bool:
        """Move an abandoned active state to a truthful, generic waiting state."""
        if self.config.stale_after_seconds <= 0:
            return False
        if self.mailbox.latest.state not in {
            AgentState.STARTING,
            AgentState.THINKING,
            AgentState.WORKING,
        }:
            return False
        if self._stale_source_generation == self._source_generation:
            return False
        checked_at = time.monotonic() if now is None else now
        if checked_at - self._last_source_at < self.config.stale_after_seconds:
            return False

        stale = replace(
            self.mailbox.latest,
            state=AgentState.WAITING_USER,
            activity=Activity(
                ActivityKind.MESSAGE,
                "长时间未收到 Agent 事件，请检查 Mac",
            ),
            progress=Progress(ProgressMode.INDETERMINATE),
        )
        self._stale_source_generation = self._source_generation
        self.mailbox.publish(self.timeline.enrich(stale))
        print(
            f"source stale after {self.config.stale_after_seconds:g}s; "
            "displaying waiting_user",
            flush=True,
        )
        return True

    async def _stale_loop(self) -> None:
        interval = min(self.config.heartbeat_seconds, self.config.stale_after_seconds)
        while True:
            await asyncio.sleep(interval)
            self.publish_stale_if_needed()

    def observe_device_diagnostics(self, pong: Mapping[str, object]) -> bool:
        diagnostics = parse_device_diagnostics(pong)
        if diagnostics is None:
            return False
        anomalies = tuple(diagnostics[key] for key in _DEVICE_ANOMALY_KEYS)
        if anomalies == self._last_device_anomalies:
            return False
        self._last_device_anomalies = anomalies
        if not any(anomalies):
            return False
        summary = " ".join(f"{key}={diagnostics[key]}" for key in _DEVICE_DIAGNOSTIC_KEYS)
        print(f"device diagnostics {summary}", flush=True)
        return True

    async def _send_status_with_retry(
        self,
        session: SerialSession,
        codec: ProtocolCodec,
        snapshot: StateSnapshot,
        source_emitted_ns: int | None = None,
    ) -> None:
        message = codec.status(snapshot)
        last_error: SerialSessionError | None = None
        for attempt in range(3):
            try:
                ack = await asyncio.to_thread(session.apply_status, message, 1.0)
                summary = (
                    f"synced state={snapshot.state.value} "
                    f"seq={ack.get('last_seq')} attempt={attempt + 1}"
                )
                if source_emitted_ns is not None:
                    source_to_ack_ms = max(
                        0.0,
                        (time.monotonic_ns() - source_emitted_ns) / 1_000_000,
                    )
                    summary += f" source_to_ack_ms={source_to_ack_ms:.1f}"
                print(summary, flush=True)
                return
            except SerialSessionError as exc:
                last_error = exc
                if attempt < 2:
                    await asyncio.sleep(0.1)
        assert last_error is not None
        raise last_error

    async def _wait_for_port_disappearance(self, port: str) -> None:
        missing_samples = 0
        while True:
            await asyncio.sleep(self.config.connected_port_check_seconds)
            if Path(port).exists():
                missing_samples = 0
                continue
            missing_samples += 1
            if missing_samples >= self.config.connected_port_missing_samples:
                return

    async def _connected_loop(
        self,
        session: SerialSession,
        codec: ProtocolCodec,
        port: str,
    ) -> None:
        sent_generation = self.mailbox.generation
        disappeared = asyncio.create_task(self._wait_for_port_disappearance(port))
        try:
            await self._send_status_with_retry(
                session,
                codec,
                self.mailbox.latest,
                self._take_source_emitted(sent_generation),
            )
            while True:
                update = asyncio.create_task(self.mailbox.wait_after(sent_generation))
                done, _ = await asyncio.wait(
                    {update, disappeared},
                    timeout=self.config.heartbeat_seconds,
                    return_when=asyncio.FIRST_COMPLETED,
                )
                if disappeared in done:
                    update.cancel()
                    await asyncio.gather(update, return_exceptions=True)
                    raise SerialSessionError(f"serial device disappeared: {port}")
                if update in done:
                    snapshot, generation = update.result()
                    await self._send_status_with_retry(
                        session,
                        codec,
                        snapshot,
                        self._take_source_emitted(generation),
                    )
                    sent_generation = generation
                    continue

                update.cancel()
                await asyncio.gather(update, return_exceptions=True)
                pong = await asyncio.to_thread(session.ping, codec.ping(), 2.0)
                self.observe_device_diagnostics(pong)
        finally:
            disappeared.cancel()
            await asyncio.gather(disappeared, return_exceptions=True)

    async def _connection_loop(self) -> None:
        reconnect = ReconnectPolicy(self.config)
        while True:
            session: SerialSession | None = None
            was_connected = False
            error: SerialSessionError | None = None
            try:
                port = await asyncio.to_thread(select_saki_port, self.config.port)
                session = await asyncio.to_thread(SerialSession.open, port)
                codec = ProtocolCodec()
                hello = await asyncio.to_thread(session.handshake, codec.hello(), 3.0)
                device = hello["device"]
                print(f"connected device={device['id']} port={port}", flush=True)
                reconnect.on_connected()
                was_connected = True
                await self._connected_loop(session, codec, port)
            except asyncio.CancelledError:
                raise
            except SerialSessionError as exc:
                error = exc
            finally:
                if session is not None:
                    await asyncio.to_thread(session.close)

            if error is None:
                error = SerialSessionError("connected session ended unexpectedly")
            delay, fast = reconnect.next_delay(
                was_connected=was_connected,
                now=time.monotonic(),
            )
            if was_connected or not fast:
                mode = " fast-window" if fast else ""
                print(
                    f"disconnected reason={error}; retrying in {delay:.2f}s{mode}",
                    flush=True,
                )
            await asyncio.sleep(delay)

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        prepare_socket_path(self.config.socket_path)
        transport, _ = await loop.create_datagram_endpoint(
            lambda: HookDatagramProtocol(self.accept_snapshot),
            family=socket.AF_UNIX,
            local_addr=str(self.config.socket_path),
        )
        self._transport = transport
        protect_socket(self.config.socket_path)
        print(f"hook socket ready path={self.config.socket_path}", flush=True)
        connection_task = asyncio.create_task(self._connection_loop())
        stale_task = (
            asyncio.create_task(self._stale_loop())
            if self.config.stale_after_seconds > 0
            else None
        )
        try:
            await connection_task
        finally:
            connection_task.cancel()
            tasks = [connection_task]
            if stale_task is not None:
                stale_task.cancel()
                tasks.append(stale_task)
            await asyncio.gather(*tasks, return_exceptions=True)
            transport.close()
            try:
                self.config.socket_path.unlink()
            except FileNotFoundError:
                pass
