from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import Any


class AgentState(StrEnum):
    IDLE = "idle"
    STARTING = "starting"
    THINKING = "thinking"
    WORKING = "working"
    WAITING_USER = "waiting_user"
    WAITING_APPROVAL = "waiting_approval"
    COMPLETED = "completed"
    FAILED = "failed"
    CANCELLED = "cancelled"


class ActivityKind(StrEnum):
    PLAN = "plan"
    READ = "read"
    EDIT = "edit"
    SHELL = "shell"
    WEB = "web"
    TEST = "test"
    MESSAGE = "message"
    OTHER = "other"


class ProgressMode(StrEnum):
    NONE = "none"
    INDETERMINATE = "indeterminate"
    DETERMINATE = "determinate"


def truncate_utf8(value: str, limit: int) -> str:
    """Return at most limit UTF-8 bytes without splitting a code point."""
    encoded = value.encode("utf-8", errors="replace")
    if len(encoded) <= limit:
        return encoded.decode("utf-8")
    return encoded[:limit].decode("utf-8", errors="ignore")


@dataclass(frozen=True, slots=True)
class TaskInfo:
    id: str
    title: str

    def to_dict(self) -> dict[str, str]:
        return {
            "id": truncate_utf8(self.id, 64),
            "title": truncate_utf8(self.title, 160),
        }


@dataclass(frozen=True, slots=True)
class Activity:
    kind: ActivityKind = ActivityKind.OTHER
    summary: str = ""
    detail: str = ""

    def to_dict(self) -> dict[str, str]:
        result = {"kind": self.kind.value}
        if self.summary:
            result["summary"] = truncate_utf8(self.summary, 240)
        if self.detail:
            result["detail"] = truncate_utf8(self.detail, 512)
        return result


@dataclass(frozen=True, slots=True)
class Progress:
    mode: ProgressMode = ProgressMode.NONE
    percent: int | None = None
    label: str = ""

    def __post_init__(self) -> None:
        if self.mode is ProgressMode.DETERMINATE and (
            self.percent is None or not 0 <= self.percent <= 100
        ):
            raise ValueError("determinate progress requires percent from 0 to 100")

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {"mode": self.mode.value}
        if self.mode is ProgressMode.DETERMINATE:
            result["percent"] = self.percent
        if self.label:
            result["label"] = truncate_utf8(self.label, 64)
        return result


@dataclass(frozen=True, slots=True)
class AgentInfo:
    name: str = "Agent"
    model: str = ""

    def to_dict(self) -> dict[str, str]:
        result = {"name": truncate_utf8(self.name, 32)}
        if self.model:
            result["model"] = truncate_utf8(self.model, 64)
        return result


@dataclass(frozen=True, slots=True)
class StateSnapshot:
    state: AgentState
    task: TaskInfo | None = None
    activity: Activity | None = None
    progress: Progress | None = None
    elapsed_ms: int | None = None
    agent: AgentInfo = AgentInfo()

    def __post_init__(self) -> None:
        if self.state is not AgentState.IDLE and self.task is None:
            raise ValueError(f"{self.state.value} requires task information")
        if self.elapsed_ms is not None and self.elapsed_ms < 0:
            raise ValueError("elapsed_ms cannot be negative")

    def to_payload(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "state": self.state.value,
            "agent": self.agent.to_dict(),
        }
        if self.task is not None:
            result["task"] = self.task.to_dict()
        if self.activity is not None:
            result["activity"] = self.activity.to_dict()
        if self.progress is not None:
            result["progress"] = self.progress.to_dict()
        if self.elapsed_ms is not None:
            result["elapsed_ms"] = self.elapsed_ms
        return result


def snapshot_from_payload(payload: dict[str, Any]) -> StateSnapshot:
    """Validate and reconstruct a snapshot received over local IPC."""
    try:
        state = AgentState(payload["state"])
        task_payload = payload.get("task")
        activity_payload = payload.get("activity")
        progress_payload = payload.get("progress")
        agent_payload = payload.get("agent", {})

        task = None
        if task_payload is not None:
            if not isinstance(task_payload, dict):
                raise ValueError("task must be an object")
            task = TaskInfo(id=str(task_payload["id"]), title=str(task_payload["title"]))

        activity = None
        if activity_payload is not None:
            if not isinstance(activity_payload, dict):
                raise ValueError("activity must be an object")
            activity = Activity(
                kind=ActivityKind(activity_payload.get("kind", ActivityKind.OTHER.value)),
                summary=str(activity_payload.get("summary", "")),
                detail=str(activity_payload.get("detail", "")),
            )

        progress = None
        if progress_payload is not None:
            if not isinstance(progress_payload, dict):
                raise ValueError("progress must be an object")
            progress = Progress(
                mode=ProgressMode(progress_payload.get("mode", ProgressMode.NONE.value)),
                percent=progress_payload.get("percent"),
                label=str(progress_payload.get("label", "")),
            )

        if not isinstance(agent_payload, dict):
            raise TypeError("agent must be an object")
        elapsed_ms = payload.get("elapsed_ms")
        if elapsed_ms is not None and not isinstance(elapsed_ms, int):
            raise ValueError("elapsed_ms must be an integer")
        return StateSnapshot(
            state=state,
            task=task,
            activity=activity,
            progress=progress,
            elapsed_ms=elapsed_ms,
            agent=AgentInfo(
                name=str(agent_payload.get("name", "Agent")),
                model=str(agent_payload.get("model", "")),
            ),
        )
    except (KeyError, TypeError) as exc:
        raise ValueError("invalid snapshot payload") from exc
