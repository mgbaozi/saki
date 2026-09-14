import unittest

from saki_host.models import (
    MAX_JSON_SAFE_INTEGER,
    AgentState,
    Progress,
    ProgressMode,
    StateSnapshot,
    truncate_utf8,
)


class ModelTests(unittest.TestCase):
    def test_utf8_truncation_keeps_codepoint_boundary(self) -> None:
        self.assertEqual(truncate_utf8("中文abc", 7), "中文a")

    def test_utf8_truncation_replaces_unpaired_surrogates(self) -> None:
        self.assertEqual(truncate_utf8("bad\ud800text", 32), "bad?text")

    def test_non_idle_requires_task(self) -> None:
        with self.assertRaises(ValueError):
            StateSnapshot(state=AgentState.WORKING)

    def test_determinate_progress_requires_valid_percent(self) -> None:
        with self.assertRaises(ValueError):
            Progress(ProgressMode.DETERMINATE, 101)

    def test_elapsed_accepts_json_safe_integer_maximum(self) -> None:
        snapshot = StateSnapshot(
            state=AgentState.IDLE,
            elapsed_ms=MAX_JSON_SAFE_INTEGER,
        )
        self.assertEqual(snapshot.elapsed_ms, MAX_JSON_SAFE_INTEGER)

    def test_elapsed_rejects_value_above_json_safe_integer_maximum(self) -> None:
        with self.assertRaisesRegex(ValueError, "JSON safe integer"):
            StateSnapshot(
                state=AgentState.IDLE,
                elapsed_ms=MAX_JSON_SAFE_INTEGER + 1,
            )


if __name__ == "__main__":
    unittest.main()
