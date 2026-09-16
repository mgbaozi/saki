from __future__ import annotations

import io
import json
import os
import subprocess
import sys
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

import pytest

from saki_host import cli
from saki_host.adapters import SourceKind, normalize_hook
from saki_host.adapters.base import SourceEvent
from saki_host.hooks_config import configure_hooks, merge_hooks
from saki_host.identity import initialize_identity_key, load_identity_key
from saki_host.ipc import decode_source_event, encode_source_event
from saki_host.models import AgentState
from saki_host.service import SakiHostService, ServiceConfig
from saki_host.sessions import SessionRegistry, load_checkpoint, save_checkpoint

KEY = b"x" * 32


def event(name="UserPromptSubmit", source=SourceKind.CODEX, sid="same-id", ns=1, **payload):
    return normalize_hook(source, name, {"session_id": sid, **payload}, KEY, emitted_ns=ns)


def test_sources_are_separate_even_when_titles_paths_and_native_ids_match():
    registry = SessionRegistry()
    for source in SourceKind:
        registry.accept(event(source=source), now=0)
    assert len(registry.records) == 2
    assert {r.snapshot.agent.name for r in registry.visible()} == {"Codex", "Claude Code"}


def test_no_raw_hook_data_in_ipc_checkpoint_or_device_snapshot():
    secret = "sk-ant-api03-FAKE-PRIVATE-SENTINEL"
    hook = {
        "session_id": secret,
        "turn_id": secret,
        "event_id": secret,
        "prompt": secret,
        "task_title": secret,
        "cwd": secret,
        "tool_name": secret,
        "tool_input": secret,
        "tool_response": secret,
        "transcript_path": secret,
        "model": secret,
    }
    for source in SourceKind:
        normalized = normalize_hook(source, "UserPromptSubmit", hook, KEY, emitted_ns=1)
        wire = encode_source_event(normalized)
        registry = SessionRegistry()
        registry.accept(normalized, now=0)
        assert secret not in wire.decode()
        assert secret not in json.dumps(registry.checkpoint(now=1))
        assert secret not in json.dumps(registry.focus(1).to_payload())
        assert decode_source_event(wire) == normalized


def test_tool_failure_and_subagent_stop_cannot_end_user_session():
    failed = event("PostToolUseFailure", SourceKind.CLAUDE_CODE)
    assert failed.snapshot().state is AgentState.THINKING
    assert event("SubagentStop", SourceKind.CLAUDE_CODE) is None
    assert event("Stop", parent_session_id="parent") is None
    assert (
        event("Notification", SourceKind.CLAUDE_CODE, notification_type="permission_prompt") is None
    )


def test_unsupported_identity_or_arbitrary_ipc_field_rejected():
    with pytest.raises(ValueError):
        normalize_hook(SourceKind.CLAUDE_CODE, "Stop", {}, KEY)
    data = event().to_dict()
    data["prompt"] = "secret"
    with pytest.raises(ValueError):
        SourceEvent.from_dict(data)
    with pytest.raises(ValueError):
        SourceEvent.from_dict({**event().to_dict(), "emitted_ns": True})


def test_new_run_rejects_old_stop_and_duplicate_prompt():
    registry = SessionRegistry()
    assert registry.accept(event(turn_id="first"), now=0)
    assert not registry.accept(event(turn_id="first", ns=2), now=1)
    assert registry.accept(event(turn_id="second", ns=3), now=2)
    assert not registry.accept(event("Stop", turn_id="first", ns=4), now=3)
    assert registry.focus(3).state is AgentState.STARTING


def test_resume_does_not_reset_clock_or_allow_old_turn():
    registry = SessionRegistry()
    registry.accept(event(turn_id="first"), now=0)
    registry.accept(event("SessionStart", ns=2), now=10)
    assert registry.focus(12).elapsed_ms == 12000
    assert not registry.accept(event("Stop", turn_id="other", ns=3), now=13)


