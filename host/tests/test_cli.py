from __future__ import annotations

import json
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from typing import Self
from unittest.mock import patch

from saki_host import cli
from saki_host.models import AgentState, StateSnapshot
from saki_host.protocol import ProtocolError
from saki_host.transports.serial import SerialCandidate, SerialSessionError

SAKI_CANDIDATE = SerialCandidate(
    device="/dev/cu.saki",
    description="Saki Agent Display",
    product="Saki Agent Display",
    serial_number="0123456789ab",
    vid=0x303A,
    pid=0x4001,
)


class FakeCycleSession:
    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_: object) -> None:
        pass

    def handshake(self, message: dict[str, object]) -> dict[str, object]:
        return {
            "device": {"id": "0123456789ab", "fw": "0.1.4"},
            "reply_to": message["id"],
        }


class FakeDoctorSession(FakeCycleSession):
    def handshake(self, message: dict[str, object]) -> dict[str, object]:
        return {
            "device": {"name": "saki-box3", "id": "0123456789ab", "fw": "0.1.4"},
            "screen": {"width": 320, "height": 240},
            "reply_to": message["id"],
        }

    def ping(self, message: dict[str, object]) -> dict[str, object]:
        return {
            "uptime_ms": 1234,
            "last_seq": 0,
            "diagnostics": {"valid_frames": 2},
            "runtime": {
                "heap_free_bytes": 8_000_000,
                "heap_min_bytes": 7_900_000,
                "internal_free_bytes": 180_000,
                "internal_min_bytes": 160_000,
                "app_stack_min_bytes": 1_400,
                "ui_stack_min_bytes": 3_200,
                "usb_stack_min_bytes": 2_800,
            },
            "reply_to": message["id"],
        }


class LegacyDoctorSession(FakeDoctorSession):
    def ping(self, message: dict[str, object]) -> dict[str, object]:
        pong = super().ping(message)
        del pong["runtime"]
        return pong


class LowRuntimeDoctorSession(FakeDoctorSession):
    def ping(self, message: dict[str, object]) -> dict[str, object]:
        pong = super().ping(message)
        runtime = pong["runtime"]
        assert isinstance(runtime, dict)
        runtime["internal_min_bytes"] = 16_000
        runtime["usb_stack_min_bytes"] = 512
        return pong


class IncompatibleDoctorSession(FakeCycleSession):
    def handshake(self, _message: dict[str, object]) -> dict[str, object]:
        try:
            raise ProtocolError("unsupported_version")
        except ProtocolError as exc:
            raise SerialSessionError(f"invalid device frame: {exc}") from exc


class FakeStateSession(FakeCycleSession):
    def __init__(self) -> None:
        self.status_messages: list[dict[str, object]] = []
        self.clear_messages: list[dict[str, object]] = []
        self.ping_messages: list[dict[str, object]] = []

    def apply_status(self, message: dict[str, object]) -> dict[str, object]:
        self.status_messages.append(message)
        return {"applied": True, "last_seq": message["seq"]}

    def ping(self, message: dict[str, object]) -> dict[str, object]:
        self.ping_messages.append(message)
        return {"reply_to": message["id"]}


class FakeFuzzSession(FakeStateSession):
    def __init__(self) -> None:
        super().__init__()
        self.raw_writes: list[bytes] = []
        self.ping_count = 0
        self.drain_count = 0

    def handshake(self, message: dict[str, object]) -> dict[str, object]:
        return {
            "device": {"id": "0123456789ab", "fw": "0.2.0-dev"},
            "reply_to": message["id"],
        }

    def ping(self, message: dict[str, object]) -> dict[str, object]:
        self.ping_count += 1
        return {
            "reply_to": message["id"],
            "diagnostics": {
                "invalid_frames": 10 if self.ping_count == 1 else 25,
                "oversized_frames": 2 if self.ping_count == 1 else 3,
                "tx_drops": 0,
            },
        }

    def write_raw(self, data: bytes) -> None:
        self.raw_writes.append(data)

    def drain_input(self, quiet_seconds: float, timeout: float) -> int:
        self.drain_count += 1
        return 256

    def apply_clear(self, message: dict[str, object]) -> dict[str, object]:
        self.clear_messages.append(message)
        return {"applied": True, "last_seq": message["seq"]}


