from __future__ import annotations

import re
from collections.abc import Callable, Mapping
from dataclasses import asdict, dataclass
from enum import StrEnum
from pathlib import Path
from types import MappingProxyType
from typing import Self

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
    CODEX = ("codex", "Codex")
    CLAUDE_CODE = ("claude_code", "Claude Code")

    def __new__(cls, value: str, label: str) -> Self:
        member = str.__new__(cls, value)
        member._value_ = value
        member._label = label
        return member

    @property
    def label(self) -> str:
        return self._label


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


class HookConfigFamily(StrEnum):
    JSON_COMMAND = "json_command"


@dataclass(frozen=True, slots=True)
class NativeHookFields:
    session_id: str
    run_id: str = ""
    event_id: str = ""
    tool_name: str = ""
    parent_id: str = ""
    task_title: str = ""
    prompt: str = ""


ClassifyHook = Callable[[str, Mapping[str, object], EventKind | None], EventKind | None]
ExtractHook = Callable[[Mapping[str, object]], NativeHookFields]


@dataclass(frozen=True, slots=True)
class AdapterSpec:
    source: SourceKind
    default_settings_path: tuple[str, ...]
    hook_events: tuple[str, ...]
    event_kinds: Mapping[str, EventKind]
    extract: ExtractHook
    classify: ClassifyHook
    tool_kinds: Mapping[str, ActivityKind]
    input_tools: frozenset[str]
    config_family: HookConfigFamily = HookConfigFamily.JSON_COMMAND

    def __post_init__(self) -> None:
        if not isinstance(self.source, SourceKind):
            raise TypeError("invalid adapter source")
        if (
            self.source.value == "legacy"
            or re.fullmatch(r"[a-z][a-z0-9_]{0,31}", self.source.value) is None
            or not 1 <= len(self.source.label.encode("utf-8")) <= 32
        ):
            raise ValueError("invalid adapter source metadata")
        if (
            not self.default_settings_path
            or any(
                not part or part in {".", ".."} or Path(part).name != part
                for part in self.default_settings_path
            )
        ):
            raise ValueError("invalid default settings path")
        if (
            not self.hook_events
            or len(set(self.hook_events)) != len(self.hook_events)
            or any(not isinstance(event, str) or not event for event in self.hook_events)
        ):
            raise ValueError("invalid hook events")
        if not callable(self.extract) or not callable(self.classify):
            raise TypeError("invalid adapter callbacks")
        event_kinds = dict(self.event_kinds)
        if not event_kinds or any(
            not isinstance(name, str) or not name or not isinstance(kind, EventKind)
            for name, kind in event_kinds.items()
        ):
            raise ValueError("invalid event kinds")
        if not set(self.hook_events).issubset(event_kinds):
            raise ValueError("installed hook lacks an event kind")
        tool_kinds = dict(self.tool_kinds)
        if any(
            not isinstance(name, str) or not name or not isinstance(kind, ActivityKind)
            for name, kind in tool_kinds.items()
        ):
            raise ValueError("invalid tool kinds")
        if any(not isinstance(name, str) or not name for name in self.input_tools):
            raise ValueError("invalid input tools")
        if not isinstance(self.config_family, HookConfigFamily):
            raise TypeError("invalid hook config family")
        object.__setattr__(self, "event_kinds", MappingProxyType(event_kinds))
        object.__setattr__(self, "tool_kinds", MappingProxyType(tool_kinds))

    @property
    def settings_path(self) -> Path:
        return Path.home().joinpath(*self.default_settings_path)


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
                self.session_id, self.goal or "未捕获任务描述"
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