def test_stop_then_continue_and_ttl_not_extended_by_duplicate():
    registry = SessionRegistry()
    registry.accept(event(), now=0)
    registry.accept(event("Stop", ns=2), now=10)
    registry.accept(event("PreToolUse", ns=3), now=12)
    assert registry.focus(12).state is AgentState.WORKING
    registry.accept(event("Stop", ns=4), now=20)
    assert not registry.accept(event("Stop", ns=5), now=50)
    registry.tick(now=80)
    assert not registry.records
    assert not registry.accept(event("PreToolUse", ns=6), now=81)


def test_waiting_protected_and_capacity_pressure_evicts_terminal():
    registry = SessionRegistry(capacity=2)
    registry.accept(event(sid="waiting"), now=0)
    registry.accept(event("PermissionRequest", sid="waiting", ns=2), now=1)
    registry.accept(event(sid="ordinary"), now=2)
    assert not registry.accept(event(sid="overflow"), now=3)
    registry.accept(event("Stop", sid="ordinary", ns=2), now=4)
    assert registry.accept(event(sid="overflow", ns=2), now=5)
    assert registry.focus(5).state is AgentState.STARTING
    assert registry.visible()[1].snapshot.state is AgentState.WAITING_APPROVAL
    assert registry.rejected == 1


def test_source_local_elapsed_and_stale_are_independent():
    registry = SessionRegistry(stale_after=10)
    for index in range(4):
        registry.accept(event(sid=str(index), source=list(SourceKind)[index % 2]), now=index)
    assert registry.tick(now=10)
    first = registry.records[event(sid="0").session_id]
    assert first.stale and first.current(100).elapsed_ms == 10000
    assert not registry.records[event(sid="1", source=SourceKind.CLAUDE_CODE).session_id].stale
    assert (
        registry.records[event(sid="1", source=SourceKind.CLAUDE_CODE).session_id]
        .current(10)
        .elapsed_ms
        == 9000
    )
    assert len({item.event.session_id for item in registry.visible()}) == 4


def test_checkpoint_is_private_bounded_and_restores_as_unconfirmed(tmp_path):
    registry = SessionRegistry()
    registry.accept(event(source=SourceKind.CLAUDE_CODE), now=10)
    path = tmp_path / "sessions.json"
    save_checkpoint(path, registry.checkpoint(now=20, wall=100))
    assert path.stat().st_mode & 0o777 == 0o600
    recovered = SessionRegistry()
    recovered.restore(load_checkpoint(path), now=1, wall=105)
    assert recovered.focus(100).elapsed_ms == 10000
    assert recovered.visible()[0].stale
    recovered.tick(now=86401)
    assert not recovered.records
    os.chmod(path, 0o644)
    with pytest.raises(ValueError):
        load_checkpoint(path)


def test_corrupt_checkpoint_does_not_partially_replace_state():
    registry = SessionRegistry()
    registry.accept(event(), now=0)
    before = set(registry.records)
    with pytest.raises(ValueError):
        registry.restore(
            {
                "v": 1,
                "saved_at": 100,
                "records": [
                    {"event": {"prompt": "sensitive"}},
                ],
            },
            now=1,
            wall=101,
        )
    assert set(registry.records) == before


def test_identity_permissions_and_symlinks(tmp_path):
    key = tmp_path / "identity.key"
    initialize_identity_key(key)
    first = load_identity_key(key)
    initialize_identity_key(key)
    assert load_identity_key(key) == first
    link = tmp_path / "link"
    link.symlink_to(key)
    with pytest.raises(OSError):
        load_identity_key(link)
    os.chmod(key, 0o644)
    with pytest.raises(ValueError):
        load_identity_key(key)


def test_configuration_preserves_credentials_and_other_hooks(tmp_path):
    original = {
        "env": {"ANTHROPIC_API_KEY": "FAKE-SECRET"},
        "permissions": {"deny": ["Bash"]},
        "hooks": {"Stop": [{"hooks": [{"type": "command", "command": "user-command"}]}]},
    }
    source = SourceKind.CLAUDE_CODE
    installed = merge_hooks(original, source, tmp_path / "with spaces/hook.sh", install=True)
    assert (
        merge_hooks(installed, source, tmp_path / "with spaces/hook.sh", install=True) == installed
    )
    assert merge_hooks(installed, source, tmp_path / "hook.sh", install=False) == original
    assert original["hooks"]["Stop"][0]["hooks"][0]["command"] == "user-command"


