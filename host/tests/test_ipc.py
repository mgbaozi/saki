from __future__ import annotations

import unittest

from saki_host.ipc import (
    decode_snapshot_datagram,
    decode_snapshot_envelope,
    encode_snapshot_datagram,
)
from saki_host.models import (
    Activity,
    ActivityKind,
    AgentState,
    Progress,
    ProgressMode,
    StateSnapshot,
    TaskInfo,
)


class IpcTests(unittest.TestCase):
    def test_snapshot_round_trip(self) -> None:
        snapshot = StateSnapshot(
            state=AgentState.WORKING,
            task=TaskInfo("task-1", "Build firmware"),
            activity=Activity(ActivityKind.TEST, "Running tests"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=1234,
        )

        decoded = decode_snapshot_datagram(encode_snapshot_datagram(snapshot))

        self.assertEqual(decoded, snapshot)

    def test_snapshot_envelope_preserves_monotonic_source_timestamp(self) -> None:
        snapshot = StateSnapshot(state=AgentState.IDLE)

        decoded = decode_snapshot_envelope(
            encode_snapshot_datagram(snapshot, emitted_monotonic_ns=123_456_789)
        )

        self.assertEqual(decoded.snapshot, snapshot)
        self.assertEqual(decoded.emitted_monotonic_ns, 123_456_789)

    def test_rejects_non_snapshot_message(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid local IPC message"):
            decode_snapshot_datagram(b'{"v":1,"type":"raw-hook","payload":{}}')


if __name__ == "__main__":
    unittest.main()
