from __future__ import annotations

import copy

import pytest
from test_schema import message_validator
from test_sources import event

from saki_host.adapters import SourceKind
from saki_host.display import encode_projection, projection
from saki_host.protocol import ProtocolCodec, ProtocolError, encode_frame
from saki_host.protocol_session import ProtocolSession, ProtocolSessionError
from saki_host.sessions import SessionRegistry


def mixed_projection():
    registry = SessionRegistry()
    sources = tuple(SourceKind)
    for index in range(4):
        registry.accept(event(source=sources[index % len(sources)], sid=str(index)), now=0)
    return projection(registry, 1)


def test_complete_mixed_set_schema_and_empty_clear():
    codec = ProtocolCodec()
    view = mixed_projection()
    encoded = encode_projection(codec, view)
    message_validator().validate(encoded)
    empty = encode_projection(codec, {**view, "sessions": [], "total": 0})
    message_validator().validate(empty)
    assert empty["seq"] > encoded["seq"]
    assert {r["source"] for r in encoded["sessions"]} == {
        source.value for source in tuple(SourceKind)[:4]
    }


def test_suppressed_stale_session_is_not_counted_as_recent():
    registry = SessionRegistry(stale_after=10, stale_retention=20)
    registry.accept(event(sid="quota-limited"), now=0)
    registry.tick(now=10)
    assert projection(registry, 20)["total"] == 1
    registry.tick(now=30)
    view = projection(registry, 30)
    assert view["total"] == 0
    assert view["sessions"] == []


def test_worst_case_escaping_fits_without_mutating_original_or_identities():
    view = mixed_projection()
    for item in view["sessions"]:
        item["task"]["title"] = "中" * 53
        item["activity"] = {"summary": '\\"\t' * 80, "detail": '\\"\t' * 170}
        item["agent"]["model"] = "m" * 64
        item["elapsed_ms"] = 9007199254740991
    original = copy.deepcopy(view)
    codec = ProtocolCodec("ffffffff-ffff-4fff-8fff-ffffffffffff")
    codec._next_id = codec._next_seq = 4294967295
    message = encode_projection(codec, view)
    message_validator().validate(message)
    assert len(encode_frame(message)) <= 2049
    assert view == original
    assert [item["task"]["id"] for item in message["sessions"]] == [
        item["task"]["id"] for item in view["sessions"]
    ]
    assert all(item["task"]["title"] for item in message["sessions"])


def test_generic_source_is_preserved_or_safely_downgraded_by_capability():
    view = mixed_projection()
    view["sessions"][0]["source"] = "demo_agent"
    view["sessions"][0]["agent"]["name"] = "演示 Agent"
    original = copy.deepcopy(view)

    generic = encode_projection(ProtocolCodec(), view, generic_source=True)
    legacy = encode_projection(ProtocolCodec(), view, generic_source=False)

    assert generic["sessions"][0]["source"] == "demo_agent"
    assert generic["sessions"][0]["agent"]["name"] == "演示 Agent"
    assert legacy["sessions"][0]["source"] == "legacy"
    assert legacy["sessions"][0]["agent"]["name"] == "Agent"
    assert view == original
    message_validator().validate(generic)
    message_validator().validate(legacy)


def test_generic_source_metadata_is_bounded_before_encoding():
    view = mixed_projection()
    item = view["sessions"][0]
    item["source"] = "demo_agent"
    item["agent"]["name"] = "A" * 32
    message_validator().validate(
        encode_projection(ProtocolCodec(), view, generic_source=True)
    )
    item["agent"]["name"] = "演" * 10
    message_validator().validate(
        encode_projection(ProtocolCodec(), view, generic_source=True)
    )
    item["agent"]["name"] = "演" * 11
    with pytest.raises(ProtocolError, match="agent name"):
        encode_projection(ProtocolCodec(), view, generic_source=True)
    item["agent"]["name"] = ""
    with pytest.raises(ProtocolError, match="agent name"):
        encode_projection(ProtocolCodec(), view, generic_source=True)
    item["agent"]["name"] = "Demo Agent"
    item["source"] = "Demo-Agent"
    with pytest.raises(ProtocolError, match="source key"):
        encode_projection(ProtocolCodec(), view, generic_source=True)
    item["source"] = "demo_agent"
    item["task"]["id"] = "raw-id"
    with pytest.raises(ProtocolError, match="pseudonymous"):
        encode_projection(ProtocolCodec(), view, generic_source=True)


