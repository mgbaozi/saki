import unittest

from saki_host.codex_hooks import snapshot_from_codex_hook
from saki_host.models import ActivityKind, AgentState


class CodexHookTests(unittest.TestCase):
    def setUp(self) -> None:
        self.payload = {
            "thread_id": "thread-demo",
            "task_title": "实现状态屏",
            "tool_name": "exec_command",
        }

    def test_tool_use_maps_to_working(self) -> None:
        snapshot = snapshot_from_codex_hook("PreToolUse", self.payload)
        self.assertEqual(snapshot.state, AgentState.WORKING)
        self.assertEqual(snapshot.activity.kind, ActivityKind.SHELL)

    def test_permission_maps_to_waiting_approval(self) -> None:
        snapshot = snapshot_from_codex_hook("PermissionRequest", self.payload)
        self.assertEqual(snapshot.state, AgentState.WAITING_APPROVAL)

    def test_failed_stop(self) -> None:
        snapshot = snapshot_from_codex_hook("Stop", {**self.payload, "reason": "failed"})
        self.assertEqual(snapshot.state, AgentState.FAILED)

    def test_usage_limit_stop_waits_for_user(self) -> None:
        snapshot = snapshot_from_codex_hook(
            "Stop",
            {**self.payload, "error": "You have reached your usage limit"},
        )
        self.assertEqual(snapshot.state, AgentState.WAITING_USER)
        self.assertIn("额度", snapshot.activity.summary)

    def test_chinese_usage_limit_stop_waits_for_user(self) -> None:
        snapshot = snapshot_from_codex_hook(
            "Stop",
            {**self.payload, "message": "你已达到使用上限，请稍后重试"},
        )
        self.assertEqual(snapshot.state, AgentState.WAITING_USER)

    def test_session_start_is_idle(self) -> None:
        snapshot = snapshot_from_codex_hook("SessionStart", self.payload)
        self.assertEqual(snapshot.state, AgentState.IDLE)

    def test_hook_whitespace_entities_are_normalized_for_display(self) -> None:
        snapshot = snapshot_from_codex_hook(
            "PreToolUse",
            {
                **self.payload,
                "task_title": "&#x20;继续&#32;开发&nbsp;固件&#20;",
                "tool_name": "exec&#x20;command",
            },
        )

        self.assertEqual(snapshot.task.title, "继续 开发 固件")
        self.assertEqual(snapshot.activity.summary, "正在使用 exec command")

    def test_non_whitespace_html_entities_stay_literal(self) -> None:
        snapshot = snapshot_from_codex_hook(
            "PreToolUse",
            {**self.payload, "task_title": "显示 &lt;tag&gt;"},
        )

        self.assertEqual(snapshot.task.title, "显示 &lt;tag&gt;")

    def test_usage_limit_detection_handles_encoded_spaces(self) -> None:
        snapshot = snapshot_from_codex_hook(
            "Stop",
            {**self.payload, "error": "usage&#x20;limit"},
        )

        self.assertEqual(snapshot.state, AgentState.WAITING_USER)


if __name__ == "__main__":
    unittest.main()
