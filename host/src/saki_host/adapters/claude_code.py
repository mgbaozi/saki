from collections.abc import Mapping

from ..models import ActivityKind
from .base import AdapterSpec, EventKind, NativeHookFields, SourceKind


def extract(payload: Mapping[str, object]) -> NativeHookFields:
    session_id = payload.get("session_id") or ""
    event_id = payload.get("event_id") or ""
    for value in (session_id, event_id):
        if not isinstance(value, str):
            raise TypeError("invalid native hook identity")
    return NativeHookFields(
        session_id=session_id,
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
    if name == "PostToolUseFailure":
        return EventKind.TOOL_ERROR
    if name == "StopFailure":
        return EventKind.FAILURE
    # Notification lacks reliable causal ordering against PermissionRequest/tool
    # results. Do not reintroduce a completed approval from a delayed reminder.
    if name == "Notification":
        return None
    return default


SPEC = AdapterSpec(
    source=SourceKind.CLAUDE_CODE,
    default_settings_path=(".claude", "settings.json"),
    hook_events=(
        "SessionStart",
        "UserPromptSubmit",
        "PreToolUse",
        "PostToolUse",
        "PermissionRequest",
        "Stop",
        "SessionEnd",
        "PostToolUseFailure",
        "StopFailure",
    ),
    event_kinds={
        "SessionStart": EventKind.OPEN,
        "UserPromptSubmit": EventKind.PROMPT,
        "PreToolUse": EventKind.TOOL_START,
        "PostToolUse": EventKind.TOOL_END,
        "PermissionRequest": EventKind.APPROVAL,
        "Stop": EventKind.STOP,
        "SessionEnd": EventKind.CLOSE,
        "PostToolUseFailure": EventKind.TOOL_ERROR,
        "StopFailure": EventKind.FAILURE,
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
