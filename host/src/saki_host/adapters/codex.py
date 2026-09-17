from collections.abc import Mapping

from ..models import ActivityKind
from .base import AdapterSpec, EventKind, NativeHookFields, SourceKind


def extract(payload: Mapping[str, object]) -> NativeHookFields:
    session_id = payload.get("session_id") or payload.get("thread_id") or ""
    run_id = payload.get("turn_id") or ""
    event_id = payload.get("event_id") or ""
    for value in (session_id, run_id, event_id):
        if not isinstance(value, str):
            raise TypeError("invalid native hook identity")
    return NativeHookFields(
        session_id=session_id,
        run_id=run_id,
        event_id=event_id,
        tool_name=payload.get("tool_name") if isinstance(payload.get("tool_name"), str) else "",
        parent_id="present"
        if payload.get("parent_session_id") or payload.get("parent_thread_id")
        else "",
        task_title=payload.get("task_title")
        if isinstance(payload.get("task_title"), str)
        else "",
        prompt=payload.get("prompt") if isinstance(payload.get("prompt"), str) else "",
    )


def classify(
    name: str, payload: Mapping[str, object], default: EventKind | None
) -> EventKind | None:
    if name == "Interrupt":
        return EventKind.CANCEL
    # Only exact structured result codes; never inspect conversation/error prose.
    if name == "Stop":
        return {"failed": EventKind.FAILURE, "cancelled": EventKind.CANCEL}.get(
            payload.get("stop_reason") if isinstance(payload.get("stop_reason"), str) else "",
            EventKind.STOP,
        )
    return default


SPEC = AdapterSpec(
    source=SourceKind.CODEX,
    default_settings_path=(".codex", "hooks.json"),
    hook_events=(
        "SessionStart",
        "UserPromptSubmit",
        "PreToolUse",
        "PostToolUse",
        "PermissionRequest",
        "Stop",
        "SessionEnd",
        "Interrupt",
    ),
    event_kinds={
        "SessionStart": EventKind.OPEN,
        "UserPromptSubmit": EventKind.PROMPT,
        "PreToolUse": EventKind.TOOL_START,
        "PostToolUse": EventKind.TOOL_END,
        "PermissionRequest": EventKind.APPROVAL,
        "Stop": EventKind.STOP,
        "SessionEnd": EventKind.CLOSE,
        "Interrupt": EventKind.CANCEL,
    },
    extract=extract,
    classify=classify,
    tool_kinds={
        "Bash": ActivityKind.SHELL,
        "exec_command": ActivityKind.SHELL,
        "Read": ActivityKind.READ,
        "read_file": ActivityKind.READ,
        "Glob": ActivityKind.READ,
        "Grep": ActivityKind.READ,
        "Edit": ActivityKind.EDIT,
        "Write": ActivityKind.EDIT,
        "apply_patch": ActivityKind.EDIT,
        "WebFetch": ActivityKind.WEB,
        "WebSearch": ActivityKind.WEB,
    },
    input_tools=frozenset({"AskUserQuestion", "request_user_input"}),
)
