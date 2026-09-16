"""Bounded, source-isolated display sessions independent of device transport."""

from __future__ import annotations

import json
import os
import re
import stat
import tempfile
import time
from collections import OrderedDict
from dataclasses import dataclass, replace
from pathlib import Path

from .adapters.base import EventKind, SourceEvent
from .models import Activity, AgentState, StateSnapshot

RUNNING = {AgentState.STARTING, AgentState.THINKING, AgentState.WORKING}
TIMED = RUNNING | {AgentState.WAITING_USER, AgentState.WAITING_APPROVAL}
_STATE_KIND = {
    AgentState.IDLE: EventKind.OPEN,
    AgentState.STARTING: EventKind.PROMPT,
    AgentState.THINKING: EventKind.TOOL_END,
    AgentState.WORKING: EventKind.TOOL_START,
    AgentState.WAITING_USER: EventKind.INPUT,
    AgentState.WAITING_APPROVAL: EventKind.APPROVAL,
    AgentState.COMPLETED: EventKind.STOP,
    AgentState.FAILED: EventKind.FAILURE,
    AgentState.CANCELLED: EventKind.CANCEL,
}
_RANK = {
    AgentState.WAITING_APPROVAL: 0,
    AgentState.WAITING_USER: 1,
    AgentState.FAILED: 2,
    AgentState.STARTING: 3,
    AgentState.THINKING: 3,
    AgentState.WORKING: 3,
    AgentState.COMPLETED: 4,
    AgentState.CANCELLED: 4,
    AgentState.IDLE: 5,
}


@dataclass
class SessionRecord:
    event: SourceEvent
    snapshot: StateSnapshot
    run_id: str
    revision: int
    updated_at: float
    started_at: float
    rank_since: float
    expires_at: float | None = None
    stale: bool = False
    restored_at: float | None = None
    closed: bool = False
    submitted_order: int = 0

    def current(self, now: float) -> StateSnapshot:
        elapsed = self.snapshot.elapsed_ms or 0
        if not self.stale and not self.closed and self.snapshot.state in TIMED:
            elapsed = max(elapsed, int((now - self.started_at) * 1000))
        result = replace(self.snapshot, elapsed_ms=elapsed)
        if self.stale:
            result = replace(result, activity=Activity(summary="状态可能过期，请检查 Mac"))
        return result


