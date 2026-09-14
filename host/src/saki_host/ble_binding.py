from __future__ import annotations

import json
import os
import re
from dataclasses import asdict, dataclass
from pathlib import Path

DEFAULT_BLE_BINDING_PATH = (
    Path.home() / "Library" / "Application Support" / "Saki" / "ble-binding.json"
)
_DEVICE_ID_PATTERN = re.compile(r"^[0-9a-f]{12}$")


class BleBindingError(ValueError):
    """The cached BLE identity is absent or invalid."""


@dataclass(frozen=True, slots=True)
class BleBinding:
    identifier: str
    device_id: str

    def validate(self) -> None:
        if not self.identifier or len(self.identifier) > 128 or not self.identifier.isascii():
            raise BleBindingError("BLE peripheral identifier is invalid")
        if not _DEVICE_ID_PATTERN.fullmatch(self.device_id):
            raise BleBindingError("BLE device identity is invalid")


class BleBindingStore:
    def __init__(self, path: Path = DEFAULT_BLE_BINDING_PATH) -> None:
        self.path = path

    def load(self) -> BleBinding | None:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return None
        except (OSError, json.JSONDecodeError) as exc:
            raise BleBindingError(f"cannot read BLE binding cache: {exc}") from exc
        if not isinstance(raw, dict):
            raise BleBindingError("BLE binding cache must be a JSON object")
        binding = BleBinding(
            identifier=str(raw.get("identifier", "")),
            device_id=str(raw.get("device_id", "")),
        )
        binding.validate()
        return binding

    def save(self, binding: BleBinding) -> None:
        binding.validate()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(f".{self.path.name}.tmp")
        try:
            temporary.write_text(
                json.dumps(asdict(binding), separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            os.chmod(temporary, 0o600)
            temporary.replace(self.path)
        except OSError as exc:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
            raise BleBindingError(f"cannot save BLE binding cache: {exc}") from exc
