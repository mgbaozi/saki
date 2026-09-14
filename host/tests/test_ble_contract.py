from __future__ import annotations

import re
import unittest
from pathlib import Path

from saki_host.ble_ids import SAKI_BLE_NAME, SAKI_RX_UUID, SAKI_SERVICE_UUID, SAKI_TX_UUID

REPO_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_IDS = REPO_ROOT / "firmware/components/saki_ble/include/saki_ble_ids.h"
SPEC = REPO_ROOT / "docs/versions/0.3.0/SPEC.md"
SDKCONFIG_DEFAULTS = REPO_ROOT / "firmware/sdkconfig.defaults"


class BleContractTests(unittest.TestCase):
    def test_host_firmware_and_spec_use_the_same_ids(self) -> None:
        header = FIRMWARE_IDS.read_text(encoding="utf-8")
        spec = SPEC.read_text(encoding="utf-8")
        expected = {
            "SAKI_BLE_NAME": SAKI_BLE_NAME,
            "SAKI_BLE_SERVICE_UUID": SAKI_SERVICE_UUID,
            "SAKI_BLE_RX_UUID": SAKI_RX_UUID,
            "SAKI_BLE_TX_UUID": SAKI_TX_UUID,
        }

        for macro, value in expected.items():
            match = re.search(rf'^#define {macro} "([^"]+)"$', header, re.MULTILINE)
            self.assertIsNotNone(match, macro)
            self.assertEqual(match.group(1), value)
            if macro != "SAKI_BLE_NAME":
                self.assertIn(f"`{value}`", spec)

    def test_firmware_defaults_enforce_the_reviewed_security_profile(self) -> None:
        config = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8").splitlines()
        enabled = {
            "CONFIG_BT_NIMBLE_ENABLED=y",
            "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y",
            "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y",
            "CONFIG_BT_NIMBLE_GATT_SERVER=y",
            "CONFIG_BT_NIMBLE_SECURITY_ENABLE=y",
            "CONFIG_BT_NIMBLE_SM_SC=y",
            "CONFIG_BT_NIMBLE_SM_LVL=2",
            "CONFIG_BT_NIMBLE_SM_SC_ONLY=0",
            "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
            "CONFIG_BT_NIMBLE_MAX_BONDS=1",
            "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
        }
        disabled = {
            "# CONFIG_BT_BLUEDROID_ENABLED is not set",
            "# CONFIG_BT_NIMBLE_ROLE_CENTRAL is not set",
            "# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set",
            "# CONFIG_BT_NIMBLE_GATT_CLIENT is not set",
            "# CONFIG_BT_NIMBLE_SM_LEGACY is not set",
            "# CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS is not set",
            "# CONFIG_BT_NIMBLE_HANDLE_REPEAT_PAIRING_DELETION is not set",
        }

        self.assertTrue(enabled.issubset(config), enabled.difference(config))
        self.assertTrue(disabled.issubset(config), disabled.difference(config))


if __name__ == "__main__":
    unittest.main()
