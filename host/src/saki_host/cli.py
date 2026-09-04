from __future__ import annotations

import argparse
import asyncio
import json
import math
import platform
import resource
import statistics
import sys
import time
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path

from . import __version__
from .codex_hooks import snapshot_from_codex_hook
from .fuzz import build_fuzz_cases
from .ipc import DEFAULT_SOCKET_PATH, HookDeliveryError, deliver_snapshot
from .models import (
    Activity,
    ActivityKind,
    AgentInfo,
    AgentState,
    Progress,
    ProgressMode,
    StateSnapshot,
    TaskInfo,
    snapshot_from_payload,
)
from .protocol import (
    PROTOCOL_VERSION,
    ProtocolCodec,
    ProtocolError,
    decode_frame,
    encode_frame,
)
from .service import (
    SakiHostService,
    ServiceConfig,
    parse_device_diagnostics,
    parse_device_runtime,
)
from .transports.serial import (
    SAKI_PRODUCT,
    SerialSession,
    SerialSessionError,
    list_candidates,
    select_saki_port,
)

SANITIZED_REPLAY_ROOT = (
    Path(__file__).resolve().parents[3] / "protocol" / "fixtures" / "v1" / "sessions"
)
MIN_INTERNAL_HEAP_BYTES = 32 * 1024
MIN_TASK_STACK_BYTES = 1024
HOLD_HEARTBEAT_SECONDS = 5.0
SOAK_MAX_UPDATE_INTERVAL_SECONDS = 10.0


class ReplayError(ValueError):
    """Raised when a replay file is outside the sanitized fixture contract."""


def _hold_serial_session(
    session: SerialSession,
    codec: ProtocolCodec,
    hold: float,
    *,
    heartbeat_seconds: float = HOLD_HEARTBEAT_SECONDS,
) -> None:
    """Keep a manual CLI session alive while respecting the device watchdog."""
    remaining = hold
    while remaining > 0:
        delay = min(heartbeat_seconds, remaining)
        time.sleep(delay)
        remaining -= delay
        if remaining > 0:
            session.ping(codec.ping())


def _nearest_rank(values: list[float], percentile: float) -> float:
    ordered = sorted(values)
    rank = max(1, math.ceil((percentile / 100) * len(ordered)))
    return ordered[rank - 1]


def _latency_summary(values: list[float]) -> dict[str, float]:
    return {
        "min": min(values),
        "mean": statistics.fmean(values),
        "p50": _nearest_rank(values, 50),
        "p95": _nearest_rank(values, 95),
        "p99": _nearest_rank(values, 99),
        "max": max(values),
    }


def _host_max_rss_bytes() -> int:
    value = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    return value if platform.system() == "Darwin" else value * 1024


def _soak_snapshot(index: int, elapsed_ms: int) -> StateSnapshot:
    kinds = (ActivityKind.READ, ActivityKind.EDIT, ActivityKind.SHELL, ActivityKind.TEST)
    kind = kinds[(index - 1) % len(kinds)]
    detail = "长时间测试详情：中文、ASCII 与 UTF-8 边界。" if index % 10 == 0 else ""
    return StateSnapshot(
        state=AgentState.WORKING if index % 2 else AgentState.THINKING,
        task=TaskInfo("soak", "Saki 24 小时稳定性测试"),
        activity=Activity(kind, f"状态更新 {index}", detail),
        progress=Progress(ProgressMode.DETERMINATE, index % 101, "Soak"),
        elapsed_ms=elapsed_ms,
        agent=AgentInfo("Codex", "soak-test"),
    )