@pytest.mark.asyncio
async def test_commit_ack_recovery_requires_matching_sequence():
    session = ProtocolSession(None)
    response = {"ok": True, "applied": False, "committed": True, "last_seq": 1}

    async def request(_message, _expected):
        return response

    session.request = request
    assert (await session.apply_projection(mixed_projection()))["committed"]
    with pytest.raises(ProtocolSessionError):
        await session.apply_projection(mixed_projection())


@pytest.mark.asyncio
async def test_service_freezes_mixed_set_and_retains_events_during_slow_ack():
    import asyncio
    from types import SimpleNamespace

    from saki_host.service import SakiHostService, ServiceConfig
    from saki_host.transports.base import TransportKind

    service = SakiHostService(ServiceConfig())
    sources = tuple(SourceKind)
    for index in range(4):
        service.accept_source_event(event(sid=str(index), source=sources[index % len(sources)]))
    sent = []
    started = asyncio.Event()
    release = asyncio.Event()

    async def apply(view):
        sent.append(copy.deepcopy(view))
        started.set()
        await release.wait()
        return {"applied": True, "last_seq": len(sent)}

    link = SimpleNamespace(
        multi_session=True, kind=TransportKind.BLE, session=SimpleNamespace(apply_projection=apply)
    )
    pending = asyncio.create_task(service._sync_link(link))
    await started.wait()
    first_generation = service.mailbox.generation
    service.accept_source_event(
        event("PermissionRequest", sid="1", source=SourceKind.CLAUDE_CODE, ns=2)
    )
    service.accept_source_event(event("Stop", sid="0", ns=2))
    assert all(item["state"] == "starting" for item in sent[0]["sessions"])
    release.set()
    assert await pending == first_generation
    assert service.mailbox.generation > first_generation
    assert await service._sync_link(link) == service.mailbox.generation
    assert len(sent[1]["sessions"]) == 4
    assert sent[1]["sessions"][0]["task"]["id"] == sent[0]["sessions"][0]["task"]["id"]
    assert any(item["state"] == "waiting_approval" for item in sent[1]["sessions"])
    assert any(item["state"] == "completed" for item in sent[1]["sessions"])


@pytest.mark.asyncio
async def test_old_device_uses_stable_focus_and_never_new_message_family():
    from types import SimpleNamespace

    from saki_host.service import SakiHostService, ServiceConfig
    from saki_host.transports.base import TransportKind

    service = SakiHostService(ServiceConfig())
    service.accept_source_event(event(sid="waiting"))
    service.accept_source_event(event("PermissionRequest", sid="waiting", ns=2))
    service.accept_source_event(event(sid="busy", source=SourceKind.CLAUDE_CODE))
    captured = []

    async def apply(snapshot):
        captured.append(snapshot)
        return {"applied": True, "last_seq": len(captured)}

    link = SimpleNamespace(
        multi_session=False, kind=TransportKind.USB, session=SimpleNamespace(apply_status=apply)
    )
    await service._sync_link(link)
    service.accept_source_event(
        event("PreToolUse", sid="busy", source=SourceKind.CLAUDE_CODE, ns=2)
    )
    await service._sync_link(link)
    assert captured[0].task.id == captured[1].task.id
    assert captured[-1].state == "working"


def test_main_is_not_displaced_by_hidden_attention():
    registry = SessionRegistry()
    for i in range(6):
        registry.accept(event(sid=str(i)), now=i)
        registry.accept(event("PermissionRequest", sid=str(i), ns=2), now=i + 0.1)
    registry.accept(event(sid="main"), now=7)
    view = projection(registry, 7)
    assert view["sessions"][0]["task"]["id"] == event(sid="main").session_id
    assert view["hidden_attention"] == 3
    assert view["total"] == 7
    encoded = encode_projection(ProtocolCodec(), view)
    assert encoded["sessions"][0] == view["sessions"][0]


