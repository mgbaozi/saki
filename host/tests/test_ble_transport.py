from __future__ import annotations

import unittest
from dataclasses import dataclass
from typing import Any, ClassVar

from saki_host.ble_ids import SAKI_RX_UUID, SAKI_SERVICE_UUID, SAKI_TX_UUID
from saki_host.transports.base import ByteTransportError
from saki_host.transports.ble import (
    BleByteTransport,
    BleGattError,
    _error_detail,
    discover_saki_devices,
)


@dataclass
class FakeDevice:
    address: str = "00000000-0000-4000-8000-000000000001"
    name: str = "Saki"


@dataclass
class FakeAdvertisement:
    service_uuids: list[str]
    rssi: int
    local_name: str | None = "Saki"


class FakeScanner:
    call: ClassVar[dict[str, Any]] = {}

    @classmethod
    async def discover(cls, **kwargs: Any) -> dict[str, tuple[FakeDevice, FakeAdvertisement]]:
        cls.call = kwargs
        return {
            "near": (
                FakeDevice(),
                FakeAdvertisement([SAKI_SERVICE_UUID.upper()], -41),
            ),
            "other": (
                FakeDevice("00000000-0000-4000-8000-000000000002", "Other"),
                FakeAdvertisement(["0000180f-0000-1000-8000-00805f9b34fb"], -20),
            ),
        }


class FakeCharacteristic:
    def __init__(self, properties: list[str]) -> None:
        self.properties = properties


class FakeServices:
    def __init__(self) -> None:
        self.characteristics = {
            SAKI_RX_UUID: FakeCharacteristic(["write"]),
            SAKI_TX_UUID: FakeCharacteristic(["notify"]),
        }

    def get_service(self, uuid: str) -> object | None:
        return object() if uuid == SAKI_SERVICE_UUID else None

    def get_characteristic(self, uuid: str) -> FakeCharacteristic | None:
        return self.characteristics.get(uuid)


class FakeClient:
    def __init__(self, mtu_size: int = 23, notify_security_failures: int = 0) -> None:
        self.is_connected = False
        self.mtu_size = mtu_size
        self.services = FakeServices()
        self.callback: Any = None
        self.writes: list[tuple[str, bytes, bool]] = []
        self.disconnect_count = 0
        self.stop_notify_count = 0
        self.start_notify_count = 0
        self.notify_security_failures = notify_security_failures

    async def connect(self) -> None:
        self.is_connected = True

    async def disconnect(self) -> None:
        self.is_connected = False
        self.disconnect_count += 1

    async def start_notify(self, characteristic: str, callback: Any) -> None:
        self.start_notify_count += 1
        if self.notify_security_failures > 0:
            self.notify_security_failures -= 1
            raise RuntimeError("Authentication is insufficient.")
        self.callback = callback

    async def stop_notify(self, characteristic: str) -> None:
        self.stop_notify_count += 1

    async def write_gatt_char(
        self,
        characteristic: str,
        data: bytes,
        *,
        response: bool,
    ) -> None:
        self.writes.append((characteristic, data, response))


class ClientFactory:
    def __init__(self, client: FakeClient) -> None:
        self.client = client
        self.kwargs: dict[str, Any] = {}

    def __call__(self, _device: object, **kwargs: Any) -> FakeClient:
        self.kwargs = kwargs
        return self.client