def test_configuration_install_check_and_uninstall_are_idempotent(tmp_path):
    path = tmp_path / "settings.json"
    path.write_text('{"env":{"ANTHROPIC_API_KEY":"FAKE-SECRET"}}')
    state_dir = tmp_path / "state"
    first = configure_hooks(path, SourceKind.CLAUDE_CODE, "install", state_dir=state_dir)
    assert first["changed"]
    assert "FAKE-SECRET" not in json.dumps(first)
    assert not configure_hooks(path, SourceKind.CLAUDE_CODE, "install", state_dir=state_dir)[
        "changed"
    ]
    assert configure_hooks(path, SourceKind.CLAUDE_CODE, "check", state_dir=state_dir)["installed"]
    configure_hooks(path, SourceKind.CLAUDE_CODE, "uninstall", state_dir=state_dir)
    assert json.loads(path.read_text())["env"]["ANTHROPIC_API_KEY"] == "FAKE-SECRET"
    assert path.stat().st_mode & 0o777 == 0o600


@pytest.mark.parametrize("raw", ['{"password":"FAKE-SECRET"', "[]", "[" * 1000, "x" * 65537])
def test_live_hook_failures_never_print_or_block(raw, tmp_path):
    output, error = io.StringIO(), io.StringIO()
    with (
        patch.object(sys, "stdin", io.StringIO(raw)),
        redirect_stdout(output),
        redirect_stderr(error),
    ):
        assert cli.main(["hook", "Stop", "--identity-key", str(tmp_path / "missing")]) == 0
    assert output.getvalue() == error.getvalue() == ""


