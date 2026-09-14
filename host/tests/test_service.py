from __future__ import annotations

import asyncio
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

from saki_host import service as service_module
from saki_host.ble_binding import BleBinding, BleBindingStore
from saki_host.models import AgentState, StateSnapshot, TaskInfo
from saki_host.protocol import ProtocolCodec
from saki_host.protocol_session import ProtocolSessionError
from saki_host.service import (
    ActiveLink,
    ReconnectPolicy,
    SakiHostService,
    ServiceConfig,
    SnapshotMailbox,
    SnapshotTimeline,
    parse_device_diagnostics,
    parse_device_runtime,
)
from saki_host.transports.base import TransportKind
from saki_host.transports.ble import BleCandidate


class ReconnectPolicyTests(unittest.TestCase):
    def test_recent_disconnect_uses_a_ten_second_fast_window(self) -> None:
        policy = ReconnectPolicy(
            ServiceConfig(
                reconnect_fast_window_seconds=10.0,
                reconnect_fast_interval_seconds=0.1,
            )
        )
        policy.on_connected()

        first_delay, first_fast = policy.next_delay(was_connected=True, now=100.0)
        later_delay, later_fast = policy.next_delay(was_connected=False, now=109.5)

        self.assertTrue(first_fast)
        self.assertAlmostEqual(first_delay, 0.1)
        self.assertTrue(later_fast)
        self.assertAlmostEqual(later_delay, 0.1)

    def test_fast_window_falls_back_to_exponential_retry(self) -> None:
        policy = ReconnectPolicy(ServiceConfig(reconnect_fast_window_seconds=10.0))
        policy.on_connected()
        policy.next_delay(was_connected=True, now=100.0)

        first_delay, first_fast = policy.next_delay(was_connected=False, now=110.0)
        second_delay, second_fast = policy.next_delay(was_connected=False, now=111.0)

        self.assertFalse(first_fast)
        self.assertEqual(first_delay, 0.25)
        self.assertFalse(second_fast)
        self.assertEqual(second_delay, 0.5)

    def test_startup_failure_does_not_enter_fast_window(self) -> None:
        policy = ReconnectPolicy(ServiceConfig())

        delay, fast = policy.next_delay(was_connected=False, now=100.0)

        self.assertFalse(fast)
        self.assertEqual(delay, 0.25)


class ConnectedPortWatchTests(unittest.IsolatedAsyncioTestCase):
    async def test_two_missing_samples_detect_disappearance(self) -> None:
        service = SakiHostService(
            ServiceConfig(
                connected_port_check_seconds=0,
                connected_port_missing_samples=2,
            )
        )

        with patch.object(Path, "exists", side_effect=[False, False]) as exists:
            await service._wait_for_port_disappearance("/dev/cu.saki")

        self.assertEqual(exists.call_count, 2)

    async def test_present_sample_resets_missing_count(self) -> None:
        service = SakiHostService(
            ServiceConfig(
                connected_port_check_seconds=0,
                connected_port_missing_samples=2,
            )
        )

        with patch.object(
            Path,
            "exists",
            side_effect=[False, True, False, False],
        ) as exists:
            await service._wait_for_port_disappearance("/dev/cu.saki")

        self.assertEqual(exists.call_count, 4)


class SnapshotMailboxTests(unittest.IsolatedAsyncioTestCase):
    async def test_latest_snapshot_coalesces_updates(self) -> None:
        mailbox = SnapshotMailbox(StateSnapshot(state=AgentState.IDLE))
        first = StateSnapshot(state=AgentState.STARTING, task=TaskInfo("t", "Task"))
        latest = StateSnapshot(state=AgentState.WORKING, task=TaskInfo("t", "Task"))

        mailbox.publish(first)
        mailbox.publish(latest)
        snapshot, generation = await asyncio.wait_for(mailbox.wait_after(0), 0.1)

        self.assertEqual(snapshot, latest)
        self.assertEqual(generation, 2)


