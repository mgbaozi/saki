from __future__ import annotations

import json

import pytest
from test_sources import KEY, event

from saki_host.adapters import SourceKind, normalize_hook
from saki_host.adapters.base import SourceEvent
from saki_host.display import encode_projection, projection
from saki_host.ipc import decode_source_event, encode_source_event
from saki_host.protocol import ProtocolCodec
from saki_host.sessions import SessionRegistry
from saki_host.task_goal import display_goal


@pytest.mark.parametrize("source", list(SourceKind))
def test_goal_reaches_device_and_survives_tools_continue_resume_and_restart(source):
    registry = SessionRegistry()
    first = event(
        source=source, prompt="实现多 Session 和 Claude Code 支持。后续细节不应进入标题。"
    )
    registry.accept(first, now=0)
    title = "实现多 Session 和 Claude Code 支持"
    assert decode_source_event(encode_source_event(first)).goal == title
    for ns, name in enumerate(("PreToolUse", "PostToolUse", "Stop", "SessionStart"), 2):
        registry.accept(event(name, source=source, ns=ns, task_title="工具不得改目标"), now=ns)
        assert registry.focus(ns).task.title == title
    registry.accept(event(source=source, ns=6, prompt="继续"), now=6)
    assert registry.focus(6).task.title == title
    recovered = SessionRegistry()
    recovered.restore(registry.checkpoint(now=7, wall=100), now=0, wall=101)
    assert recovered.focus(0).task.title == title
    wire = encode_projection(ProtocolCodec(), projection(recovered, 0))
    assert wire["sessions"][0]["task"]["title"] == title
    recovered.accept(
        event(source=source, ns=7, task_title="修复侧栏触摸", prompt="其他说明"), now=1
    )
    assert recovered.focus(1).task.title == "修复侧栏触摸"


def test_goals_are_isolated_and_old_events_and_checkpoints_remain_readable():
    registry = SessionRegistry()
    registry.accept(event(sid="a", prompt="修复布局"), now=0)
    registry.accept(event(sid="b", prompt="更新文档"), now=1)
    assert {r.snapshot.task.title for r in registry.visible()} == {"修复布局", "更新文档"}
    old = event(sid="old").to_dict()
    old.pop("goal")
    assert SourceEvent.from_dict(old).goal == ""
    saved = registry.checkpoint(now=2, wall=100)
    for item in saved["records"]:
        item["event"].pop("goal")
    restored = SessionRegistry()
    restored.restore(saved, now=0, wall=101)
    assert len(restored.records) == 2


@pytest.mark.parametrize(
    "secret",
    [
        "sk-ant-api03-FAKE-PRIVATE-SENTINEL",
        '"api_key": "fake-secret-value"',
        "Authorization: Bearer fake-secret-value",
        "密码是 fake-secret-value",
        "-----BEGIN PRIVATE KEY-----",
        "abcdefghijklmnopqrstuvwxyz1234567890",
        "https://user:fake-password@example.test/?token=secret",
    ],
)
def test_sensitive_lines_never_reach_ipc_checkpoint_or_device(secret):
    prompt = "修复状态屏\n" + secret
    normalized = normalize_hook(
        SourceKind.CODEX, "UserPromptSubmit", {"session_id": "test", "prompt": prompt}, KEY
    )
    registry = SessionRegistry()
    registry.accept(normalized, now=0)
    for value in (
        encode_source_event(normalized).decode(),
        json.dumps(registry.checkpoint(now=1)),
        json.dumps(registry.focus(1).to_payload()),
    ):
        assert secret not in value
    assert normalized.goal == "修复状态屏"
    assert "fake-secret-value" not in display_goal(secret)


def test_context_code_paths_urls_and_long_utf8_are_bounded_and_idempotent():
    text = "<environment_context>DO NOT DISPLAY</environment_context>\n```\nprivate code\n```\n修复 /Users/demo/private/app.py 的状态展示"
    assert display_goal(text) == "修复 [路径] 的状态展示"
    assert display_goal("检查 https://user:pass@example.test/a?token=x 链接") == "检查 [链接] 链接"
    assert display_goal("更新 [路线图](docs/ROADMAP.md)") == "更新 路线图"
    result = display_goal("中" * 500)
    assert len(result.encode()) <= 96
    assert display_goal(result) == result
    assert display_goal("继续") == ""
    assert display_goal(None) == ""
    assert display_goal("a" * 65537) == ""
    assert display_goal("修复\x00布局\u202e") == "修复布局"


def test_untrusted_ipc_cannot_bypass_goal_redaction():
    for goal in ("sk-ant-api03-FAKE-PRIVATE-SENTINEL", "x\nsecond line", "中" * 50, 123):
        with pytest.raises(ValueError):
            SourceEvent.from_dict({**event().to_dict(), "goal": goal})
