#!/usr/bin/env python3
"""Explicit hardware probe; stop the resident Host before running, restore after.

Only synthetic source events and allowlisted metrics are used. No raw device ID,
BLE identifier, source content or client configuration is printed or saved.
"""

from __future__ import annotations

import argparse
import asyncio
import copy
import json
import os
import signal
import statistics
import sys
import tempfile
import time
from pathlib import Path

from saki_host.adapters import SourceKind, normalize_hook
from saki_host.display import encode_projection, projection
from saki_host.protocol import encode_frame
from saki_host.protocol_session import DeviceReplyError
from saki_host.service import SakiHostService, ServiceConfig
from saki_host.sessions import SessionRegistry


def view_at(elapsed: float = 0) -> dict:
    registry = SessionRegistry(stale_after=0)
    for index, (source, final) in enumerate(
        [
            (SourceKind.CODEX, "PermissionRequest"),
            (SourceKind.CLAUDE_CODE, "PreToolUse"),
            (SourceKind.CODEX, "PostToolUse"),
            (SourceKind.CLAUDE_CODE, "Stop"),
        ]
    ):
        for ns, name in enumerate(["UserPromptSubmit", final], 1):
            event = normalize_hook(
                source,
                name,
                {"session_id": f"synthetic-probe-{index}"},
                b"t" * 32,
                emitted_ns=ns,
            )
            registry.accept(event, now=0)
    return projection(registry, elapsed)


def summary(values: list[float]) -> dict:
    ordered = sorted(values)
    return {
        "count": len(values),
        "min": round(ordered[0], 2),
        "mean": round(statistics.mean(values), 2),
        "p95": round(ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))], 2),
        "max": round(ordered[-1], 2),
    }


def public_metrics(pong: dict) -> dict:
    return {
        key: value
        for key, value in pong.get("runtime", {}).items()
        if key
        in {
            "internal_free_bytes",
            "internal_min_bytes",
            "heap_free_bytes",
            "heap_min_bytes",
            "app_stack_min_bytes",
            "ui_stack_min_bytes",
            "usb_stack_min_bytes",
            "ble_stack_min_bytes",
            "ble_rx_drops",
            "ble_tx_drops",
            "transport_switches",
            "transport_rejections",
        }
        and type(value) is int
    }


async def contracts(session) -> None:
    for count in (0, 1, 2, 4):
        value = view_at()
        value["sessions"] = value["sessions"][:count]
        value["total"] = count
        await session.apply_projection(value)
    message = encode_projection(session.codec, view_at())
    ack = await session.request(message, "ack")
    assert ack.get("applied") is True
    replay = await session.request(message, "ack")
    assert replay.get("applied") is False and replay.get("committed") is True
    conflicting = copy.deepcopy(message)
    conflicting["sessions"][0]["elapsed_ms"] += 1
    replay = await session.request(conflicting, "ack")
    assert replay.get("applied") is False and replay.get("committed") is False
    for mutate in (
        lambda value: value["sessions"].__setitem__(1, value["sessions"][0]),
        lambda value: value["sessions"][0].__setitem__("run_id", "x" * 33),
        lambda value: value.__setitem__("total", 33),
    ):
        bad = encode_projection(session.codec, view_at())
        mutate(bad)
        try:
            await session.request(bad, "ack")
        except DeviceReplyError as exc:
            assert exc.code == "invalid_field"
        else:
            raise AssertionError("invalid candidate accepted")
        pong = await session.ping()
        assert pong.get("last_seq") == message["seq"]
    try:
        await session.apply_clear()
    except DeviceReplyError as exc:
        assert exc.code == "invalid_field"
    else:
        raise AssertionError("legacy clear accepted in multi mode")
    # Exercise the BLE minimum payload size on either byte stream. No newline
    # means no commit; the rest of this exact request is sent by request().
    split = encode_projection(session.codec, view_at())
    frame = encode_frame(split)
    await session.write_raw(frame[:20])
    await asyncio.sleep(0.1)
    await session.write_raw(frame[20:])
    replay = await session.request(split, "ack")
    assert replay.get("committed") is True and replay.get("last_seq") == split["seq"]
    await session.apply_projection(view_at())