class BleTransportTests(unittest.IsolatedAsyncioTestCase):
    def test_bluetooth_unavailable_reasons_have_stable_diagnostics(self) -> None:
        class Reason:
            def __init__(self, name: str) -> None:
                self.name = name

        class UnavailableError(Exception):
            def __init__(self, reason: str) -> None:
                super().__init__("backend-specific detail", Reason(reason))

            @property
            def reason(self) -> Reason:
                return self.args[1]

        expected = {
            "NO_BLUETOOTH": "Bluetooth hardware is unavailable",
            "NO_BLE_CENTRAL_ROLE": "Bluetooth LE Central role is unavailable",
            "POWERED_OFF": "Bluetooth is powered off",
            "DENIED_BY_USER": "Bluetooth permission was denied by the user",
            "DENIED_BY_SYSTEM": "Bluetooth permission was denied by the system",
            "DENIED_BY_UNKNOWN": "Bluetooth permission was denied",
            "UNKNOWN": "Bluetooth is unavailable",
        }
        for reason, message in expected.items():
            with self.subTest(reason=reason):
                self.assertEqual(_error_detail(UnavailableError(reason)), message)

    async def test_discovery_always_filters_by_service_uuid(self) -> None:
        candidates = await discover_saki_devices(timeout=1.5, scanner_type=FakeScanner)

        self.assertEqual(FakeScanner.call["service_uuids"], [SAKI_SERVICE_UUID])
        self.assertTrue(FakeScanner.call["return_adv"])
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0].rssi, -41)

    async def test_write_payload_tracks_negotiated_att_mtu(self) -> None:
        for mtu, expected in ((23, 20), (64, 61), (185, 182), (256, 253)):
            with self.subTest(mtu=mtu):
                client = FakeClient(mtu_size=mtu)
                transport = BleByteTransport(
                    FakeDevice(),
                    client_factory=ClientFactory(client),
                )
                await transport.connect()
                self.assertEqual(transport.negotiated_att_mtu, mtu)
                self.assertEqual(transport.write_payload_bytes, expected)
                await transport.close()

    async def test_connect_validates_gatt_and_subscribes_before_writes(self) -> None:
        client = FakeClient(mtu_size=64)
        factory = ClientFactory(client)
        transport = BleByteTransport(FakeDevice(), client_factory=factory)

        await transport.connect()
        await transport.write(b"x" * 130)

        self.assertEqual(factory.kwargs["services"], [SAKI_SERVICE_UUID])
        self.assertFalse(factory.kwargs["pair"])
        self.assertIsNotNone(client.callback)
        self.assertEqual([len(item[1]) for item in client.writes], [61, 61, 8])
        self.assertTrue(all(item[0] == SAKI_RX_UUID and item[2] for item in client.writes))

    async def test_connect_waits_for_device_initiated_security(self) -> None:
        client = FakeClient(notify_security_failures=1)
        factory = ClientFactory(client)
        transport = BleByteTransport(
            FakeDevice(),
            client_factory=factory,
            connect_timeout=1.0,
            pair=True,
        )

        await transport.connect()

        self.assertTrue(factory.kwargs["pair"])
        self.assertEqual(client.start_notify_count, 2)
        await transport.close()

    async def test_blank_corebluetooth_error_still_has_a_diagnostic_name(self) -> None:
        class BlankErrorClient(FakeClient):
            async def connect(self) -> None:
                raise TimeoutError()

        transport = BleByteTransport(
            FakeDevice(),
            client_factory=ClientFactory(BlankErrorClient()),
        )

        with self.assertRaisesRegex(BleGattError, "TimeoutError"):
            await transport.connect()

    async def test_maximum_att_mtu_respects_firmware_write_bound(self) -> None:
        client = FakeClient(mtu_size=517)
        transport = BleByteTransport(FakeDevice(), client_factory=ClientFactory(client))

        await transport.connect()
        await transport.write(b"x" * 513)

        self.assertEqual([len(item[1]) for item in client.writes], [512, 1])

    async def test_notification_reader_preserves_bytes_and_bounds_reads(self) -> None:
        client = FakeClient()
        transport = BleByteTransport(FakeDevice(), client_factory=ClientFactory(client))
        await transport.connect()

        client.callback(object(), bytearray(b"abcdef"))

        self.assertEqual(await transport.read(2), b"ab")
        self.assertEqual(await transport.read(8), b"cdef")

    async def test_notification_overflow_fails_connection(self) -> None:
        client = FakeClient()
        transport = BleByteTransport(
            FakeDevice(),
            client_factory=ClientFactory(client),
            notify_queue_depth=1,
        )
        await transport.connect()

        client.callback(object(), bytearray(b"one"))
        client.callback(object(), bytearray(b"two"))

        with self.assertRaisesRegex(ByteTransportError, "overflow"):
            await transport.read(8)

    async def test_close_stops_notify_and_disconnects(self) -> None:
        client = FakeClient()
        transport = BleByteTransport(FakeDevice(), client_factory=ClientFactory(client))
        await transport.connect()

        await transport.close()

        self.assertEqual(client.stop_notify_count, 1)
        self.assertEqual(client.disconnect_count, 1)

    async def test_reconnect_discards_partial_notification_bytes(self) -> None:
        client = FakeClient()
        transport = BleByteTransport(FakeDevice(), client_factory=ClientFactory(client))
        await transport.connect()
        client.callback(object(), bytearray(b"partial"))
        self.assertEqual(await transport.read(2), b"pa")

        await transport.close()
        await transport.connect()
        client.callback(object(), bytearray(b"new"))

        self.assertEqual(await transport.read(8), b"new")


if __name__ == "__main__":
    unittest.main()