def test_hook_pipe_deadline_is_internal(tmp_path):
    process = subprocess.Popen(
        [sys.executable, "-m", "saki_host", "hook", "Stop"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        assert process.wait(timeout=3) == 0
        out, err = process.communicate()
        assert out == err == b""
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate()


def test_service_fallback_focus_is_not_last_event_wins():
    service = SakiHostService(ServiceConfig())
    service.accept_source_event(event())
    service.accept_source_event(event("PermissionRequest", ns=2))
    service.accept_source_event(event(source=SourceKind.CLAUDE_CODE))
    service.accept_source_event(event("PermissionRequest", ns=3))
    assert service.mailbox.latest.state is AgentState.STARTING
    assert service.mailbox.latest.agent.name == "Claude Code"
    assert len(service.registry.records) == 2


def test_restored_resume_retains_state_run_and_clock():
    original = SessionRegistry()
    original.accept(event(turn_id="first"), now=0)
    original.accept(event("PreToolUse", turn_id="first", ns=2), now=1)
    original.accept(event("SessionStart", ns=3), now=2)
    saved = original.checkpoint(now=3, wall=100)
    restored = SessionRegistry()
    restored.restore(saved, now=0, wall=101)
    assert restored.focus(50).state is AgentState.WORKING
    assert restored.focus(50).elapsed_ms == 3000
    assert restored.visible()[0].run_id == original.visible()[0].run_id
    assert restored.visible()[0].stale


def test_closed_wait_keeps_state_and_freezes_independent_elapsed():
    registry = SessionRegistry()
    registry.accept(event(), now=0)
    registry.accept(event("PermissionRequest", ns=2), now=1)
    assert registry.focus(20).elapsed_ms == 20000
    registry.accept(event("SessionEnd", ns=3), now=21)
    assert registry.focus(30).elapsed_ms == 21000
    saved = registry.checkpoint(now=30, wall=100)
    restored = SessionRegistry()
    restored.restore(saved, now=0, wall=101)
    assert restored.focus(100).state is AgentState.WAITING_APPROVAL
    assert restored.focus(100).elapsed_ms == 21000
    registry.accept(event("SessionStart", ns=4), now=31)
    assert registry.focus(31).elapsed_ms == 0


def test_forget_command_is_bounded_and_does_not_control_agent():
    from saki_host.ipc import HookDatagramProtocol, encode_forget

    service = SakiHostService(ServiceConfig())
    service.accept_source_event(event())
    protocol = HookDatagramProtocol(
        service.accept_snapshot, service.accept_source_event, service.forget_session
    )
    protocol.datagram_received(encode_forget("all"), None)
    assert not service.registry.records
    service.accept_source_event(event("Stop", ns=2))
    assert not service.registry.records
    with pytest.raises(ValueError):
        encode_forget("/private/raw/path")


def test_hook_check_detects_unusable_private_identity_and_changed_wrapper(tmp_path):
    settings = tmp_path / "settings.json"
    state = tmp_path / "private state"
    configure_hooks(settings, SourceKind.CLAUDE_CODE, "install", state_dir=state)
    (state / "identity.key").chmod(0o644)
    (state / "hook-claude_code.sh").write_text("#!/bin/sh\nexit 0\n")
    status = configure_hooks(settings, SourceKind.CLAUDE_CODE, "check", state_dir=state)
    assert status["installed"]
    assert not status["identity_ready"]
    assert not status["wrapper_ready"]


def test_latest_submission_keeps_main_visible_and_attention_in_sidebar():
    registry = SessionRegistry()
    for i in range(6):
        registry.accept(event(sid=str(i)), now=i)
        registry.accept(event("PermissionRequest", sid=str(i), ns=2), now=i + 0.1)
    latest = event(sid="latest", ns=10)
    registry.accept(latest, now=7)
    assert registry.visible()[0].event.session_id == latest.session_id
    assert len(registry.visible()) == 4
    assert all(r.snapshot.state is AgentState.WAITING_APPROVAL for r in registry.visible()[1:])
    registry.accept(event("PreToolUse", sid="0", ns=3), now=8)
    registry.accept(event("SessionStart", sid="1", ns=3), now=9)
    registry.accept(event("SessionStart", sid="new-idle", ns=3), now=10)
    registry.accept(event("Stop", sid="latest", ns=11), now=11)
    assert registry.focus(11).task.id == latest.session_id
    assert registry.focus(11).state is AgentState.COMPLETED
    registry.accept(event(sid="0", ns=4), now=12)
    assert registry.focus(12).task.id == event(sid="0").session_id
    registry.remove(event(sid="0").session_id, 13)
    assert registry.focus(13).task.id == latest.session_id


def test_submission_order_survives_restart_and_old_checkpoint_migration():
    registry = SessionRegistry()
    registry.accept(event(sid="a"), now=0)
    registry.accept(event(sid="b"), now=1)
    registry.accept(event("PreToolUse", sid="a", ns=2), now=2)
    saved = registry.checkpoint(now=3, wall=100)
    restored = SessionRegistry()
    restored.restore(saved, now=0, wall=101)
    assert restored.focus(0).task.id == event(sid="b").session_id
    restored.accept(event("SessionStart", sid="a", ns=3), now=1)
    assert restored.focus(1).task.id == event(sid="b").session_id
    restored.accept(event(sid="a", ns=4), now=2)
    assert restored.focus(2).task.id == event(sid="a").session_id
    for item in saved["records"]:
        item.pop("submitted_order")
    restored.restore(saved, now=3, wall=102)
    assert restored.focus(3).task.id == event(sid="b").session_id
    saved["records"][0]["submitted_order"] = -1
    with pytest.raises(ValueError):
        restored.restore(saved, now=4, wall=103)
    assert restored.focus(4).task.id == event(sid="b").session_id


def test_rejected_duplicate_prompt_does_not_take_focus():
    registry = SessionRegistry()
    registry.accept(event(sid="a", turn_id="one"), now=0)
    registry.accept(event(sid="b", turn_id="two"), now=1)
    assert not registry.accept(event(sid="a", turn_id="one", ns=2), now=2)
    assert registry.focus(2).task.id == event(sid="b").session_id
