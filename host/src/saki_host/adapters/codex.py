from .base import EventKind


def classify(name: str, payload: dict, default: EventKind | None) -> EventKind | None:
    if name == "Interrupt":
        return EventKind.CANCEL
    # Only exact structured result codes; never inspect conversation/error prose.
    if name == "Stop":
        return {"failed": EventKind.FAILURE, "cancelled": EventKind.CANCEL}.get(
            payload.get("stop_reason") if isinstance(payload.get("stop_reason"), str) else "",
            EventKind.STOP,
        )
    return default
