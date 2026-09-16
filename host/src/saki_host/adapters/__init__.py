"""Fixed, explicit source adapters; never infer a product from a tool name."""

from __future__ import annotations

import time

from ..identity import pseudonym
from ..models import ActivityKind
from ..task_goal import display_goal
from .base import TOOL_KINDS, EventKind, SourceEvent, SourceKind

_EVENTS = {
    "SessionStart": EventKind.OPEN,
    "UserPromptSubmit": EventKind.PROMPT,
    "PreToolUse": EventKind.TOOL_START,
    "PostToolUse": EventKind.TOOL_END,
    "PermissionRequest": EventKind.APPROVAL,
    "Stop": EventKind.STOP,
    "SessionEnd": EventKind.CLOSE,
}


def normalize_hook(
    source: SourceKind, name: str, payload: dict, key: bytes, *, emitted_ns: int | None = None
) -> SourceEvent | None:
    if not isinstance(payload, dict):
        raise TypeError("hook must be an object")
    if source is SourceKind.CODEX:
        from .codex import classify
    else:
        from .claude_code import classify
    kind = classify(name, payload, _EVENTS.get(name))
    if kind is None:
        return None
    # Child identities are not independent user-visible sessions in this release.
    if payload.get("parent_session_id") or payload.get("parent_thread_id"):
        return None
    sid = payload.get("session_id") or (
        payload.get("thread_id") if source is SourceKind.CODEX else None
    )
    run = payload.get("turn_id") if source is SourceKind.CODEX else None
    event_id = payload.get("event_id")
    tool = payload.get("tool_name")
    activity = (
        TOOL_KINDS.get(tool, ActivityKind.OTHER) if isinstance(tool, str) else ActivityKind.OTHER
    )
    if name == "PreToolUse" and tool in ("AskUserQuestion", "request_user_input"):
        kind = EventKind.INPUT
    return SourceEvent(
        source,
        pseudonym(key, source, "session", sid),
        kind,
        time.monotonic_ns() if emitted_ns is None else emitted_ns,
        pseudonym(key, source, "run", run) if run else "",
        pseudonym(key, source, "event", event_id) if event_id else "",
        activity,
        (display_goal(payload.get("task_title")) or display_goal(payload.get("prompt")))
        if kind is EventKind.PROMPT
        else "",
    )


__all__ = ["SourceEvent", "SourceKind", "normalize_hook"]
