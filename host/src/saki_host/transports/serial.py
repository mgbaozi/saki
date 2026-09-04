from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Protocol, Self

import serial
from serial.tools import list_ports

from ..protocol import MAX_FRAME_BYTES, ProtocolError, decode_frame, encode_frame

SAKI_PRODUCT = "Saki Agent Display"
DEFAULT_BAUD_RATE = 115_200


class SerialSessionError(RuntimeError):
    """Base exception for the host-to-device serial session."""


class SerialSessionTimeout(SerialSessionError):
    """Raised when the device does not answer before the request deadline."""


class DeviceError(SerialSessionError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"device error {code}: {message}")
        self.code = code
        self.message = message


class SerialStream(Protocol):
    dtr: bool

    def read(self, size: int = 1) -> bytes: ...

    def write(self, data: bytes) -> int: ...

    def flush(self) -> None: ...

    def reset_input_buffer(self) -> None: ...

    def close(self) -> None: ...


@dataclass(frozen=True, slots=True)
class SerialCandidate:
    device: str
    description: str
    product: str | None
    serial_number: str | None
    vid: int | None
    pid: int | None

    @property
    def preferred_callout(self) -> bool:
        return self.device.startswith("/dev/cu.")

    @property
    def looks_like_saki(self) -> bool:
        return self.product == SAKI_PRODUCT or SAKI_PRODUCT in self.description


def list_candidates() -> list[SerialCandidate]:
    candidates = [
        SerialCandidate(
            device=port.device,
            description=port.description or "",
            product=port.product,
            serial_number=port.serial_number,
            vid=port.vid,
            pid=port.pid,
        )
        for port in list_ports.comports()
    ]
    return sorted(candidates, key=lambda item: (not item.preferred_callout, item.device))


def select_saki_port(explicit_port: str | None = None) -> str:
    if explicit_port:
        return explicit_port

    candidates = [candidate for candidate in list_candidates() if candidate.looks_like_saki]
    preferred = [candidate for candidate in candidates if candidate.preferred_callout]
    if len(preferred) == 1:
        return preferred[0].device
    if len(candidates) == 1:
        return candidates[0].device
    if not candidates:
        raise SerialSessionError(
            f"no serial device advertises product {SAKI_PRODUCT!r}; use --port to select one"
        )
    devices = ", ".join(candidate.device for candidate in candidates)
    raise SerialSessionError(f"multiple Saki serial devices found ({devices}); use --port")


class SerialSession:
    def __init__(self, stream: SerialStream) -> None:
        self._stream = stream
        self._receive_buffer = bytearray()

    @classmethod
    def open(cls, port: str, baud_rate: int = DEFAULT_BAUD_RATE) -> Self:
        try:
            stream = serial.Serial(
                port=port,
                baudrate=baud_rate,
                timeout=0.1,
                write_timeout=1.0,
                exclusive=True,
            )
            stream.dtr = True
            time.sleep(0.05)
            stream.reset_input_buffer()
        except (OSError, serial.SerialException) as exc:
            raise SerialSessionError(f"cannot open {port}: {exc}") from exc
        return cls(stream)

    def close(self) -> None:
        self._stream.close()

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def write_raw(self, data: bytes) -> None:
        """Write bounded test input without applying Host protocol validation."""
        try:
            written = self._stream.write(data)
            self._stream.flush()
        except (OSError, serial.SerialException) as exc:
            raise SerialSessionError(f"serial write failed: {exc}") from exc
        if written != len(data):
            raise SerialSessionError(f"short serial write: {written} of {len(data)} bytes")

    def drain_input(self, quiet_seconds: float = 0.25, timeout: float = 2.0) -> int:
        """Consume raw input until the device has been quiet for a bounded interval."""
        if quiet_seconds < 0 or timeout <= 0:
            raise ValueError("quiet_seconds must be non-negative and timeout must be positive")
        self._receive_buffer.clear()
        try:
            self._stream.reset_input_buffer()
            started = time.monotonic()
            quiet_deadline = started + quiet_seconds
            hard_deadline = started + timeout
            byte_count = 0
            while time.monotonic() < hard_deadline:
                chunk = self._stream.read(256)
                now = time.monotonic()
                if chunk:
                    byte_count += len(chunk)
                    quiet_deadline = now + quiet_seconds
                elif now >= quiet_deadline:
                    break
            self._stream.reset_input_buffer()
        except (OSError, serial.SerialException) as exc:
            raise SerialSessionError(f"cannot drain serial input: {exc}") from exc
        return byte_count

    def _read_message(self, deadline: float) -> dict[str, Any]:
        while True:
            newline = self._receive_buffer.find(b"\n")
            if newline >= 0:
                frame = bytes(self._receive_buffer[: newline + 1])
                del self._receive_buffer[: newline + 1]
                if frame.rstrip(b"\r\n"):
                    try:
                        return decode_frame(frame)
                    except ProtocolError as exc:
                        raise SerialSessionError(f"invalid device frame: {exc}") from exc

            if len(self._receive_buffer) > MAX_FRAME_BYTES:
                self._receive_buffer.clear()
                raise SerialSessionError("device frame exceeds protocol limit")

            if time.monotonic() >= deadline:
                raise SerialSessionTimeout("timed out waiting for device response")

            try:
                chunk = self._stream.read(256)
            except (OSError, serial.SerialException) as exc:
                raise SerialSessionError(f"serial read failed: {exc}") from exc
            if chunk:
                self._receive_buffer.extend(chunk)

    def request(
        self,
        message: dict[str, Any],
        expected_type: str,
        timeout: float = 2.0,
    ) -> dict[str, Any]:
        request_id = message.get("id")
        if not isinstance(request_id, int):
            raise SerialSessionError("outgoing request has no integer id")

        frame = encode_frame(message)
        self.write_raw(frame)

        deadline = time.monotonic() + timeout
        while True:
            response = self._read_message(deadline)
            if response.get("reply_to") != request_id:
                continue
            if response.get("type") == "error":
                code = response.get("code", "unknown_error")
                detail = response.get("message", "no diagnostic message")
                raise DeviceError(str(code), str(detail))
            if response.get("type") != expected_type:
                raise SerialSessionError(
                    f"request {request_id} expected {expected_type}, "
                    f"received {response.get('type')!r}"
                )
            return response

    def handshake(self, message: dict[str, Any], timeout: float = 3.0) -> dict[str, Any]:
        response = self.request(message, "hello", timeout)
        if response.get("role") != "device":
            raise SerialSessionError("hello response does not identify a device")
        device = response.get("device")
        screen = response.get("screen")
        if not isinstance(device, dict) or not isinstance(device.get("id"), str):
            raise SerialSessionError("hello response has invalid device metadata")
        if not isinstance(screen, dict) or not all(
            isinstance(screen.get(key), int) for key in ("width", "height")
        ):
            raise SerialSessionError("hello response has invalid screen metadata")
        return response

    def apply_status(self, message: dict[str, Any], timeout: float = 2.0) -> dict[str, Any]:
        response = self.request(message, "ack", timeout)
        if response.get("ok") is not True:
            raise SerialSessionError("device returned a negative acknowledgement")
        return response

    def apply_clear(self, message: dict[str, Any], timeout: float = 2.0) -> dict[str, Any]:
        response = self.request(message, "ack", timeout)
        if response.get("ok") is not True:
            raise SerialSessionError("device returned a negative acknowledgement")
        return response

    def ping(self, message: dict[str, Any], timeout: float = 2.0) -> dict[str, Any]:
        return self.request(message, "pong", timeout)