async def run(args) -> dict:
    service = SakiHostService(ServiceConfig())
    link = None
    timings = []
    try:
        link = await (
            service._connect_usb_link()
            if args.transport == "usb"
            else service._connect_ble_link()
        )
        assert link.multi_session, "multi-session capability missing"
        hello = await link.session.handshake()
        firmware = hello["device"]["fw"]
        assert firmware in {"0.4.0-dev", "0.4.0"}, "unexpected firmware"
        await link.session.enable_multi_session()
        before = await link.session.ping()
        await contracts(link.session)
        print(
            json.dumps(
                {
                    "phase": "contracts",
                    "ok": True,
                    "transport": args.transport,
                    "firmware": firmware,
                }
            ),
            flush=True,
        )
        started = time.monotonic()
        next_ping = started
        next_progress = started + 30
        latest = before
        while True:
            now = time.monotonic()
            view = view_at(now - started)
            sent = time.monotonic()
            await link.session.apply_projection(view)
            timings.append((time.monotonic() - sent) * 1000)
            if now >= next_ping:
                latest = await link.session.ping()
                next_ping = now + 5
            if now >= next_progress:
                print(
                    json.dumps(
                        {
                            "phase": "soak",
                            "elapsed_s": round(now - started),
                            "frames": len(timings),
                            "runtime": public_metrics(latest),
                        }
                    ),
                    flush=True,
                )
                next_progress = now + 30
            remaining = args.duration - (time.monotonic() - started)
            if remaining <= 0:
                break
            # Keep a 1 Hz schedule including ACK time instead of sleeping one
            # additional second after every request.
            await asyncio.sleep(
                min(max(0, started + len(timings) - time.monotonic()), remaining)
            )
        latest = await link.session.ping()
        baseline = before.get("diagnostics", {})
        delta = {
            key: value - baseline.get(key, 0)
            for key, value in latest.get("diagnostics", {}).items()
            if type(value) is int
        }
        # The 4 intentional invalid candidates must not disconnect or starve UI.
        assert delta.get("tx_drops", 0) == 0 and delta.get("heartbeat_timeouts", 0) == 0
        return {
            "ok": True,
            "transport": args.transport,
            "firmware": firmware,
            "duration_s": round(time.monotonic() - started, 1),
            "ack_ms": summary(timings),
            "runtime": public_metrics(latest),
            "diagnostics_delta": delta,
            "source": "synthetic 2 Codex + 2 Claude Code",
            "visual_and_touch_verified": False,
        }
    finally:
        if link is not None:
            await link.close()


async def ipc_probe() -> dict:
    """Test real hook/Host processes against USB, with an isolated private registry."""
    with tempfile.TemporaryDirectory(prefix="saki-device-ipc-") as directory:
        root = Path(directory)
        socket_path = root / "hooks.sock"
        state_dir = root / "state"
        key = state_dir / "identity.key"
        log_path = root / "host.log"
        fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as log:
            process = await asyncio.create_subprocess_exec(
                sys.executable,
                "-m",
                "saki_host",
                "serve",
                "--transport",
                "usb",
                "--state-dir",
                str(state_dir),
                "--socket",
                str(socket_path),
                stdout=log,
                stderr=log,
            )
            try:
                for _ in range(100):
                    if socket_path.exists() and key.exists():
                        break
                    assert process.returncode is None, "isolated Host exited"
                    await asyncio.sleep(0.1)
                else:
                    raise AssertionError("isolated Host did not create IPC")
                for index, (source, final) in enumerate(
                    [
                        ("codex", "PermissionRequest"),
                        ("claude_code", "PreToolUse"),
                        ("codex", "PostToolUse"),
                        ("claude_code", "Stop"),
                    ]
                ):
                    for event_name in ("UserPromptSubmit", final):
                        hook = await asyncio.create_subprocess_exec(
                            sys.executable,
                            "-m",
                            "saki_host",
                            "hook",
                            "--source",
                            source,
                            "--identity-key",
                            str(key),
                            "--socket",
                            str(socket_path),
                            event_name,
                            stdin=asyncio.subprocess.PIPE,
                            stdout=asyncio.subprocess.PIPE,
                            stderr=asyncio.subprocess.PIPE,
                        )
                        out, err = await asyncio.wait_for(
                            hook.communicate(
                                json.dumps(
                                    {
                                        "session_id": f"synthetic-ipc-{index}",
                                        "tool_name": "Read",
                                    }
                                ).encode()
                            ),
                            timeout=3,
                        )
                        assert hook.returncode == 0 and not out and not err
                checkpoint = state_dir / "sessions.json"
                saved = None
                for _ in range(100):
                    if checkpoint.exists():
                        saved = json.loads(checkpoint.read_text())
                        if (
                            len(saved["records"]) == 4
                            and "sessions=4" in log_path.read_text()
                        ):
                            break
                    assert process.returncode is None
                    await asyncio.sleep(0.1)
                else:
                    raise AssertionError("four-session projection not acknowledged")
                assert sorted(item["event"]["source"] for item in saved["records"]) == [
                    "claude_code",
                    "claude_code",
                    "codex",
                    "codex",
                ]
                retained = {item["event"]["session_id"] for item in saved["records"]}
                removed = saved["records"][-1]["event"]["session_id"]
                clear = await asyncio.create_subprocess_exec(
                    sys.executable,
                    "-m",
                    "saki_host",
                    "sessions",
                    "forget",
                    removed,
                    "--socket",
                    str(socket_path),
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                )
                out, err = await asyncio.wait_for(clear.communicate(), timeout=3)
                assert (
                    clear.returncode == 0 and not err and json.loads(out)["requested"]
                )
                for _ in range(100):
                    saved = json.loads(checkpoint.read_text())
                    if (
                        len(saved["records"]) == 3
                        and "sessions=3" in log_path.read_text()
                        and removed
                        not in {
                            item["event"]["session_id"] for item in saved["records"]
                        }
                    ):
                        break
                    await asyncio.sleep(0.1)
                else:
                    raise AssertionError("display-only deletion not acknowledged")
                assert {
                    item["event"]["session_id"] for item in saved["records"]
                } == retained - {removed}
                return {
                    "ok": True,
                    "phase": "hook_ipc_host_device",
                    "hook_processes": 8,
                    "sources": 2,
                    "initial_sessions": 4,
                    "remaining_sessions": 3,
                    "hook_output_empty": True,
                    "user_configuration_modified": False,
                    "model_api_calls": 0,
                }
            finally:
                if process.returncode is None:
                    process.send_signal(signal.SIGINT)
                    try:
                        await asyncio.wait_for(process.wait(), timeout=5)
                    except TimeoutError:
                        process.kill()
                        await process.wait()