class SnapshotTimelineTests(unittest.TestCase):
    def test_active_task_receives_elapsed_time(self) -> None:
        timeline = SnapshotTimeline()
        task = TaskInfo("t", "Task")

        starting = timeline.enrich(StateSnapshot(state=AgentState.STARTING, task=task))
        working = timeline.enrich(StateSnapshot(state=AgentState.WORKING, task=task))

        self.assertEqual(starting.elapsed_ms, 0)
        self.assertIsNotNone(working.elapsed_ms)


class ActiveStateWatchdogTests(unittest.TestCase):
    def test_stale_thinking_becomes_waiting_user(self) -> None:
        service = SakiHostService(ServiceConfig(stale_after_seconds=10.0))
        task = TaskInfo("t", "Task")
        service.accept_snapshot(StateSnapshot(state=AgentState.THINKING, task=task))

        changed = service.publish_stale_if_needed(now=service._last_source_at + 10.0)

        self.assertTrue(changed)
        self.assertEqual(service.mailbox.latest.state, AgentState.WAITING_USER)
        self.assertIn("Mac", service.mailbox.latest.activity.summary)

    def test_new_hook_replaces_watchdog_state(self) -> None:
        service = SakiHostService(ServiceConfig(stale_after_seconds=10.0))
        task = TaskInfo("t", "Task")
        service.accept_snapshot(StateSnapshot(state=AgentState.THINKING, task=task))
        service.publish_stale_if_needed(now=service._last_source_at + 10.0)

        service.accept_snapshot(StateSnapshot(state=AgentState.WORKING, task=task))

        self.assertEqual(service.mailbox.latest.state, AgentState.WORKING)

    def test_terminal_state_does_not_expire(self) -> None:
        service = SakiHostService(ServiceConfig(stale_after_seconds=10.0))
        service.accept_snapshot(
            StateSnapshot(state=AgentState.COMPLETED, task=TaskInfo("t", "Task"))
        )

        changed = service.publish_stale_if_needed(now=service._last_source_at + 20.0)

        self.assertFalse(changed)
        self.assertEqual(service.mailbox.latest.state, AgentState.COMPLETED)


class SourceLatencyTests(unittest.IsolatedAsyncioTestCase):
    async def test_successful_sync_reports_hook_source_to_ack_latency(self) -> None:
        service = SakiHostService(ServiceConfig())
        snapshot = StateSnapshot(state=AgentState.IDLE)
        output = StringIO()

        class AckSession:
            def apply_status(
                self,
                message: dict[str, object],
                timeout: float,
            ) -> dict[str, object]:
                del timeout
                return {"ok": True, "last_seq": message["seq"]}

        with (
            patch("saki_host.service.time.monotonic_ns", return_value=1_125_000_000),
            redirect_stdout(output),
        ):
            await service._send_status_with_retry(
                AckSession(),  # type: ignore[arg-type]
                ProtocolCodec(),
                snapshot,
                source_emitted_ns=1_000_000_000,
            )

        self.assertIn("source_to_ack_ms=125.0", output.getvalue())

    async def test_only_matching_latest_generation_keeps_source_timestamp(self) -> None:
        service = SakiHostService(ServiceConfig())

        service.accept_snapshot(StateSnapshot(state=AgentState.IDLE), 123)
        generation = service.mailbox.generation

        self.assertIsNone(service._take_source_emitted(generation - 1))
        self.assertEqual(service._take_source_emitted(generation), 123)
        self.assertIsNone(service._take_source_emitted(generation))


class DeviceDiagnosticsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.diagnostics = {
            "valid_frames": 42,
            "invalid_frames": 1,
            "oversized_frames": 0,
            "old_sequences": 2,
            "ui_queue_overwrites": 3,
            "tx_drops": 0,
            "heartbeat_timeouts": 0,
        }

    def test_parser_accepts_complete_uint32_counters(self) -> None:
        parsed = parse_device_diagnostics({"diagnostics": self.diagnostics})

        self.assertEqual(parsed, self.diagnostics)

    def test_parser_ignores_older_or_malformed_pong(self) -> None:
        self.assertIsNone(parse_device_diagnostics({}))
        self.assertIsNone(
            parse_device_diagnostics(
                {"diagnostics": {**self.diagnostics, "tx_drops": True}}
            )
        )

    def test_service_reports_anomaly_changes_once(self) -> None:
        service = SakiHostService(ServiceConfig())

        self.assertTrue(service.observe_device_diagnostics({"diagnostics": self.diagnostics}))
        self.assertFalse(service.observe_device_diagnostics({"diagnostics": self.diagnostics}))

    def test_runtime_parser_accepts_complete_metrics(self) -> None:
        runtime = {
            "heap_free_bytes": 8_000_000,
            "heap_min_bytes": 7_900_000,
            "internal_free_bytes": 180_000,
            "internal_min_bytes": 160_000,
            "app_stack_min_bytes": 1_400,
            "ui_stack_min_bytes": 3_200,
            "usb_stack_min_bytes": 2_800,
        }

        self.assertEqual(parse_device_runtime({"runtime": runtime}), runtime)

    def test_runtime_parser_accepts_optional_ble_stack_metric(self) -> None:
        runtime = {
            "heap_free_bytes": 8_000_000,
            "heap_min_bytes": 7_900_000,
            "internal_free_bytes": 180_000,
            "internal_min_bytes": 160_000,
            "app_stack_min_bytes": 1_400,
            "ui_stack_min_bytes": 3_200,
            "usb_stack_min_bytes": 2_800,
            "ble_stack_min_bytes": 2_400,
        }

        self.assertEqual(parse_device_runtime({"runtime": runtime}), runtime)

    def test_runtime_parser_ignores_missing_or_malformed_metrics(self) -> None:
        self.assertIsNone(parse_device_runtime({}))
        self.assertIsNone(
            parse_device_runtime(
                {
                    "runtime": {
                        "heap_free_bytes": True,
                        "heap_min_bytes": 1,
                        "internal_free_bytes": 1,
                        "internal_min_bytes": 1,
                        "app_stack_min_bytes": 1,
                        "ui_stack_min_bytes": 1,
                        "usb_stack_min_bytes": 1,
                    }
                }
            )
        )


