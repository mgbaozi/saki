from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from saki_host.ble_binding import BleBinding, BleBindingError, BleBindingStore


class BleBindingStoreTests(unittest.TestCase):
    def test_missing_binding_is_not_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = BleBindingStore(Path(directory) / "binding.json")

            self.assertIsNone(store.load())

    def test_round_trip_preserves_core_bluetooth_and_device_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "binding.json"
            store = BleBindingStore(path)
            binding = BleBinding(
                "00000000-0000-4000-8000-000000000001",
                "0123456789ab",
            )

            store.save(binding)

            self.assertEqual(store.load(), binding)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_malformed_or_untrusted_identity_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "binding.json"
            path.write_text(json.dumps({"identifier": "x", "device_id": "secret"}))

            with self.assertRaises(BleBindingError):
                BleBindingStore(path).load()


if __name__ == "__main__":
    unittest.main()