async def handoffs(count: int) -> dict:
    """One Host/codec; close CDC to exercise DTR grace, never cut board power."""
    service = SakiHostService(ServiceConfig())
    usb = ble = None
    to_ble, to_usb = [], []
    epoch = time.monotonic()
    try:
        usb = await service._connect_usb_link()
        assert usb.multi_session
        identity = usb.device_id
        await usb.session.apply_projection(view_at(time.monotonic() - epoch))
        before = await usb.session.ping()
        latest = before
        for iteration in range(count):
            started = time.monotonic()
            ble = await service._connect_ble_link()
            assert ble.multi_session and ble.device_id == identity
            try:
                await ble.session.apply_projection(view_at(time.monotonic() - epoch))
            except DeviceReplyError as exc:
                assert exc.code == "busy"
            else:
                raise AssertionError("BLE bypassed USB priority")
            await usb.close()
            usb = None
            await asyncio.sleep(2.2)
            await ble.session.apply_projection(view_at(time.monotonic() - epoch))
            to_ble.append((time.monotonic() - started) * 1000)
            started = time.monotonic()
            usb = await service._connect_usb_link()
            assert usb.multi_session and usb.device_id == identity
            await usb.session.apply_projection(view_at(time.monotonic() - epoch))
            to_usb.append((time.monotonic() - started) * 1000)
            await ble.close()
            ble = None
            latest = await usb.session.ping()
            print(
                json.dumps(
                    {
                        "phase": "handoff",
                        "cycle": iteration + 1,
                        "usb_to_ble_ms": round(to_ble[-1], 2),
                        "ble_to_usb_ms": round(to_usb[-1], 2),
                        "runtime": public_metrics(latest),
                    }
                ),
                flush=True,
            )
        runtime = public_metrics(latest)
        assert runtime.get("ble_rx_drops", 0) == 0
        assert runtime.get("ble_tx_drops", 0) == 0
        assert (
            runtime.get("transport_switches", 0)
            - public_metrics(before).get("transport_switches", 0)
            == count * 2
        )
        return {
            "ok": True,
            "cycles": count,
            "usb_to_ble_ms": summary(to_ble),
            "ble_to_usb_ms": summary(to_usb),
            "runtime": runtime,
            "method": "CDC DTR close/reopen; USB power stays connected",
            "physical_unplug_verified": False,
        }
    finally:
        for link in (usb, ble):
            if link is not None:
                await link.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transport", choices=["usb", "ble"], default="usb")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument(
        "--handoffs", type=int, default=0, help="CDC/BLE round trips instead of soak"
    )
    parser.add_argument(
        "--ipc", action="store_true", help="isolated real hook/Host process test on USB"
    )
    args = parser.parse_args()
    if not 0 <= args.handoffs <= 100:
        parser.error("handoffs must be between 0 and 100")
    if not 0 <= args.duration <= 3600:
        parser.error("duration must be between 0 and 3600 seconds")
    try:
        result = asyncio.run(
            ipc_probe()
            if args.ipc
            else (handoffs(args.handoffs) if args.handoffs else run(args))
        )
    except Exception as exc:  # noqa: BLE001 - never expose device identifiers from drivers
        # Exceptions may contain a device path/identifier. Never echo the body.
        print(json.dumps({"ok": False, "error_type": type(exc).__name__}), flush=True)
        return 1
    print(json.dumps(result), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
