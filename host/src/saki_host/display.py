"""Capability-gated v1 full-set snapshots, bounded to a single NDJSON frame."""

from __future__ import annotations

import re
from copy import deepcopy

from .models import truncate_utf8
from .protocol import MAX_FRAME_BYTES, ProtocolCodec, ProtocolError, encode_frame
from .sessions import SessionRegistry

CAPABILITY = "multi-session"
GENERIC_SOURCE_CAPABILITY = "generic-source"
LEGACY_SOURCE = "legacy"
_KNOWN_045_SOURCES = frozenset({"codex", "claude_code"})
_SOURCE_KEY = re.compile(r"[a-z][a-z0-9_]{0,31}")
_PSEUDONYM = re.compile(r"[a-f0-9]{32}")


def projection(registry: SessionRegistry, now: float) -> dict:
    visible = registry.visible()
    displayable = registry.displayable()
    ids = {r.event.session_id for r in visible}
    return {
        "total": len(displayable),
        "hidden_attention": sum(
            r.event.session_id not in ids
            and r.snapshot.state
            in {
                "waiting_approval",
                "waiting_user",
                "failed",
            }
            for r in displayable
        ),
        "capacity_rejected": registry.rejected,
        "sessions": [
            {
                "source": r.event.source.value,
                "run_id": r.run_id[:32],
                "revision": r.revision,
                "fresh": not r.stale and not r.closed,
                **r.current(now).to_payload(),
            }
            for r in visible
        ],
    }


def _prepare_sources(result: dict, *, generic_source: bool) -> None:
    for item in result["sessions"]:
        if not isinstance(item, dict):
            raise ProtocolError("invalid session item")
        source = item.get("source")
        if not isinstance(source, str) or _SOURCE_KEY.fullmatch(source) is None:
            raise ProtocolError("invalid source key")
        task = item.get("task")
        task_id = task.get("id") if isinstance(task, dict) else None
        if source != LEGACY_SOURCE and (
            not isinstance(task_id, str) or _PSEUDONYM.fullmatch(task_id) is None
        ):
            raise ProtocolError("non-legacy source requires a pseudonymous task id")
        agent = item.get("agent")
        if source == LEGACY_SOURCE or (not generic_source and source not in _KNOWN_045_SOURCES):
            item["source"] = LEGACY_SOURCE
            item["agent"] = {**agent, "name": "Agent"} if isinstance(agent, dict) else {"name": "Agent"}
            continue
        if generic_source:
            name = agent.get("name") if isinstance(agent, dict) else None
            if not isinstance(name, str) or not name or len(name.encode("utf-8")) > 32:
                raise ProtocolError("generic source requires a bounded agent name")


def encode_projection(
    codec: ProtocolCodec, view: dict, *, generic_source: bool = False
) -> dict:
    """Shorten only optional display text on UTF-8 boundaries, never identities.

    Legacy status encoding/limits are unchanged. Every sessions frame is a full
    replacement including empty/optional fields; no cross-message fragments.
    """
    result = {
        "v": 1,
        "type": "sessions",
        "id": codec._id(),
        "session": codec.session,
        "seq": codec._seq(),
        **deepcopy(view),
    }
    if (
        not isinstance(result["sessions"], list)
        or len(result["sessions"]) > 4
        or not len(result["sessions"]) <= result["total"] <= 32
    ):
        raise ProtocolError("invalid display projection")
    _prepare_sources(result, generic_source=generic_source)
    texts = []
    for item in result["sessions"]:
        for parent, key, minimum in (
            ("activity", "detail", 0),
            ("progress", "label", 0),
            ("agent", "model", 0),
            ("activity", "summary", 0),
            ("task", "title", 1),
        ):
            field = item.get(parent, {})
            if isinstance(field.get(key), str):
                texts.append((field, key, minimum))
    while True:
        try:
            encode_frame(result)
            return result
        except ProtocolError:
            choices = [
                (field, key, minimum)
                for field, key, minimum in texts
                if len(field[key].encode()) > minimum
            ]
            if not choices:
                raise ProtocolError(
                    f"projection metadata exceeds {MAX_FRAME_BYTES} bytes"
                ) from None
            field, key, minimum = max(choices, key=lambda entry: len(entry[0][entry[1]].encode()))
            shortened = truncate_utf8(field[key], max(minimum, len(field[key].encode()) - 16))
            field[key] = shortened if shortened or not minimum else "?"