class FakeSoakSession(FakeStateSession):
    def ping(self, message: dict[str, object]) -> dict[str, object]:
        self.ping_messages.append(message)
        return {
            "reply_to": message["id"],
            "diagnostics": {
                "valid_frames": len(self.status_messages) + len(self.ping_messages),
                "invalid_frames": 0,
                "oversized_frames": 0,
                "old_sequences": 0,
                "ui_queue_overwrites": 0,
                "tx_drops": 0,
                "heartbeat_timeouts": 0,
            },
            "runtime": {
                "heap_free_bytes": 8_000_000,
                "heap_min_bytes": 7_900_000,
                "internal_free_bytes": 180_000,
                "internal_min_bytes": 160_000,
                "app_stack_min_bytes": 1_400,
                "ui_stack_min_bytes": 3_200,
                "usb_stack_min_bytes": 2_800,
            },
        }


class DoctorCliTests(unittest.TestCase):
    def test_doctor_reports_no_serial_devices(self) -> None:
        output = StringIO()

        with patch.object(cli, "list_candidates", return_value=[]), redirect_stdout(output):
            result = cli._run_doctor(None)

        self.assertEqual(result, 1)
        self.assertIn("FAIL serial: no serial devices are visible", output.getvalue())

    def test_doctor_rejects_an_explicit_non_saki_port(self) -> None:
        output = StringIO()
        candidate = SerialCandidate(
            device="/dev/cu.debug",
            description="USB JTAG/serial debug unit",
            product="USB JTAG/serial debug unit",
            serial_number="0123456789ab",
            vid=0x303A,
            pid=0x1001,
        )

        with patch.object(cli, "list_candidates", return_value=[candidate]), redirect_stdout(output):
            result = cli._run_doctor(candidate.device)

        self.assertEqual(result, 1)
        self.assertIn("not 'Saki Agent Display'", output.getvalue())
        self.assertIn("press RST without holding K0", output.getvalue())

    def test_doctor_explains_an_incompatible_protocol(self) -> None:
        output = StringIO()

        with (
            patch.object(cli, "list_candidates", return_value=[SAKI_CANDIDATE]),
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(
                cli.SerialSession, "open", return_value=IncompatibleDoctorSession()
            ),
            redirect_stdout(output),
        ):
            result = cli._run_doctor(None)

        self.assertEqual(result, 1)
        self.assertIn("FAIL protocol", output.getvalue())
        self.assertIn("Host requires v1", output.getvalue())

    def test_doctor_reports_a_healthy_device(self) -> None:
        output = StringIO()
        clock = iter([1.0, 1.16, 2.0, 2.04])

        with (
            patch.object(cli, "list_candidates", return_value=[SAKI_CANDIDATE]),
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=FakeDoctorSession()),
            patch.object(cli.time, "monotonic", side_effect=lambda: next(clock)),
            redirect_stdout(output),
        ):
            result = cli._run_doctor(None)

        self.assertEqual(result, 0)
        self.assertIn("PASS handshake", output.getvalue())
        self.assertIn("firmware=0.1.4", output.getvalue())
        self.assertIn("handshake_ms=160.0", output.getvalue())
        self.assertIn("ping_ms=40.0", output.getvalue())
        self.assertIn("PASS runtime", output.getvalue())
        self.assertIn("RESULT: healthy", output.getvalue())

    def test_doctor_allows_legacy_firmware_without_runtime_metrics(self) -> None:
        output = StringIO()

        with (
            patch.object(cli, "list_candidates", return_value=[SAKI_CANDIDATE]),
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=LegacyDoctorSession()),
            redirect_stdout(output),
        ):
            result = cli._run_doctor(None)

        self.assertEqual(result, 0)
        self.assertIn("WARN runtime: metrics unavailable", output.getvalue())
        self.assertIn("RESULT: healthy", output.getvalue())

    def test_doctor_fails_low_runtime_safety_margins(self) -> None:
        output = StringIO()

        with (
            patch.object(cli, "list_candidates", return_value=[SAKI_CANDIDATE]),
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=LowRuntimeDoctorSession()),
            redirect_stdout(output),
        ):
            result = cli._run_doctor(None)

        self.assertEqual(result, 1)
        self.assertIn("FAIL runtime", output.getvalue())
        self.assertIn("internal_min_bytes=16000<32768", output.getvalue())
        self.assertIn("usb_stack_min_bytes=512<1024", output.getvalue())
        self.assertIn("RESULT: unhealthy", output.getvalue())

    def test_doctor_parser_accepts_an_explicit_port(self) -> None:
        args = cli.build_parser().parse_args(["doctor", "--port", "/dev/cu.saki"])

        self.assertEqual(args.command, "doctor")
        self.assertEqual(args.port, "/dev/cu.saki")


