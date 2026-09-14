"""Stable BLE identifiers shared by all Host BLE entry points."""

SAKI_BLE_NAME = "Saki"
SAKI_SERVICE_UUID = "9f6d0100-7c7a-4c3b-9d9a-73616b690001"
SAKI_RX_UUID = "9f6d0101-7c7a-4c3b-9d9a-73616b690001"
SAKI_TX_UUID = "9f6d0102-7c7a-4c3b-9d9a-73616b690001"

SAKI_BLE_UUIDS = frozenset(
    {
        SAKI_SERVICE_UUID,
        SAKI_RX_UUID,
        SAKI_TX_UUID,
    }
)
