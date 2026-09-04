"""Observe physical Saki USB reconnects without opening the serial port."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import time
from pathlib import Path

from serial.tools import list_ports

DEFAULT_HOST_LOG = Path.home() / "Library/Logs/Saki/host.stdout.log"
DEFAULT_PRODUCT = "Saki Agent Display"


def device_present(serial_number: str, product: str) -> bool:
    return any(
        port.serial_number == serial_number and port.product == product
        for port in list_ports.comports()
    )


def monitor(
    *,
    serial_number: str,
    product: str,
    cycles: int,
    timeout: float,
    poll_interval: float,
    host_log: Path | None,
) -> int:
    deadline = time.monotonic() + timeout
    completed = 0
    removed_at: float | None = None
    enumerated_at: float | None = None
    host_connected_at: float | None = None
    present = device_present(serial_number, product)

    print(f"monitor ready present={present} target_cycles={cycles}", flush=True)
    log_context = (
        host_log.open("r", encoding="utf-8", errors="replace")
        if host_log is not None
        else nullcontext(None)
    )
    with log_context as log:
        if log is not None:
            log.seek(0, 2)
        while completed < cycles and time.monotonic() < deadline:
            now = time.monotonic()
            found = device_present(serial_number, product)
            if present and not found:
                removed_at = now
                enumerated_at = None
                host_connected_at = None
                print(f"cycle={completed + 1}/{cycles} device_removed", flush=True)
            elif not present and found and removed_at is not None:
                enumerated_at = now
                print(
                    f"cycle={completed + 1}/{cycles} device_enumerated "
                    f"off_ms={(enumerated_at - removed_at) * 1000:.1f}",
                    flush=True,
                )
                if log is None:
                    completed += 1
                    print(f"cycle={completed}/{cycles} enumeration_recovered", flush=True)
                    removed_at = None
                    enumerated_at = None
            present = found

            if log is not None:
                for line in log.readlines():
                    if removed_at is not None and f"connected device={serial_number}" in line:
                        host_connected_at = time.monotonic()

            if enumerated_at is not None and host_connected_at is not None:
                completed += 1
                print(
                    f"cycle={completed}/{cycles} host_reconnected "
                    f"after_enumeration_ms="
                    f"{max(0.0, (host_connected_at - enumerated_at) * 1000):.1f}",
                    flush=True,
                )
                removed_at = None
                enumerated_at = None
                host_connected_at = None

            time.sleep(poll_interval)

    if completed == cycles:
        print(f"monitor complete cycles={cycles} failures=0", flush=True)
        return 0
    print(f"monitor timeout completed={completed}/{cycles}", flush=True)
    return 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="USB serial number")
    parser.add_argument("--product", default=DEFAULT_PRODUCT, help="USB product string")
    parser.add_argument("--cycles", type=int, default=3, help="physical unplug/replug cycles")
    parser.add_argument("--timeout", type=float, default=900, help="overall timeout in seconds")
    parser.add_argument("--poll", type=float, default=0.02, help="poll interval in seconds")
    parser.add_argument("--host-log", type=Path, default=DEFAULT_HOST_LOG)
    parser.add_argument(
        "--enumeration-only",
        action="store_true",
        help="observe USB removal/re-enumeration without reading the Host log",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.cycles <= 0 or args.timeout <= 0 or args.poll <= 0:
        raise SystemExit("--cycles, --timeout and --poll must be greater than zero")
    return monitor(
        serial_number=args.serial,
        product=args.product,
        cycles=args.cycles,
        timeout=args.timeout,
        poll_interval=args.poll,
        host_log=None if args.enumeration_only else args.host_log,
    )


if __name__ == "__main__":
    raise SystemExit(main())
