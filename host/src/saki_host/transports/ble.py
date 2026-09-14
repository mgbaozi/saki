from __future__ import annotations

import asyncio
from collections.abc import Callable, Mapping, Sequence
from contextlib import suppress
from dataclasses import dataclass
from typing import Any, Protocol

from ..ble_ids import SAKI_RX_UUID, SAKI_SERVICE_UUID, SAKI_TX_UUID
from .base import ByteTransportClosed, ByteTransportError, TransportKind

DEFAULT_BLE_SCAN_SECONDS = 5.0
DEFAULT_BLE_CONNECT_SECONDS = 30.0
DEFAULT_BLE_PAYLOAD_BYTES = 20
BLE_SECURITY_RETRY_SECONDS = 0.25
MAX_WRITE_WITH_RESPONSE_BYTES = 512
DEFAULT_NOTIFY_QUEUE_DEPTH = 32


class BleDependencyError(ByteTransportError):
    """The optional Bleak runtime is not installed."""


class BleDiscoveryError(ByteTransportError):
    """BLE discovery failed or did not find a usable Saki peripheral."""


class BleGattError(ByteTransportError):
    """The connected peripheral does not expose the Saki GATT contract."""


def _error_detail(error: Exception) -> str:
    reason = getattr(getattr(error, "reason", None), "name", None)
    unavailable_reasons = {
        "NO_BLUETOOTH": "Bluetooth hardware is unavailable",
        "NO_BLE_CENTRAL_ROLE": "Bluetooth LE Central role is unavailable",
        "POWERED_OFF": "Bluetooth is powered off",
        "DENIED_BY_USER": "Bluetooth permission was denied by the user",
        "DENIED_BY_SYSTEM": "Bluetooth permission was denied by the system",
        "DENIED_BY_UNKNOWN": "Bluetooth permission was denied",
        "UNKNOWN": "Bluetooth is unavailable",
    }
    if reason in unavailable_reasons:
        return unavailable_reasons[reason]
    return str(error).strip() or type(error).__name__


class _BleakClientLike(Protocol):
    is_connected: bool
    mtu_size: int
    services: Any

    async def connect(self) -> None: ...

    async def disconnect(self) -> None: ...

    async def start_notify(self, characteristic: str, callback: Callable[..., None]) -> None: ...

    async def stop_notify(self, characteristic: str) -> None: ...

    async def write_gatt_char(
        self,
        characteristic: str,
        data: bytes,
        *,
        response: bool,
    ) -> None: ...


ClientFactory = Callable[..., _BleakClientLike]


@dataclass(frozen=True, slots=True)
class BleCandidate:
    identifier: str
    name: str
    rssi: int
    device: object


def _load_bleak() -> tuple[type[Any], type[Any]]:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        raise BleDependencyError(
            "BLE support is not installed; install the 'saki-host[ble]' extra"
        ) from exc
    return BleakClient, BleakScanner


async def discover_saki_devices(
    *,
    timeout: float = DEFAULT_BLE_SCAN_SECONDS,
    scanner_type: type[Any] | None = None,
) -> list[BleCandidate]:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    if scanner_type is None:
        _, scanner_type = _load_bleak()
    try:
        discovered = await scanner_type.discover(
            timeout=timeout,
            return_adv=True,
            service_uuids=[SAKI_SERVICE_UUID],
        )
    except Exception as exc:
        raise BleDiscoveryError(f"BLE scan failed: {_error_detail(exc)}") from exc

    if not isinstance(discovered, Mapping):
        raise BleDiscoveryError("BLE scanner returned an unsupported result")

    candidates: list[BleCandidate] = []
    for device, advertisement in discovered.values():
        identifier = str(getattr(device, "address", ""))
        if not identifier:
            continue
        advertised_uuids = {
            str(value).lower() for value in getattr(advertisement, "service_uuids", ())
        }
        if SAKI_SERVICE_UUID not in advertised_uuids:
            continue
        name = str(
            getattr(advertisement, "local_name", None)
            or getattr(device, "name", None)
            or "Saki"
        )
        rssi = getattr(advertisement, "rssi", None)
        candidates.append(
            BleCandidate(
                identifier=identifier,
                name=name,
                rssi=rssi if isinstance(rssi, int) and not isinstance(rssi, bool) else -127,
                device=device,
            )
        )
    return sorted(candidates, key=lambda item: (-item.rssi, item.identifier))