class SessionRegistry:
    def __init__(self, *, capacity: int = 32, stale_after: float = 120.0) -> None:
        if not 1 <= capacity <= 32 or stale_after < 0:
            raise ValueError("invalid session capacity or stale timeout")
        self.capacity = capacity
        self.stale_after = stale_after
        self.records: dict[str, SessionRecord] = {}
        self.tombstones: OrderedDict[str, float] = OrderedDict()
        self.seen: OrderedDict[str, None] = OrderedDict()
        self.old_runs: OrderedDict[str, None] = OrderedDict()
        self.generation = 0
        self.rejected = 0
        self._submission_order = 0

    def _remember(self, cache: OrderedDict, key: str, value: object = None) -> None:
        if key:
            cache[key] = value
            cache.move_to_end(key)
            while len(cache) > 256:
                cache.popitem(last=False)

    def remove(self, sid: str, now: float) -> bool:
        if sid not in self.records:
            return False
        del self.records[sid]
        self._remember(self.tombstones, sid, now + 86400)
        self.generation += 1
        return True

    def accept(self, event: SourceEvent, *, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        self.tick(now=now)
        if event.event_id and event.event_id in self.seen:
            return False
        record = self.records.get(event.session_id)
        opening = event.kind in {EventKind.OPEN, EventKind.PROMPT}
        if record is None:
            if not opening:
                return False
            if len(self.records) >= self.capacity:
                evictable = [
                    r
                    for r in self.records.values()
                    if r.closed
                    or r.snapshot.state
                    in {
                        AgentState.COMPLETED,
                        AgentState.CANCELLED,
                    }
                ]
                if not evictable:
                    self.rejected = min(2**32 - 1, self.rejected + 1)
                    self.generation += 1
                    return False
                oldest = min(evictable, key=lambda r: (r.updated_at, r.event.session_id))
                self.remove(oldest.event.session_id, now)
            self.tombstones.pop(event.session_id, None)
            snapshot = event.snapshot()
            record = SessionRecord(
                event, snapshot, event.run_id or f"local-{event.emitted_ns}", 0, now, now, now
            )
            self.records[event.session_id] = record
        else:
            if record.event.source != event.source:
                return False
            if record.restored_at is None and event.emitted_ns <= record.event.emitted_ns:
                return False
            if record.closed and not opening:
                return False
            if event.run_id and event.run_id in self.old_runs:
                return False
            if (
                event.run_id
                and not record.run_id.startswith("local-")
                and event.run_id != record.run_id
                and event.kind not in {EventKind.PROMPT, EventKind.OPEN}
                and record.restored_at is None
            ):
                return False
            if event.kind is EventKind.OPEN and not record.closed:
                # Resume/compaction observes the same session, without starting a turn.
                record.event = replace(event, goal=record.event.goal)
                record.updated_at = now
                record.revision = (record.revision + 1) & 0xFFFFFFFF
                self._remember(self.seen, event.event_id)
                self.generation += 1
                return True
            if event.kind is EventKind.STOP and record.snapshot.state is AgentState.COMPLETED:
                return False
            if event.kind is EventKind.PROMPT or (event.kind is EventKind.OPEN and record.closed):
                if event.run_id and event.run_id == record.run_id:
                    return False
                self._remember(self.old_runs, record.run_id)
                record.run_id = event.run_id or f"local-{event.emitted_ns}"
                record.started_at = now
            elif record.restored_at is not None or record.stale:
                record.started_at = now - (record.snapshot.elapsed_ms or 0) / 1000
        # Tool/Stop/resume events lack a prompt. Keep the goal across the entire
        # run and brief continuation requests; only a new meaningful prompt replaces it.
        event = replace(
            event,
            goal=event.goal if event.kind is EventKind.PROMPT and event.goal else record.event.goal,
        )
        previous_rank = _RANK[record.snapshot.state]
        if event.kind is EventKind.PROMPT:
            self._submission_order += 1
            record.submitted_order = self._submission_order
        elapsed = max(0, int((now - record.started_at) * 1000))
        if event.kind is EventKind.CLOSE:
            record.snapshot = replace(record.current(now), activity=Activity(summary="会话已结束"))
            record.closed = True
        else:
            record.snapshot = event.snapshot(elapsed)
            record.closed = False
        record.stale = False
        record.restored_at = None
        record.event = event
        record.updated_at = now
        record.revision = (record.revision + 1) & 0xFFFFFFFF
        if previous_rank != _RANK[record.snapshot.state]:
            record.rank_since = now
        record.expires_at = (
            now + 60
            if record.closed
            or record.snapshot.state
            in {
                AgentState.COMPLETED,
                AgentState.CANCELLED,
            }
            else None
        )
        self._remember(self.seen, event.event_id)
        self.generation += 1
        return True

    def tick(self, *, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        generation = self.generation
        for sid, record in list(self.records.items()):
            if (
                (record.expires_at is not None and now >= record.expires_at)
                or (record.restored_at is not None and now - record.restored_at >= 86400)
                or (record.snapshot.state is AgentState.IDLE and now - record.updated_at >= 600)
            ):
                self.remove(sid, now)
            elif (
                self.stale_after > 0
                and not record.stale
                and not record.closed
                and record.snapshot.state in RUNNING
                and now - record.updated_at >= self.stale_after
            ):
                # Freeze at the watchdog boundary, even if this tick is delayed.
                record.snapshot = record.current(record.updated_at + self.stale_after)
                record.stale = True
                record.revision = (record.revision + 1) & 0xFFFFFFFF
                self.generation += 1
        for sid, expires in list(self.tombstones.items()):
            if now >= expires:
                del self.tombstones[sid]
        return self.generation != generation

    def visible(self) -> list[SessionRecord]:
        # Opening a client registers its identity, but is not a task. Derive
        # eligibility from persisted state so old checkpoints need no migration.
        records = [
            r for r in self.records.values()
            if r.submitted_order > 0 or r.snapshot.state is not AgentState.IDLE
        ]
        if not records:
            return []
        def attention_key(record: SessionRecord) -> tuple[int, float, str]:
            return (_RANK[record.snapshot.state], record.rank_since, record.event.session_id)

        # Accepted submissions retain focus, including their completion window.
        # If no prompt was observed, prefer meaningful activity over creation order.
        submitted = [r for r in records if r.submitted_order > 0]
        main = (
            max(submitted, key=lambda r: r.submitted_order)
            if submitted else min(records, key=attention_key)
        )
        return [
            main,
            *sorted(
                (record for record in records if record is not main),
                key=attention_key,
            )[:3],
        ]

    def focus(self, now: float | None = None) -> StateSnapshot:
        items = self.visible()
        return (
            items[0].current(time.monotonic() if now is None else now)
            if items
            else StateSnapshot(
                state=AgentState.IDLE,
            )
        )

    def checkpoint(self, *, now: float | None = None, wall: float | None = None) -> dict:
        now = time.monotonic() if now is None else now
        wall = time.time() if wall is None else wall
        return {
            "v": 1,
            "saved_at": wall,
            "records": [
                {
                    "event": r.event.to_dict(),
                    "display_kind": _STATE_KIND[r.snapshot.state].value,
                    "run_id": r.run_id,
                    "revision": r.revision,
                    "elapsed_ms": r.current(now).elapsed_ms or 0,
                    "closed": r.closed,
                    "submitted_order": r.submitted_order,
                    "expires_in": max(0, r.expires_at - now) if r.expires_at is not None else None,
                }
                for r in self.records.values()
            ],
        }

    def restore(self, value: dict, *, now: float | None = None, wall: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        wall = time.time() if wall is None else wall
        if (
            not isinstance(value, dict)
            or value.get("v") != 1
            or not isinstance(value.get("records"), list)
            or len(value["records"]) > self.capacity
            or type(value.get("saved_at")) not in (int, float)
        ):
            raise ValueError("invalid checkpoint")
        age = wall - value["saved_at"]
        if not 0 <= age < 86400:
            return
        restored = {}
        for item in value["records"]:
            event = SourceEvent.from_dict(item["event"])
            display_kind = EventKind(item["display_kind"])
            run_id, revision = item["run_id"], item["revision"]
            elapsed, closed, expires = item["elapsed_ms"], item["closed"], item["expires_in"]
            submitted_order = item.get("submitted_order", 0)
            if (
                not isinstance(run_id, str)
                or not re.fullmatch(r"[a-zA-Z0-9_-]{1,32}", run_id)
                or type(revision) is not int
                or not 0 <= revision <= 0xFFFFFFFF
                or type(elapsed) is not int
                or not 0 <= elapsed <= 9007199254740991
                or type(closed) is not bool
                or type(submitted_order) is not int
                or not 0 <= submitted_order <= 9007199254740991
                or event.session_id in restored
                or (
                    expires is not None
                    and (type(expires) not in (int, float) or not 0 <= expires <= 60)
                )
            ):
                raise ValueError("invalid checkpoint record")
            if expires is not None and expires <= age:
                continue
            restored[event.session_id] = SessionRecord(
                event,
                replace(event, kind=display_kind).snapshot(elapsed),
                run_id,
                (revision + 1) & 0xFFFFFFFF,
                now,
                now - elapsed / 1000,
                now,
                expires_at=now + expires - age if expires is not None else None,
                stale=True,
                restored_at=now - age,
                closed=closed,
                submitted_order=submitted_order,
            )
        self.records = restored
        self._submission_order = max((r.submitted_order for r in restored.values()), default=0)
        self.generation += 1


def save_checkpoint(path: Path, value: dict) -> None:
    data = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()
    if len(data) > 65536:
        raise ValueError("checkpoint too large")
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".sessions-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def load_checkpoint(path: Path) -> dict:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError("checkpoint permissions are invalid")
        data = os.read(fd, 65537)
        if len(data) > 65536:
            raise ValueError("checkpoint too large")
        return json.loads(data)
    finally:
        os.close(fd)