def _write_soak_report(path: Path, report: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def _demo_snapshots() -> list[StateSnapshot]:
    task = TaskInfo("demo-task", "实现 Saki Agent 状态同步")
    agent = AgentInfo("Codex", "gpt-5.6")
    return [
        StateSnapshot(state=AgentState.IDLE, agent=agent),
        StateSnapshot(
            state=AgentState.STARTING,
            task=task,
            activity=Activity(ActivityKind.PLAN, "正在开始任务"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=0,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.WORKING,
            task=task,
            activity=Activity(ActivityKind.SHELL, "正在运行测试"),
            progress=Progress(ProgressMode.DETERMINATE, 62, "Tests"),
            elapsed_ms=42_000,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.WAITING_APPROVAL,
            task=task,
            activity=Activity(ActivityKind.SHELL, "等待操作批准"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=51_000,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.COMPLETED,
            task=task,
            activity=Activity(ActivityKind.MESSAGE, "任务已完成"),
            progress=Progress(ProgressMode.DETERMINATE, 100),
            elapsed_ms=75_000,
            agent=agent,
        ),
    ]


def _serial_demo_snapshots() -> list[StateSnapshot]:
    task = TaskInfo("serial-demo", "Saki serial link")
    agent = AgentInfo("Codex", "gpt-5.6")
    return [
        StateSnapshot(state=AgentState.IDLE, agent=agent),
        StateSnapshot(
            state=AgentState.STARTING,
            task=task,
            activity=Activity(ActivityKind.PLAN, "Starting task"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=0,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.WORKING,
            task=task,
            activity=Activity(ActivityKind.TEST, "Testing USB protocol"),
            progress=Progress(ProgressMode.DETERMINATE, 62, "Tests"),
            elapsed_ms=42_000,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.WAITING_APPROVAL,
            task=task,
            activity=Activity(ActivityKind.SHELL, "Waiting for approval"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=51_000,
            agent=agent,
        ),
        StateSnapshot(
            state=AgentState.COMPLETED,
            task=task,
            activity=Activity(ActivityKind.MESSAGE, "Task completed"),
            progress=Progress(ProgressMode.DETERMINATE, 100),
            elapsed_ms=75_000,
            agent=agent,
        ),
    ]


def _write(message: dict[str, object]) -> None:
    sys.stdout.buffer.write(encode_frame(message))


def _run_demo() -> int:
    codec = ProtocolCodec(session="00000000-0000-4000-8000-000000000001")
    _write(codec.hello())
    for snapshot in _demo_snapshots():
        _write(codec.status(snapshot))
    return 0


def _run_hook(
    event_name: str,
    socket_path: Path,
    write_stdout: bool,
    strict: bool,
) -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError as exc:
        print(f"invalid hook JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(payload, dict):
        print("hook payload must be a JSON object", file=sys.stderr)
        return 2
    snapshot = snapshot_from_codex_hook(event_name, payload)
    if write_stdout:
        codec = ProtocolCodec()
        _write(codec.status(snapshot))
        return 0
    try:
        deliver_snapshot(snapshot, socket_path)
    except HookDeliveryError as exc:
        if strict:
            print(str(exc), file=sys.stderr)
            return 1
    return 0


def _run_service(
    port: str | None,
    socket_path: Path,
    heartbeat: float,
    stale_after: float,
) -> int:
    config = ServiceConfig(
        port=port,
        socket_path=socket_path,
        heartbeat_seconds=heartbeat,
        stale_after_seconds=stale_after,
    )
    try:
        asyncio.run(SakiHostService(config).run())
    except KeyboardInterrupt:
        return 0
    except (OSError, HookDeliveryError, SerialSessionError) as exc:
        print(f"service failed: {exc}", file=sys.stderr)
        return 1
    return 0


def _run_serial_list() -> int:
    candidates = list_candidates()
    if not candidates:
        print("No serial devices found.")
        return 0
    for candidate in candidates:
        marker = "saki?" if candidate.looks_like_saki else "serial"
        vid_pid = (
            f"{candidate.vid:04x}:{candidate.pid:04x}"
            if candidate.vid is not None and candidate.pid is not None
            else "----:----"
        )
        print(
            f"{marker:6} {candidate.device} {vid_pid} "
            f"product={candidate.product or '-'} serial={candidate.serial_number or '-'}"
        )
    return 0


def _run_doctor(port: str | None) -> int:
    print(
        f"PASS host: saki-host {__version__}, Python "
        f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}, "
        f"protocol v{PROTOCOL_VERSION}"
    )

    candidates = list_candidates()
    if port is None:
        saki_candidates = [candidate for candidate in candidates if candidate.looks_like_saki]
        if not saki_candidates:
            if candidates:
                print(
                    f"FAIL serial: {len(candidates)} serial device(s) are visible, but none "
                    f"advertise product {SAKI_PRODUCT!r}"
                )
                print("Hint: start the application firmware, then run `saki-host serial list`.")
            else:
                print("FAIL serial: no serial devices are visible")
                print("Hint: connect the device and start the application firmware.")
            return 1
    else:
        candidate = next((item for item in candidates if item.device == port), None)
        if candidate is not None and not candidate.looks_like_saki:
            identity = candidate.product or candidate.description or "unknown product"
            print(f"FAIL serial: {port} advertises {identity!r}, not {SAKI_PRODUCT!r}")
            print("Hint: this may be the ROM download/debug port; press RST without holding K0.")
            return 1
        if candidate is None:
            print(f"WARN serial: {port} is not in the current serial device list; probing it anyway")

    try:
        selected_port = select_saki_port(port)
        print(f"PASS serial: selected {selected_port}")
        started = time.monotonic()
        codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            hello = session.handshake(codec.hello())
            handshake_ms = (time.monotonic() - started) * 1000
            ping_started = time.monotonic()
            pong = session.ping(codec.ping())
            ping_ms = (time.monotonic() - ping_started) * 1000
    except SerialSessionError as exc:
        cause = exc.__cause__
        if isinstance(cause, ProtocolError) and str(cause) == "unsupported_version":
            print(
                f"FAIL protocol: the device uses an incompatible protocol version; "
                f"Host requires v{PROTOCOL_VERSION}"
            )
        elif str(exc).startswith("cannot open "):
            print(f"FAIL open: {exc}")
            print(
                "Hint: another Host may own the port; run "
                "`scripts/saki-service.zsh stop` before an exclusive diagnostic."
            )
        else:
            print(f"FAIL handshake: {exc}")
            print("Hint: reset the device and confirm it is running Saki application firmware.")
        return 1

    device = hello["device"]
    screen = hello["screen"]
    print(
        f"PASS handshake: device={device['name']} id={device['id']} firmware={device['fw']} "
        f"screen={screen['width']}x{screen['height']} handshake_ms={handshake_ms:.1f}"
    )
    print(
        f"PASS heartbeat: uptime_ms={pong.get('uptime_ms')} "
        f"last_seq={pong.get('last_seq')} ping_ms={ping_ms:.1f} "
        f"diagnostics={pong.get('diagnostics', {})}"
    )
    runtime = parse_device_runtime(pong)
    if runtime is None:
        print("WARN runtime: metrics unavailable; use firmware 0.2.0-dev or newer")
    else:
        stack_metrics = {
            key: runtime[key]
            for key in (
                "app_stack_min_bytes",
                "ui_stack_min_bytes",
                "usb_stack_min_bytes",
            )
        }
        failures: list[str] = []
        if runtime["internal_min_bytes"] < MIN_INTERNAL_HEAP_BYTES:
            failures.append(
                f"internal_min_bytes={runtime['internal_min_bytes']}<{MIN_INTERNAL_HEAP_BYTES}"
            )
        failures.extend(
            f"{key}={value}<{MIN_TASK_STACK_BYTES}"
            for key, value in stack_metrics.items()
            if value < MIN_TASK_STACK_BYTES
        )
        summary = " ".join(f"{key}={value}" for key, value in runtime.items())
        if failures:
            print(f"FAIL runtime: {summary}")
            print(f"Safety limits: {', '.join(failures)}")
            print("RESULT: unhealthy")
            return 1
        print(f"PASS runtime: {summary}")
    print("RESULT: healthy")
    return 0


def _run_serial_demo(port: str | None, interval: float, hold: float) -> int:
    try:
        selected_port = select_saki_port(port)
        codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            hello = session.handshake(codec.hello())
            device = hello["device"]
            screen = hello["screen"]
            print(
                f"Connected to {device['name']} {device['id']} on {selected_port} "
                f"({screen['width']}x{screen['height']}, firmware {device['fw']})"
            )
            for snapshot in _serial_demo_snapshots():
                ack = session.apply_status(codec.status(snapshot))
                print(
                    f"state={snapshot.state.value:18} applied={ack.get('applied')} "
                    f"last_seq={ack.get('last_seq')}"
                )
                if interval:
                    time.sleep(interval)
            pong = session.ping(codec.ping())
            print(f"pong uptime_ms={pong.get('uptime_ms')} last_seq={pong.get('last_seq')}")
            if hold:
                _hold_serial_session(session, codec, hold)
    except SerialSessionError as exc:
        print(f"serial demo failed: {exc}", file=sys.stderr)
        return 1
    return 0


def _run_serial_cycle(port: str | None, count: int, interval: float) -> int:
    durations_ms: list[float] = []

    for cycle in range(1, count + 1):
        try:
            selected_port = select_saki_port(port)
            codec = ProtocolCodec()
            started = time.monotonic()
            with SerialSession.open(selected_port) as session:
                hello = session.handshake(codec.hello())
            duration_ms = (time.monotonic() - started) * 1000
            durations_ms.append(duration_ms)
            device = hello["device"]
            print(
                f"cycle={cycle}/{count} device={device['id']} firmware={device['fw']} "
                f"handshake_ms={duration_ms:.1f}"
            )
        except SerialSessionError as exc:
            print(f"serial cycle {cycle}/{count} failed: {exc}", file=sys.stderr)
            return 1
        if cycle < count and interval:
            time.sleep(interval)

    print(
        f"completed cycles={count} failures=0 "
        f"handshake_ms_min={min(durations_ms):.1f} "
        f"handshake_ms_avg={sum(durations_ms) / len(durations_ms):.1f} "
        f"handshake_ms_max={max(durations_ms):.1f}"
    )
    return 0


def _run_serial_soak(
    port: str | None,
    count: int,
    duration: float,
    sample_interval: float,
    report_path: Path,
) -> int:
    started_at = datetime.now(UTC).isoformat()
    selected_port = ""
    device: dict[str, object] = {}
    completed = 0
    ack_latencies_ms: list[float] = []
    samples: list[dict[str, object]] = []
    error: str | None = None
    interrupted = False
    run_started = time.monotonic()

    try:
        selected_port = select_saki_port(port)
        codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            hello = session.handshake(codec.hello())
            raw_device = hello.get("device")
            if isinstance(raw_device, dict):
                device = dict(raw_device)
            run_started = time.monotonic()
            initial_pong = session.ping(codec.ping())
            samples.append(
                {
                    "elapsed_seconds": 0.0,
                    "completed_updates": 0,
                    "diagnostics": parse_device_diagnostics(initial_pong),
                    "runtime": parse_device_runtime(initial_pong),
                }
            )
            next_sample = run_started + sample_interval
            progress_interval = max(1, count // 20)

            for index in range(1, count + 1):
                target = (
                    run_started + duration * ((index - 1) / (count - 1))
                    if count > 1
                    else run_started
                )
                delay = target - time.monotonic()
                if delay > 0:
                    time.sleep(delay)

                before_ack = time.monotonic()
                ack = session.apply_status(
                    codec.status(_soak_snapshot(index, int((before_ack - run_started) * 1000)))
                )
                after_ack = time.monotonic()
                if ack.get("applied") is not True:
                    raise SerialSessionError(f"soak update {index} was not applied")
                ack_latencies_ms.append((after_ack - before_ack) * 1000)
                completed = index

                should_sample = after_ack >= next_sample or index == count
                if should_sample:
                    pong = session.ping(codec.ping())
                    samples.append(
                        {
                            "elapsed_seconds": time.monotonic() - run_started,
                            "completed_updates": completed,
                            "diagnostics": parse_device_diagnostics(pong),
                            "runtime": parse_device_runtime(pong),
                        }
                    )
                    while next_sample <= after_ack:
                        next_sample += sample_interval

                if index % progress_interval == 0 or index == count:
                    print(
                        f"soak progress={index}/{count} "
                        f"elapsed_seconds={time.monotonic() - run_started:.1f}",
                        flush=True,
                    )
    except KeyboardInterrupt:
        interrupted = True
        error = "interrupted"
    except (OSError, ValueError, SerialSessionError) as exc:
        error = str(exc)

    diagnostics_start = samples[0].get("diagnostics") if samples else None
    diagnostics_end = samples[-1].get("diagnostics") if samples else None
    diagnostic_delta: dict[str, int] | None = None
    if isinstance(diagnostics_start, dict) and isinstance(diagnostics_end, dict):
        diagnostic_delta = {
            key: max(0, int(diagnostics_end[key]) - int(diagnostics_start[key]))
            for key in diagnostics_start
        }

    final_runtime = samples[-1].get("runtime") if samples else None
    runtime_safe = isinstance(final_runtime, dict) and (
        int(final_runtime["internal_min_bytes"]) >= MIN_INTERNAL_HEAP_BYTES
        and all(
            int(final_runtime[key]) >= MIN_TASK_STACK_BYTES
            for key in (
                "app_stack_min_bytes",
                "ui_stack_min_bytes",
                "usb_stack_min_bytes",
            )
        )
    )
    diagnostics_clean = isinstance(diagnostic_delta, dict) and all(
        diagnostic_delta.get(key, 0) == 0
        for key in (
            "invalid_frames",
            "oversized_frames",
            "old_sequences",
            "ui_queue_overwrites",
            "tx_drops",
            "heartbeat_timeouts",
        )
    )
    passed = error is None and completed == count and diagnostics_clean and runtime_safe
    finished_at = datetime.now(UTC).isoformat()
    report: dict[str, object] = {
        "schema_version": 1,
        "result": "pass" if passed else "fail",
        "error": error,
        "started_at_utc": started_at,
        "finished_at_utc": finished_at,
        "requested_duration_seconds": duration,
        "actual_duration_seconds": time.monotonic() - run_started,
        "requested_updates": count,
        "completed_updates": completed,
        "sample_interval_seconds": sample_interval,
        "port": selected_port,
        "device": device,
        "ack_latency_ms": _latency_summary(ack_latencies_ms) if ack_latencies_ms else None,
        "diagnostic_delta": diagnostic_delta,
        "runtime_safe": runtime_safe,
        "host_max_rss_bytes": _host_max_rss_bytes(),
        "samples": samples,
    }
    _write_soak_report(report_path, report)

    if passed:
        latency = report["ack_latency_ms"]
        assert isinstance(latency, dict)
        print(
            f"soak PASS updates={completed} ack_mean_ms={latency['mean']:.1f} "
            f"ack_p95_ms={latency['p95']:.1f} report={report_path}"
        )
        return 0
    print(f"soak FAIL updates={completed}/{count} error={error or 'health check'}", file=sys.stderr)
    print(f"partial report={report_path}", file=sys.stderr)
    return 130 if interrupted else 1


def _diagnostic_counter(message: dict[str, object], key: str) -> int:
    diagnostics = message.get("diagnostics")
    if not isinstance(diagnostics, dict):
        raise SerialSessionError("device pong has no diagnostics object")
    value = diagnostics.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SerialSessionError(f"device pong has invalid diagnostics.{key}")
    return value


def _run_serial_fuzz(port: str | None, count: int, seed: int, delay: float) -> int:
    try:
        cases = build_fuzz_cases(count, seed)
        selected_port = select_saki_port(port)
        baseline_codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            session.handshake(baseline_codec.hello())
            baseline = session.ping(baseline_codec.ping())
            invalid_before = _diagnostic_counter(baseline, "invalid_frames")
            oversized_before = _diagnostic_counter(baseline, "oversized_frames")

            byte_count = 0
            for case in cases:
                for chunk in case.chunks:
                    session.write_raw(chunk)
                    byte_count += len(chunk)
                if delay:
                    time.sleep(delay)

            # Invalid frames intentionally produce many error replies. Consume
            # them as raw bytes until TinyUSB is quiet before reusing request
            # ids in a fresh session.
            drained_bytes = session.drain_input(quiet_seconds=0.25, timeout=2.0)

            recovery_codec = ProtocolCodec()
            recovered_hello = session.handshake(recovery_codec.hello())
            recovered_ack = session.apply_status(
                recovery_codec.status(StateSnapshot(state=AgentState.IDLE))
            )
            recovered_pong = session.ping(recovery_codec.ping())

        invalid_after = _diagnostic_counter(recovered_pong, "invalid_frames")
        oversized_after = _diagnostic_counter(recovered_pong, "oversized_frames")
        invalid_delta = invalid_after - invalid_before
        oversized_delta = oversized_after - oversized_before
        device = recovered_hello["device"]
        print(
            f"fuzzed device={device['id']} firmware={device['fw']} cases={len(cases)} "
            f"random_cases={count} seed={seed} bytes={byte_count}"
        )
        print(
            f"diagnostics invalid_delta={invalid_delta} oversized_delta={oversized_delta} "
            f"tx_drops={_diagnostic_counter(recovered_pong, 'tx_drops')} "
            f"drained_response_bytes={drained_bytes}"
        )
        if invalid_delta <= 0 or oversized_delta <= 0:
            print("fuzz failed: device diagnostics did not observe the invalid corpus", file=sys.stderr)
            return 1
        if recovered_ack.get("applied") is not True:
            print("fuzz failed: valid recovery status was not applied", file=sys.stderr)
            return 1
        print("recovery=PASS handshake=true valid_status=true heartbeat=true")
        return 0
    except (OSError, ValueError, SerialSessionError) as exc:
        print(f"serial fuzz failed: {exc}", file=sys.stderr)
        return 1


def _run_serial_send(port: str | None, snapshot: StateSnapshot, hold: float) -> int:
    try:
        selected_port = select_saki_port(port)
        codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            hello = session.handshake(codec.hello())
            ack = session.apply_status(codec.status(snapshot))
            device = hello["device"]
            print(
                f"sent state={snapshot.state.value} device={device['id']} "
                f"firmware={device['fw']} applied={ack.get('applied')} "
                f"last_seq={ack.get('last_seq')}"
            )
            if hold:
                _hold_serial_session(session, codec, hold)
    except SerialSessionError as exc:
        print(f"send failed: {exc}", file=sys.stderr)
        return 1
    return 0


def _load_sanitized_replay(path: Path) -> list[StateSnapshot | None]:
    fixture_root = SANITIZED_REPLAY_ROOT.resolve()
    resolved = path.expanduser().resolve()
    try:
        resolved.relative_to(fixture_root)
    except ValueError as exc:
        raise ReplayError(
            f"replay file must be inside the sanitized fixture directory {fixture_root}"
        ) from exc
    if resolved.suffix != ".ndjson":
        raise ReplayError("replay file must use the .ndjson extension")

    steps: list[StateSnapshot | None] = []
    try:
        with resolved.open("rb") as stream:
            for line_number, frame in enumerate(stream, start=1):
                if not frame.strip():
                    continue
                try:
                    message = decode_frame(frame)
                except ProtocolError as exc:
                    raise ReplayError(f"{resolved.name}:{line_number}: {exc}") from exc
                message_type = message["type"]
                if message_type == "hello" and message.get("role") == "host":
                    continue
                if message_type == "clear":
                    steps.append(None)
                    continue
                if message_type != "status":
                    raise ReplayError(
                        f"{resolved.name}:{line_number}: unsupported replay message "
                        f"{message_type!r}"
                    )
                try:
                    steps.append(snapshot_from_payload(message))
                except ValueError as exc:
                    raise ReplayError(
                        f"{resolved.name}:{line_number}: invalid status payload: {exc}"
                    ) from exc
    except OSError as exc:
        raise ReplayError(f"cannot read replay file {resolved}: {exc}") from exc

    if not steps:
        raise ReplayError("replay contains no status or clear messages")
    return steps


def _run_serial_replay(port: str | None, path: Path, interval: float, hold: float) -> int:
    try:
        steps = _load_sanitized_replay(path)
        selected_port = select_saki_port(port)
        codec = ProtocolCodec()
        with SerialSession.open(selected_port) as session:
            hello = session.handshake(codec.hello())
            device = hello["device"]
            print(
                f"replaying file={path.name} steps={len(steps)} device={device['id']} "
                f"firmware={device['fw']}"
            )
            for index, snapshot in enumerate(steps, start=1):
                if snapshot is None:
                    ack = session.apply_clear(codec.clear())
                    state_name = "clear"
                else:
                    ack = session.apply_status(codec.status(snapshot))
                    state_name = snapshot.state.value
                print(
                    f"step={index}/{len(steps)} state={state_name} "
                    f"applied={ack.get('applied')} last_seq={ack.get('last_seq')}"
                )
                if index < len(steps) and interval:
                    time.sleep(interval)
            if hold:
                _hold_serial_session(session, codec, hold)
    except ReplayError as exc:
        print(f"replay rejected: {exc}", file=sys.stderr)
        return 2
    except SerialSessionError as exc:
        print(f"replay failed: {exc}", file=sys.stderr)
        return 1
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="saki-host")
    subparsers = parser.add_subparsers(dest="command", required=True)
    doctor = subparsers.add_parser("doctor", help="diagnose device discovery and protocol health")
    doctor.add_argument("--port", help="serial callout device; auto-detect by default")
    subparsers.add_parser("demo", help="print a sanitized demo session as NDJSON")
    send = subparsers.add_parser("send", help="send one complete status snapshot to a device")
    send.add_argument("--port", help="serial callout device; auto-detect by default")
    send.add_argument("--state", required=True, choices=[state.value for state in AgentState])
    send.add_argument("--title", help="task title; required unless state is idle")
    send.add_argument("--task-id", default="manual", help="stable task identifier")
    send.add_argument("--kind", choices=[kind.value for kind in ActivityKind])
    send.add_argument("--summary", default="", help="current activity summary")
    send.add_argument("--detail", default="", help="optional activity detail")
    send.add_argument("--percent", type=int, help="determinate progress from 0 to 100")
    send.add_argument("--progress-label", default="", help="short progress label")
    send.add_argument("--elapsed-ms", type=int, help="elapsed task time in milliseconds")
    send.add_argument("--agent", default="Codex", help="displayed Agent name")
    send.add_argument("--model", default="", help="displayed model name")
    send.add_argument("--hold", type=float, default=3.0, help="seconds before closing the port")
    replay = subparsers.add_parser("replay", help="replay a sanitized NDJSON fixture")
    replay.add_argument("path", type=Path, help="file under protocol/fixtures/v1/sessions")
    replay.add_argument("--port", help="serial callout device; auto-detect by default")
    replay.add_argument("--interval", type=float, default=0.8, help="seconds between states")
    replay.add_argument("--hold", type=float, default=3.0, help="seconds before closing the port")
    serial = subparsers.add_parser("serial", help="inspect serial transports")
    serial_subparsers = serial.add_subparsers(dest="serial_command", required=True)
    serial_subparsers.add_parser("list", help="list visible serial devices")
    serial_demo = serial_subparsers.add_parser(
        "demo", help="handshake with a device and display a status sequence"
    )
    serial_demo.add_argument("--port", help="serial callout device; auto-detect by default")
    serial_demo.add_argument(
        "--interval", type=float, default=0.8, help="seconds between demo states"
    )
    serial_demo.add_argument(
        "--hold", type=float, default=3.0, help="seconds to keep the completed state connected"
    )
    serial_cycle = serial_subparsers.add_parser(
        "cycle", help="repeatedly open, handshake, and close the Saki serial port"
    )
    serial_cycle.add_argument("--port", help="serial callout device; auto-detect by default")
    serial_cycle.add_argument(
        "--count", type=int, default=20, help="number of open/handshake/close cycles"
    )
    serial_cycle.add_argument(
        "--interval", type=float, default=0.1, help="seconds between cycles"
    )
    serial_fuzz = serial_subparsers.add_parser(
        "fuzz", help="inject a bounded invalid corpus and verify protocol recovery"
    )
    serial_fuzz.add_argument("--port", help="serial callout device; auto-detect by default")
    serial_fuzz.add_argument(
        "--count", type=int, default=32, help="number of deterministic random mutations"
    )
    serial_fuzz.add_argument("--seed", type=int, default=20260903, help="random corpus seed")
    serial_fuzz.add_argument(
        "--delay", type=float, default=0.01, help="seconds between injected cases"
    )
    serial_soak = serial_subparsers.add_parser(
        "soak", help="run a bounded long-duration status and resource test"
    )
    serial_soak.add_argument("--port", help="serial callout device; auto-detect by default")
    serial_soak.add_argument(
        "--count", type=int, default=10_000, help="number of status updates"
    )
    serial_soak.add_argument(
        "--duration", type=float, default=86_400, help="scheduled test duration in seconds"
    )
    serial_soak.add_argument(
        "--sample-interval",
        type=float,
        default=60,
        help="seconds between device diagnostic/runtime samples",
    )
    serial_soak.add_argument(
        "--report", type=Path, required=True, help="new JSON report path; existing files are refused"
    )
    hook = subparsers.add_parser("hook", help="map one Codex hook JSON object to a status")
    hook.add_argument("event", help="Codex hook event name")
    hook.add_argument(
        "--socket", type=Path, default=DEFAULT_SOCKET_PATH, help="Host service Unix socket"
    )
    hook.add_argument("--stdout", action="store_true", help="print protocol NDJSON for debugging")
    hook.add_argument("--strict", action="store_true", help="fail if the Host service is offline")
    serve = subparsers.add_parser("serve", help="run the hook receiver and persistent device session")
    serve.add_argument("--port", help="serial callout device; auto-detect by default")
    serve.add_argument(
        "--socket", type=Path, default=DEFAULT_SOCKET_PATH, help="Unix socket for hook events"
    )
    serve.add_argument(
        "--heartbeat", type=float, default=5.0, help="seconds between device heartbeats"
    )
    serve.add_argument(
        "--stale-after",
        type=float,
        default=120.0,
        help="seconds without a hook before active states become waiting_user; 0 disables",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "doctor":
        return _run_doctor(args.port)
    if args.command == "demo":
        return _run_demo()
    if args.command == "send":
        state = AgentState(args.state)
        if state is not AgentState.IDLE and not args.title:
            print("--title is required unless --state is idle", file=sys.stderr)
            return 2
        if args.percent is not None and not 0 <= args.percent <= 100:
            print("--percent must be from 0 to 100", file=sys.stderr)
            return 2
        if args.elapsed_ms is not None and args.elapsed_ms < 0:
            print("--elapsed-ms cannot be negative", file=sys.stderr)
            return 2
        if args.hold < 0:
            print("--hold cannot be negative", file=sys.stderr)
            return 2

        task = None if state is AgentState.IDLE else TaskInfo(args.task_id, args.title)
        activity = None
        if args.kind or args.summary or args.detail:
            activity = Activity(
                ActivityKind(args.kind or ActivityKind.OTHER.value),
                args.summary,
                args.detail,
            )
        progress = None
        if args.percent is not None:
            progress = Progress(ProgressMode.DETERMINATE, args.percent, args.progress_label)
        elif args.progress_label:
            progress = Progress(ProgressMode.INDETERMINATE, label=args.progress_label)
        snapshot = StateSnapshot(
            state=state,
            task=task,
            activity=activity,
            progress=progress,
            elapsed_ms=args.elapsed_ms,
            agent=AgentInfo(args.agent, args.model),
        )
        return _run_serial_send(args.port, snapshot, args.hold)
    if args.command == "replay":
        if args.interval < 0 or args.hold < 0:
            print("--interval and --hold cannot be negative", file=sys.stderr)
            return 2
        return _run_serial_replay(args.port, args.path, args.interval, args.hold)
    if args.command == "hook":
        return _run_hook(args.event, args.socket, args.stdout, args.strict)
    if args.command == "serve":
        if args.heartbeat <= 0:
            print("--heartbeat must be greater than zero", file=sys.stderr)
            return 2
        if args.stale_after < 0:
            print("--stale-after cannot be negative", file=sys.stderr)
            return 2
        return _run_service(args.port, args.socket, args.heartbeat, args.stale_after)
    if args.command == "serial" and args.serial_command == "list":
        return _run_serial_list()
    if args.command == "serial" and args.serial_command == "demo":
        if args.interval < 0 or args.hold < 0:
            print("--interval and --hold cannot be negative", file=sys.stderr)
            return 2
        return _run_serial_demo(args.port, args.interval, args.hold)
    if args.command == "serial" and args.serial_command == "cycle":
        if args.count <= 0 or args.interval < 0:
            print("--count must be positive and --interval cannot be negative", file=sys.stderr)
            return 2
        return _run_serial_cycle(args.port, args.count, args.interval)
    if args.command == "serial" and args.serial_command == "fuzz":
        if args.count < 0 or args.delay < 0:
            print("--count and --delay cannot be negative", file=sys.stderr)
            return 2
        return _run_serial_fuzz(args.port, args.count, args.seed, args.delay)
    if args.command == "serial" and args.serial_command == "soak":
        if args.count <= 0 or args.duration < 0 or args.sample_interval <= 0:
            print(
                "--count and --sample-interval must be positive; --duration cannot be negative",
                file=sys.stderr,
            )
            return 2
        if args.count == 1 and args.duration > 0:
            print("--count must be at least 2 when --duration is positive", file=sys.stderr)
            return 2
        update_interval = args.duration / max(1, args.count - 1)
        if update_interval > SOAK_MAX_UPDATE_INTERVAL_SECONDS:
            print(
                f"scheduled update interval {update_interval:.3f}s exceeds the "
                f"{SOAK_MAX_UPDATE_INTERVAL_SECONDS:.1f}s heartbeat-safe limit",
                file=sys.stderr,
            )
            return 2
        if args.report.exists():
            print(f"--report already exists: {args.report}", file=sys.stderr)
            return 2
        return _run_serial_soak(
            args.port,
            args.count,
            args.duration,
            args.sample_interval,
            args.report,
        )
    return 2