class SerialCycleCliTests(unittest.TestCase):
    def test_cycle_opens_a_fresh_session_each_time_and_summarizes(self) -> None:
        output = StringIO()
        clock = iter([1.0, 1.05, 2.0, 2.06])

        with (
            patch.object(cli, "select_saki_port", return_value="/dev/cu.fake") as select,
            patch.object(cli.SerialSession, "open", return_value=FakeCycleSession()) as opened,
            patch.object(cli.time, "monotonic", side_effect=lambda: next(clock)),
            patch.object(cli.time, "sleep") as sleep,
            redirect_stdout(output),
        ):
            result = cli._run_serial_cycle(None, count=2, interval=0.1)

        self.assertEqual(result, 0)
        self.assertEqual(select.call_count, 2)
        self.assertEqual(opened.call_count, 2)
        sleep.assert_called_once_with(0.1)
        self.assertIn("completed cycles=2 failures=0", output.getvalue())

    def test_cycle_parser_defaults_to_twenty_rounds(self) -> None:
        args = cli.build_parser().parse_args(["serial", "cycle"])

        self.assertEqual(args.count, 20)
        self.assertEqual(args.interval, 0.1)


class SerialFuzzCliTests(unittest.TestCase):
    def test_fuzz_injects_corpus_and_verifies_recovery(self) -> None:
        output = StringIO()
        session = FakeFuzzSession()

        with (
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=session),
            redirect_stdout(output),
        ):
            result = cli._run_serial_fuzz(None, count=0, seed=7, delay=0)

        self.assertEqual(result, 0)
        self.assertGreater(len(session.raw_writes), 0)
        self.assertEqual(session.drain_count, 1)
        self.assertEqual(len(session.status_messages), 1)
        self.assertEqual(session.status_messages[0]["state"], "idle")
        self.assertIn("invalid_delta=15", output.getvalue())
        self.assertIn("oversized_delta=1", output.getvalue())
        self.assertIn("drained_response_bytes=256", output.getvalue())
        self.assertIn("recovery=PASS", output.getvalue())

    def test_fuzz_parser_rejects_negative_counts(self) -> None:
        errors = StringIO()

        with redirect_stderr(errors):
            result = cli.main(["serial", "fuzz", "--count", "-1"])

        self.assertEqual(result, 2)
        self.assertIn("--count and --delay cannot be negative", errors.getvalue())


class SerialSoakCliTests(unittest.TestCase):
    def test_soak_writes_a_machine_readable_health_report(self) -> None:
        output = StringIO()
        session = FakeSoakSession()

        with tempfile.TemporaryDirectory() as directory:
            report_path = cli.Path(directory) / "soak.json"
            with (
                patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
                patch.object(cli.SerialSession, "open", return_value=session),
                redirect_stdout(output),
            ):
                result = cli._run_serial_soak(
                    None,
                    count=3,
                    duration=0,
                    sample_interval=1,
                    report_path=report_path,
                )

            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(result, 0)
        self.assertEqual(report["result"], "pass")
        self.assertEqual(report["completed_updates"], 3)
        self.assertEqual(len(session.status_messages), 3)
        self.assertGreaterEqual(len(session.ping_messages), 2)
        self.assertIn("ack_latency_ms", report)
        self.assertIn("soak PASS", output.getvalue())

    def test_soak_parser_defaults_to_release_acceptance_targets(self) -> None:
        args = cli.build_parser().parse_args(["serial", "soak", "--report", "soak.json"])

        self.assertEqual(args.count, 10_000)
        self.assertEqual(args.duration, 86_400)
        self.assertEqual(args.sample_interval, 60)

    def test_soak_rejects_a_schedule_that_can_expire_the_device_watchdog(self) -> None:
        errors = StringIO()

        with redirect_stderr(errors):
            result = cli.main(
                [
                    "serial",
                    "soak",
                    "--count",
                    "2",
                    "--duration",
                    "20",
                    "--report",
                    "soak.json",
                ]
            )

        self.assertEqual(result, 2)
        self.assertIn("heartbeat-safe limit", errors.getvalue())


