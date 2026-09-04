from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from .models import (
    Activity,
    ActivityKind,
    AgentInfo,
    AgentState,
    Progress,
    ProgressMode,
    StateSnapshot,
    TaskInfo,
)
from .privacy import redact_text

_USAGE_LIMIT_MARKERS = (
    "usage limit",
    "rate limit",
    "quota",
    "credit",
    "额度",
    "上限",
    "余额",
    "配额",
)


def _task(payload: Mapping[str, Any]) -> TaskInfo:
    task_id = str(payload.get("thread_id") or payload.get("session_id") or "codex-task")
    title = str(payload.get("task_title") or payload.get("prompt") or "Codex task")
    normalized_title = redact_text(title).strip()
    return TaskInfo(id=task_id, title=normalized_title or "Codex task")


def _tool_kind(tool_name: str) -> ActivityKind:
    lowered = tool_name.lower()
    if any(name in lowered for name in ("patch", "edit", "write")):
        return ActivityKind.EDIT
    if any(name in lowered for name in ("shell", "bash", "exec", "command", "terminal")):
        return ActivityKind.SHELL
    if any(name in lowered for name in ("web", "browser", "search", "fetch")):
        return ActivityKind.WEB
    if "test" in lowered:
        return ActivityKind.TEST
    if any(name in lowered for name in ("read", "view", "list", "find")):
        return ActivityKind.READ
    return ActivityKind.OTHER


def snapshot_from_codex_hook(
    event_name: str,
    payload: Mapping[str, Any],
    *,
    elapsed_ms: int | None = None,
) -> StateSnapshot:
    """Map a Codex lifecycle hook into a display snapshot.

    Hook payload fields can evolve. Unknown or absent fields deliberately fall
    back to generic, privacy-safe display text.
    """
    task = _task(payload)
    agent = AgentInfo(name="Codex", model=str(payload.get("model") or ""))
    tool_name = str(payload.get("tool_name") or payload.get("tool") or "tool")

    if event_name == "SessionStart":
        return StateSnapshot(state=AgentState.IDLE, agent=agent)
    if event_name == "UserPromptSubmit":
        return StateSnapshot(
            state=AgentState.STARTING,
            task=task,
            activity=Activity(ActivityKind.PLAN, "正在开始任务"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms or 0,
            agent=agent,
        )
    if event_name == "PreToolUse":
        return StateSnapshot(
            state=AgentState.WORKING,
            task=task,
            activity=Activity(_tool_kind(tool_name), f"正在使用 {redact_text(tool_name)}"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "PostToolUse":
        return StateSnapshot(
            state=AgentState.THINKING,
            task=task,
            activity=Activity(ActivityKind.PLAN, "正在整理工具结果"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "PermissionRequest":
        return StateSnapshot(
            state=AgentState.WAITING_APPROVAL,
            task=task,
            activity=Activity(
                _tool_kind(tool_name),
                "等待操作批准",
                redact_text(str(payload.get("reason") or tool_name)),
            ),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "SubagentStart":
        return StateSnapshot(
            state=AgentState.WORKING,
            task=task,
            activity=Activity(ActivityKind.OTHER, "子 Agent 正在工作"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "SubagentStop":
        return StateSnapshot(
            state=AgentState.THINKING,
            task=task,
            activity=Activity(ActivityKind.PLAN, "正在汇总子 Agent 结果"),
            progress=Progress(ProgressMode.INDETERMINATE),
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "Stop":
        reason = redact_text(
            " ".join(
                str(payload.get(field) or "")
                for field in ("reason", "stop_reason", "error", "message")
            )
        ).lower()
        if any(marker in reason for marker in _USAGE_LIMIT_MARKERS):
            state = AgentState.WAITING_USER
            summary = "使用额度已耗尽，请在 Mac 上重试"
        elif any(word in reason for word in ("error", "failed", "failure")):
            state = AgentState.FAILED
            summary = "任务执行失败"
        elif any(word in reason for word in ("cancel", "abort", "interrupt")):
            state = AgentState.CANCELLED
            summary = "任务已取消"
        else:
            state = AgentState.COMPLETED
            summary = "任务已完成"
        progress = (
            Progress(ProgressMode.INDETERMINATE)
            if state is AgentState.WAITING_USER
            else Progress(
                ProgressMode.DETERMINATE,
                percent=100 if state is AgentState.COMPLETED else 0,
            )
        )
        return StateSnapshot(
            state=state,
            task=task,
            activity=Activity(ActivityKind.MESSAGE, summary),
            progress=progress,
            elapsed_ms=elapsed_ms,
            agent=agent,
        )
    if event_name == "SessionEnd":
        return StateSnapshot(state=AgentState.IDLE, agent=agent)

    return StateSnapshot(
        state=AgentState.THINKING,
        task=task,
        activity=Activity(ActivityKind.OTHER, "Agent 状态已更新"),
        progress=Progress(ProgressMode.INDETERMINATE),
        elapsed_ms=elapsed_ms,
        agent=agent,
    )
