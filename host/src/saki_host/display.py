"""Capability-gated v1 full-set snapshots, bounded to a single NDJSON frame."""

from __future__ import annotations

from copy import deepcopy

from .models import truncate_utf8
from .protocol import MAX_FRAME_BYTES, ProtocolCodec, ProtocolError, encode_frame
from .sessions import SessionRegistry

CAPABILITY = "multi-session"


def projection(registry: SessionRegistry, now: float) -> dict:
    visible = registry.visible()
    ids = {r.event.session_id for r in visible}
    return {
        "total": len(registry.records),
        "hidden_attention": sum(
            r.event.session_id not in ids
            and r.snapshot.state
            in {
                "waiting_approval",
                "waiting_user",
                "failed",
            }
            for r in registry.records.values()
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


def encode_projection(codec: ProtocolCodec, view: dict) -> dict:
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