class SendCliTests(unittest.TestCase):
    def test_send_applies_one_complete_snapshot(self) -> None:
        output = StringIO()
        session = FakeStateSession()
        snapshot = StateSnapshot(state=AgentState.IDLE)

        with (
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=session),
            redirect_stdout(output),
        ):
            result = cli._run_serial_send(None, snapshot, hold=0)

        self.assertEqual(result, 0)
        self.assertEqual(len(session.status_messages), 1)
        self.assertEqual(session.status_messages[0]["state"], "idle")
        self.assertIn("sent state=idle", output.getvalue())

    def test_send_hold_keeps_the_device_session_alive(self) -> None:
        output = StringIO()
        session = FakeStateSession()
        snapshot = StateSnapshot(state=AgentState.IDLE)

        with (
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=session),
            patch.object(cli.time, "sleep") as sleep,
            redirect_stdout(output),
        ):
            result = cli._run_serial_send(None, snapshot, hold=11)

        self.assertEqual(result, 0)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [5.0, 5.0, 1.0])
        self.assertEqual(len(session.ping_messages), 2)

    def test_send_requires_a_title_for_non_idle_state(self) -> None:
        errors = StringIO()

        with redirect_stderr(errors):
            result = cli.main(["send", "--state", "working"])

        self.assertEqual(result, 2)
        self.assertIn("--title is required", errors.getvalue())

    def test_send_rejects_invalid_progress_before_opening_serial(self) -> None:
        errors = StringIO()

        with redirect_stderr(errors):
            result = cli.main(
                ["send", "--state", "working", "--title", "Task", "--percent", "101"]
            )

        self.assertEqual(result, 2)
        self.assertIn("--percent must be from 0 to 100", errors.getvalue())


class ReplayCliTests(unittest.TestCase):
    def test_load_replay_accepts_the_sanitized_basic_fixture(self) -> None:
        steps = cli._load_sanitized_replay(cli.SANITIZED_REPLAY_ROOT / "basic.ndjson")

        self.assertEqual(
            [step.state if step is not None else None for step in steps],
            [AgentState.STARTING, AgentState.WORKING, AgentState.COMPLETED],
        )

    def test_load_replay_rejects_files_outside_the_fixture_directory(self) -> None:
        with self.assertRaisesRegex(cli.ReplayError, "sanitized fixture directory"):
            cli._load_sanitized_replay(cli.SANITIZED_REPLAY_ROOT.parent / "private.ndjson")

    def test_replay_reissues_snapshots_with_a_fresh_session(self) -> None:
        output = StringIO()
        session = FakeStateSession()

        with (
            patch.object(cli, "select_saki_port", return_value=SAKI_CANDIDATE.device),
            patch.object(cli.SerialSession, "open", return_value=session),
            patch.object(cli.time, "sleep") as sleep,
            redirect_stdout(output),
        ):
            result = cli._run_serial_replay(
                None,
                cli.SANITIZED_REPLAY_ROOT / "basic.ndjson",
                interval=0.1,
                hold=0,
            )

        self.assertEqual(result, 0)
        self.assertEqual(len(session.status_messages), 3)
        self.assertEqual([message["seq"] for message in session.status_messages], [1, 2, 3])
        self.assertEqual(len({message["session"] for message in session.status_messages}), 1)
        self.assertEqual(sleep.call_count, 2)
        self.assertIn("step=3/3 state=completed", output.getvalue())


if __name__ == "__main__":
    unittest.main()