class TransportSupervisorTests(unittest.IsolatedAsyncioTestCase):
    async def test_ble_reconnect_uses_only_cached_and_verified_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binding_path = Path(directory) / "binding.json"
            BleBindingStore(binding_path).save(BleBinding("verified", "0123456789ab"))
            service = SakiHostService(ServiceConfig(ble_binding_path=binding_path))
            visible = [
                BleCandidate("other", "Saki", -20, object()),
                BleCandidate("verified", "Saki", -40, object()),
            ]

            class Transport:
                def __init__(self, device: object, **_kwargs: object) -> None:
                    self.device = device

            class Session:
                latest: Session | None = None

                def __init__(self, transport: Transport, codec: ProtocolCodec, **_kwargs: object):
                    self.transport = transport
                    self.codec = codec
                    self.closed = False
                    self.required_capabilities: frozenset[str] | None = None
                    Session.latest = self

                async def connect(self) -> None:
                    pass

                async def handshake(self, **kwargs: object) -> dict[str, object]:
                    self.required_capabilities = kwargs["required_capabilities"]  # type: ignore[assignment]
                    return {"device": {"id": "0123456789ab"}}

                async def close(self) -> None:
                    self.closed = True

            async def discover(**_kwargs: object) -> list[BleCandidate]:
                return visible

            with (
                patch.object(service_module, "discover_saki_devices", side_effect=discover),
                patch.object(service_module, "BleByteTransport", Transport),
                patch.object(service_module, "ProtocolSession", Session),
            ):
                link = await service._connect_ble_link()

        session = Session.latest
        self.assertIsNotNone(session)
        assert session is not None
        self.assertIs(session.transport.device, visible[1].device)
        self.assertIs(session.codec, service._codec)
        self.assertEqual(
            session.required_capabilities,
            frozenset({"ble", "secure-connection", "transport-arbitration"}),
        )
        self.assertEqual(link.device_id, "0123456789ab")

    async def test_ble_reconnect_rejects_changed_device_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binding_path = Path(directory) / "binding.json"
            BleBindingStore(binding_path).save(BleBinding("verified", "0123456789ab"))
            service = SakiHostService(ServiceConfig(ble_binding_path=binding_path))
            candidate = BleCandidate("verified", "Saki", -40, object())

            class Transport:
                def __init__(self, _device: object, **_kwargs: object) -> None:
                    pass

            class Session:
                closed = False

                def __init__(self, *_args: object, **_kwargs: object) -> None:
                    Session.closed = False

                async def connect(self) -> None:
                    pass

                async def handshake(self, **_kwargs: object) -> dict[str, object]:
                    return {"device": {"id": "fedcba987654"}}

                async def close(self) -> None:
                    Session.closed = True

            async def discover(**_kwargs: object) -> list[BleCandidate]:
                return [candidate]

            with (
                patch.object(service_module, "discover_saki_devices", side_effect=discover),
                patch.object(service_module, "BleByteTransport", Transport),
                patch.object(service_module, "ProtocolSession", Session),
                self.assertRaisesRegex(ProtocolSessionError, "does not match"),
            ):
                await service._connect_ble_link()

        self.assertTrue(Session.closed)

    async def test_usb_takeover_syncs_before_ble_is_returned_for_close(self) -> None:
        service = SakiHostService(ServiceConfig(transport="auto", heartbeat_seconds=60))
        events: list[str] = []

        class Session:
            def __init__(self, name: str) -> None:
                self.name = name
                self.closed = False

            async def apply_status(self, _snapshot: StateSnapshot) -> dict[str, object]:
                events.append(f"{self.name}:status")
                return {"ok": True, "applied": True, "last_seq": 1}

            async def ping(self) -> dict[str, object]:
                return {}

            async def close(self) -> None:
                self.closed = True
                events.append(f"{self.name}:close")

        ble_session = Session("ble")
        usb_session = Session("usb")
        ble_link = ActiveLink(
            ble_session,  # type: ignore[arg-type]
            TransportKind.BLE,
            "ble-identifier",
            "0123456789ab",
        )
        usb_link = ActiveLink(
            usb_session,  # type: ignore[arg-type]
            TransportKind.USB,
            "/dev/cu.saki",
            "0123456789ab",
            "/dev/cu.saki",
        )

        async def usb_available() -> str:
            return "/dev/cu.saki"

        async def connect_usb(_port: str | None = None) -> ActiveLink:
            self.assertFalse(ble_session.closed)
            return usb_link

        with (
            patch.object(service, "_wait_for_usb_availability", side_effect=usb_available),
            patch.object(service, "_connect_usb_link", side_effect=connect_usb),
        ):
            replacement, generation = await asyncio.wait_for(
                service._run_active_link(ble_link, service.mailbox.generation),
                0.2,
            )

        self.assertIs(replacement, usb_link)
        self.assertEqual(generation, service.mailbox.generation)
        self.assertFalse(ble_session.closed)
        self.assertEqual(events, ["usb:status"])

        await service._close_link(ble_link)
        self.assertEqual(events, ["usb:status", "ble:close"])

    async def test_successive_transports_share_one_codec_and_global_sequence(self) -> None:
        service = SakiHostService(ServiceConfig())
        messages: list[dict[str, object]] = []

        class CodecSession:
            async def apply_status(self, snapshot: StateSnapshot) -> dict[str, object]:
                message = service._codec.status(snapshot)
                messages.append(message)
                return {"ok": True, "applied": True, "last_seq": message["seq"]}

            async def close(self) -> None:
                pass

        first = ActiveLink(
            CodecSession(),  # type: ignore[arg-type]
            TransportKind.BLE,
            "ble",
            "0123456789ab",
        )
        second = ActiveLink(
            CodecSession(),  # type: ignore[arg-type]
            TransportKind.USB,
            "usb",
            "0123456789ab",
        )

        await service._sync_link(first)
        await service._sync_link(second)

        self.assertEqual([message["seq"] for message in messages], [1, 2])
        self.assertEqual(len({message["session"] for message in messages}), 1)


if __name__ == "__main__":
    unittest.main()