def test_registry_and_device_capacity_do_not_depend_on_source_count():
    registry = SessionRegistry()
    sources = tuple(SourceKind)
    for index in range(32):
        registry.accept(
            event(sid=f"capacity-{index}", source=sources[index % len(sources)]),
            now=index,
        )
    for index in range(10):
        registry.accept(
            event(
                "PermissionRequest",
                sid=f"capacity-{index}",
                source=sources[index % len(sources)],
                ns=2,
            ),
            now=33 + index,
        )
    view = projection(registry, 50)
    encoded = encode_projection(ProtocolCodec(), view)
    assert len(registry.records) == 32
    assert view["total"] == 32
    assert len(view["sessions"]) == 4
    assert view["hidden_attention"] == 7
    assert len(encode_frame(encoded)) <= 2049


@pytest.mark.parametrize("source", list(SourceKind))
def test_unstarted_session_never_displaces_completed_task(source):
    registry = SessionRegistry()
    other = next(s for s in SourceKind if s != source)
    done = event(sid="done", source=other)
    unopened = event("SessionStart", sid="unstarted", source=source)
    registry.accept(done, now=0)
    registry.accept(event("Stop", sid="done", source=other, ns=2), now=1)
    registry.accept(unopened, now=2)
    view = projection(registry, 2)
    assert [r["task"]["id"] for r in view["sessions"]] == [done.session_id]
    assert view["sessions"][0]["state"] == "completed"
    # The device footer counts recent displayable sessions, not identities that
    # were only registered when an Agent client opened.
    assert view["total"] == 1
    message_validator().validate(encode_projection(ProtocolCodec(), view))
    registry.tick(now=60)
    assert registry.focus(60).task.id == done.session_id
    registry.tick(now=61)
    assert registry.visible() == []
    assert registry.focus(61).state == "idle"
    assert unopened.session_id in registry.records
    message_validator().validate(encode_projection(ProtocolCodec(), projection(registry, 61)))
    registry.accept(event(source=source, sid="unstarted", ns=2), now=62)
    assert registry.focus(62).task.id == unopened.session_id
    assert registry.focus(62).state == "starting"


def test_idle_filter_survives_restart_close_and_legacy_checkpoint():
    registry = SessionRegistry()
    registry.accept(event("SessionStart", sid="idle"), now=0)
    registry.accept(event("SessionStart", sid="closed"), now=0)
    registry.accept(event("SessionEnd", sid="closed", ns=2), now=1)
    saved = registry.checkpoint(now=2, wall=100)
    for item in saved["records"]:
        item.pop("submitted_order")
    restored = SessionRegistry()
    restored.restore(saved, now=0, wall=101)
    assert len(restored.records) == 2
    assert restored.visible() == []
    restored.accept(event("SessionStart", sid="idle", ns=3), now=1)
    assert restored.visible() == []
    restored.accept(event("PreToolUse", sid="idle", ns=4), now=2)
    assert restored.focus(2).state == "working"
    restored.accept(event("SessionStart", sid="idle", ns=5), now=3)
    assert restored.focus(3).state == "working"


def test_missing_prompt_falls_back_to_attention_without_stealing_submitted_focus():
    registry = SessionRegistry()
    for sid, name in (("wait", "PermissionRequest"), ("done", "Stop"), ("work", "PreToolUse")):
        registry.accept(event("SessionStart", sid=sid), now=0)
        registry.accept(event(name, sid=sid, ns=2), now=1)
    assert [r.snapshot.state for r in registry.visible()] == [
        "waiting_approval", "working", "completed"
    ]
    registry.accept(event(sid="main"), now=2)
    registry.accept(event("Stop", sid="main", ns=2), now=3)
    registry.accept(event("PreToolUse", sid="work", ns=3), now=4)
    assert registry.focus(4).task.id == event(sid="main").session_id
    assert len(registry.visible()) == 4