def _characteristic_properties(services: Any, uuid: str) -> frozenset[str] | None:
    getter = getattr(services, "get_characteristic", None)
    if not callable(getter):
        return None
    characteristic = getter(uuid)
    if characteristic is None:
        return None
    properties = getattr(characteristic, "properties", ())
    if not isinstance(properties, Sequence):
        return None
    return frozenset(str(value).lower() for value in properties)


class BleByteTransport:
    kind = TransportKind.BLE

    def __init__(
        self,
        device: object,
        *,
        client_factory: ClientFactory | None = None,
        connect_timeout: float = DEFAULT_BLE_CONNECT_SECONDS,
        notify_queue_depth: int = DEFAULT_NOTIFY_QUEUE_DEPTH,
        pair: bool = False,
    ) -> None:
        if connect_timeout <= 0:
            raise ValueError("connect_timeout must be positive")
        if notify_queue_depth <= 0:
            raise ValueError("notify_queue_depth must be positive")
        self.device = device
        self.identity_hint = str(getattr(device, "address", "unknown"))
        self._client_factory = client_factory
        self._connect_timeout = connect_timeout
        self._notify_queue_depth = notify_queue_depth
        self._pair = pair
        self._client: _BleakClientLike | None = None
        self._notifications: asyncio.Queue[bytes | None] = asyncio.Queue(notify_queue_depth)
        self._read_buffer = bytearray()
        self._write_lock = asyncio.Lock()
        self._terminal_error: ByteTransportError | None = None
        self._notify_started = False

    @property
    def negotiated_att_mtu(self) -> int | None:
        client = self._client
        mtu = getattr(client, "mtu_size", None) if client is not None else None
        if not isinstance(mtu, int) or isinstance(mtu, bool) or not 23 <= mtu <= 517:
            return None
        return mtu

    @property
    def write_payload_bytes(self) -> int:
        mtu = self.negotiated_att_mtu
        if mtu is None:
            return DEFAULT_BLE_PAYLOAD_BYTES
        return min(mtu - 3, MAX_WRITE_WITH_RESPONSE_BYTES)

    def _reset_buffers(self) -> None:
        self._notifications = asyncio.Queue(self._notify_queue_depth)
        self._read_buffer.clear()
        self._terminal_error = None

    def _signal_terminal(self, error: ByteTransportError | None) -> None:
        if error is not None and self._terminal_error is None:
            self._terminal_error = error
        try:
            self._notifications.put_nowait(None)
        except asyncio.QueueFull:
            if self._terminal_error is None:
                self._terminal_error = ByteTransportError("BLE notification queue overflow")

    def _on_disconnect(self, _client: object) -> None:
        self._signal_terminal(ByteTransportClosed("BLE peripheral disconnected"))

    def _on_notification(self, _sender: object, data: bytearray) -> None:
        if self._terminal_error is not None:
            return
        try:
            self._notifications.put_nowait(bytes(data))
        except asyncio.QueueFull:
            self._signal_terminal(ByteTransportError("BLE notification queue overflow"))

    @staticmethod
    def _security_retryable(error: Exception) -> bool:
        message = str(error).casefold()
        return any(
            marker in message
            for marker in (
                "authentication is insufficient",
                "insufficient authentication",
                "insufficient encryption",
                "encryption is insufficient",
            )
        )

    async def _start_notify_after_security(self, client: _BleakClientLike) -> None:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self._connect_timeout

        while True:
            try:
                await client.start_notify(SAKI_TX_UUID, self._on_notification)
                return
            except Exception as exc:
                remaining = deadline - loop.time()
                if (
                    not client.is_connected
                    or remaining <= 0
                    or not self._security_retryable(exc)
                ):
                    raise
                await asyncio.sleep(min(BLE_SECURITY_RETRY_SECONDS, remaining))

    async def connect(self) -> None:
        if self._client is not None and self._client.is_connected:
            return
        self._reset_buffers()
        if self._client_factory is None:
            client_type, _ = _load_bleak()
            factory: ClientFactory = client_type
        else:
            factory = self._client_factory
        try:
            client = factory(
                self.device,
                disconnected_callback=self._on_disconnect,
                services=[SAKI_SERVICE_UUID],
                timeout=self._connect_timeout,
                pair=self._pair,
            )
            self._client = client
            await client.connect()
            if not client.is_connected:
                raise BleGattError("BLE client did not report a connected state")
            services = client.services
            service_getter = getattr(services, "get_service", None)
            if not callable(service_getter) or service_getter(SAKI_SERVICE_UUID) is None:
                raise BleGattError("Saki service is missing")
            rx_properties = _characteristic_properties(services, SAKI_RX_UUID)
            tx_properties = _characteristic_properties(services, SAKI_TX_UUID)
            if rx_properties is None or "write" not in rx_properties:
                raise BleGattError("Saki RX characteristic must support write with response")
            if tx_properties is None or "notify" not in tx_properties:
                raise BleGattError("Saki TX characteristic must support notify")
            await self._start_notify_after_security(client)
            self._notify_started = True
        except ByteTransportError:
            await self._close_after_failed_connect()
            raise
        except Exception as exc:
            await self._close_after_failed_connect()
            raise BleGattError(f"BLE connection failed: {_error_detail(exc)}") from exc

    async def _close_after_failed_connect(self) -> None:
        client = self._client
        self._notify_started = False
        self._client = None
        if client is not None and client.is_connected:
            with suppress(Exception):
                await client.disconnect()

    async def read(self, max_bytes: int) -> bytes:
        if max_bytes <= 0:
            raise ValueError("max_bytes must be positive")
        if self._read_buffer:
            chunk = bytes(self._read_buffer[:max_bytes])
            del self._read_buffer[:max_bytes]
            return chunk
        if self._terminal_error is not None:
            raise self._terminal_error
        client = self._client
        if client is None or not client.is_connected:
            raise ByteTransportClosed("BLE transport is not connected")

        item = await self._notifications.get()
        if item is None:
            raise self._terminal_error or ByteTransportClosed("BLE peripheral disconnected")
        self._read_buffer.extend(item)
        chunk = bytes(self._read_buffer[:max_bytes])
        del self._read_buffer[:max_bytes]
        return chunk

    async def write(self, data: bytes) -> None:
        if not data:
            return
        if self._terminal_error is not None:
            raise self._terminal_error
        client = self._client
        if client is None or not client.is_connected or not self._notify_started:
            raise ByteTransportClosed("BLE transport is not ready")
        payload_size = self.write_payload_bytes
        async with self._write_lock:
            try:
                for offset in range(0, len(data), payload_size):
                    await client.write_gatt_char(
                        SAKI_RX_UUID,
                        data[offset : offset + payload_size],
                        response=True,
                    )
            except Exception as exc:
                raise ByteTransportError(f"BLE write failed: {_error_detail(exc)}") from exc

    async def close(self) -> None:
        client = self._client
        self._client = None
        self._read_buffer.clear()
        self._signal_terminal(ByteTransportClosed("BLE transport closed"))
        if client is None:
            self._notify_started = False
            return
        try:
            if self._notify_started and client.is_connected:
                await client.stop_notify(SAKI_TX_UUID)
            if client.is_connected:
                await client.disconnect()
        except Exception as exc:
            raise ByteTransportError(f"BLE disconnect failed: {_error_detail(exc)}") from exc
        finally:
            self._notify_started = False
