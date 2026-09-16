from __future__ import annotations

import re
from dataclasses import asdict, dataclass
from enum import StrEnum

from ..models import (
    Activity,
    ActivityKind,
    AgentInfo,
    AgentState,
    Progress,
    ProgressMode,
    StateSnapshot,
    TaskInfo,
)
from ..task_goal import display_goal


class SourceKind(StrEnum):
    CODEX = "codex"
    CLAUDE_CODE = "claude_code"

    @property
    def label(self) -> str:
        return "Codex" if self is SourceKind.CODEX else "Claude Code"


class EventKind(StrEnum):
    OPEN = "open"
    PROMPT = "prompt"
    TOOL_START = "tool_start"
    TOOL_END = "tool_end"
    TOOL_ERROR = "tool_error"
    APPROVAL = "approval"
    INPUT = "input"
    STOP = "stop"
    FAILURE = "failure"
    CANCEL = "cancel"
    CLOSE = "close"


_SAFE_ID = re.compile(r"[a-f0-9]{32}")


@dataclass(frozen=True, slots=True)
class SourceEvent:
    source: SourceKind
    session_id: str
    kind: EventKind
    emitted_ns: int
    run_id: str = ""
    event_id: str = ""
    activity: ActivityKind = ActivityKind.OTHER
    goal: str = ""

    def __post_init__(self) -> None:
        if not isinstance(self.source, SourceKind) or not isinstance(self.kind, EventKind):
            raise TypeError("invalid source event enum")
        if not isinstance(self.activity, ActivityKind):
            raise TypeError("invalid activity")
        if not isinstance(self.goal, str) or display_goal(self.goal) != self.goal:
            raise ValueError("invalid display goal")
        for value in (self.session_id, self.run_id, self.event_id):
            if not isinstance(value, str) or (value and not _SAFE_ID.fullmatch(value)):
                raise ValueError("invalid pseudonym")
        if (
            not self.session_id
            or type(self.emitted_ns) is not int
            or not 0 <= self.emitted_ns < 2**63
        ):
            raise ValueError("invalid source event metadata")

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, value: dict) -> SourceEvent:
        fields = set(cls.__dataclass_fields__)
        if not isinstance(value, dict) or set(value) not in (fields, fields - {"goal"}):
            raise ValueError("invalid source event fields")
        return cls(
            **{
                **value,
                "source": SourceKind(value["source"]),
                "kind": EventKind(value["kind"]),
                "activity": ActivityKind(value["activity"]),
            }
        )

    def snapshot(self, elapsed_ms: int = 0) -> StateSnapshot:
        state, summary = {
            EventKind.OPEN: (AgentState.IDLE, "等待任务"),
            EventKind.PROMPT: (AgentState.STARTING, "正在开始任务"),
            EventKind.TOOL_START: (AgentState.WORKING, "正在执行工具"),
            EventKind.TOOL_END: (AgentState.THINKING, "正在整理工具结果"),
            EventKind.TOOL_ERROR: (AgentState.THINKING, "工具执行失败，正在处理"),
            EventKind.APPROVAL: (AgentState.WAITING_APPROVAL, "等待操作批准"),
            EventKind.INPUT: (AgentState.WAITING_USER, "等待用户输入"),
            EventKind.STOP: (AgentState.COMPLETED, "本轮回复结束"),
            EventKind.FAILURE: (AgentState.FAILED, "本轮执行失败"),
            EventKind.CANCEL: (AgentState.CANCELLED, "本轮已取消"),
            EventKind.CLOSE: (AgentState.IDLE, "会话已结束"),
        }[self.kind]
        return StateSnapshot(
            state=state,
            task=TaskInfo(
                self.session_id, self.goal or f"{self.source.label} 任务 {self.session_id[:4]}"
            ),
            activity=Activity(self.activity, summary),
            progress=Progress(
                ProgressMode.NONE
                if state
                in {
                    AgentState.IDLE,
                    AgentState.COMPLETED,
                    AgentState.FAILED,
                    AgentState.CANCELLED,
                }
                else ProgressMode.INDETERMINATE
            ),
            elapsed_ms=elapsed_ms,
            agent=AgentInfo(self.source.label),
        )


TOOL_KINDS = {
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
}
