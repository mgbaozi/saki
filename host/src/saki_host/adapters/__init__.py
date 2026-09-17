"""Fixed, explicit source adapters; never infer a product from a tool name."""

from __future__ import annotations

import time
from collections.abc import Mapping
from types import MappingProxyType

from ..identity import pseudonym
from ..models import ActivityKind
from ..task_goal import display_goal
from .base import AdapterSpec, EventKind, SourceEvent, SourceKind
from .claude_code import SPEC as CLAUDE_CODE_SPEC
from .codex import SPEC as CODEX_SPEC


def _build_registry(specs: tuple[AdapterSpec, ...]) -> Mapping[SourceKind, AdapterSpec]:
    registry: dict[SourceKind, AdapterSpec] = {}
    wire_keys: set[str] = set()
    for spec in specs:
        if spec.source in registry or spec.source.value in wire_keys:
            raise RuntimeError("duplicate source adapter")
        registry[spec.source] = spec
        wire_keys.add(spec.source.value)
    if set(registry) != set(SourceKind):
        raise RuntimeError("source adapter registry is incomplete")
    return MappingProxyType(registry)


ADAPTERS = _build_registry((CODEX_SPEC, CLAUDE_CODE_SPEC))


def adapter_for(source: SourceKind) -> AdapterSpec:
    if not isinstance(source, SourceKind):
        raise TypeError("invalid source")
    return ADAPTERS[source]


def normalize_hook(
    source: SourceKind, name: str, payload: dict, key: bytes, *, emitted_ns: int | None = None
) -> SourceEvent | None:
    if not isinstance(payload, dict):
        raise TypeError("hook must be an object")
    spec = adapter_for(source)
    kind = spec.classify(name, payload, spec.event_kinds.get(name))
    if kind is None:
        return None
    fields = spec.extract(payload)
    # Child identities are not independent user-visible sessions in this release.
    if fields.parent_id:
        return None
    activity = spec.tool_kinds.get(fields.tool_name, ActivityKind.OTHER)
    if kind is EventKind.TOOL_START and fields.tool_name in spec.input_tools:
        kind = EventKind.INPUT
    return SourceEvent(
        source,
        pseudonym(key, source, "session", fields.session_id),
        kind,
        time.monotonic_ns() if emitted_ns is None else emitted_ns,
        pseudonym(key, source, "run", fields.run_id) if fields.run_id else "",
        pseudonym(key, source, "event", fields.event_id) if fields.event_id else "",
        activity,
        (display_goal(fields.task_title) or display_goal(fields.prompt))
        if kind is EventKind.PROMPT
        else "",
    )


__all__ = ["ADAPTERS", "SourceEvent", "SourceKind", "adapter_for", "normalize_hook"]
