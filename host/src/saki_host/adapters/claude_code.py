from .base import EventKind


def classify(name: str, payload: dict, default: EventKind | None) -> EventKind | None:
    if name == "PostToolUseFailure":
        return EventKind.TOOL_ERROR
    if name == "StopFailure":
        return EventKind.FAILURE
    # Notification lacks reliable causal ordering against PermissionRequest/tool
    # results. Do not reintroduce a completed approval from a delayed reminder.
    if name == "Notification":
        return None
    return default
