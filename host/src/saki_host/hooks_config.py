"""Reversible hook installation. Never print settings (they may contain credentials)."""

from __future__ import annotations

import copy
import json
import os
import shlex
import stat
import sys
import tempfile
from pathlib import Path

from .adapters import SourceKind, adapter_for
from .adapters.base import HookConfigFamily
from .identity import DEFAULT_STATE_DIR, initialize_identity_key, load_identity_key


def marker(source: SourceKind) -> str:
    return f" # saki-observer:{source.value}"


def merge_hooks(settings: dict, source: SourceKind, wrapper: Path, *, install: bool) -> dict:
    if not isinstance(settings, dict) or not isinstance(settings.get("hooks", {}), dict):
        raise TypeError("settings/hooks must be an object")
    result = copy.deepcopy(settings)
    hooks = result.setdefault("hooks", {})
    for event, groups in list(hooks.items()):
        if not isinstance(groups, list):
            raise TypeError("hook groups must be arrays")
        kept = []
        for group in groups:
            if not isinstance(group, dict) or not isinstance(group.get("hooks"), list):
                raise TypeError("invalid hook group")
            remaining = []
            removed = False
            for handler in group["hooks"]:
                if not isinstance(handler, dict):
                    raise TypeError("invalid hook handler")
                command = handler.get("command", "")
                if (
                    handler.get("type") == "command"
                    and isinstance(command, str)
                    and command.endswith(marker(source))
                ):
                    removed = True
                else:
                    remaining.append(handler)
            if remaining or not removed:
                kept.append({**group, "hooks": remaining})
        if kept:
            hooks[event] = kept
        else:
            del hooks[event]
    if install:
        spec = adapter_for(source)
        if spec.config_family is not HookConfigFamily.JSON_COMMAND:
            raise ValueError("unsupported hook config family")
        for event in spec.hook_events:
            command = f"{shlex.quote(str(wrapper.resolve()))} {event}{marker(source)}"
            hooks.setdefault(event, []).append(
                {
                    "hooks": [
                        {"type": "command", "command": command, "timeout": 2},
                    ]
                }
            )
    return result


def _read_settings(path: Path) -> tuple[dict, bytes | None]:
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return {}, None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError("settings must be a regular file")
        data = os.read(fd, 1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            raise ValueError("settings too large")
        value = json.loads(data)
        if not isinstance(value, dict):
            raise TypeError("settings must be an object")
        return value, data
    finally:
        os.close(fd)


def _atomic_write(path: Path, data: bytes, mode: int = 0o600) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".saki-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _observer_script(source: SourceKind, state_dir: Path) -> str:
    command = shlex.join(
        [
            sys.executable,
            "-m",
            "saki_host",
            "hook",
            "--source",
            source.value,
            "--identity-key",
            str(state_dir / "identity.key"),
        ]
    )
    return f'#!/bin/sh\n{command} "$@" >/dev/null 2>/dev/null\nexit 0\n'


def _wrapper_ready(wrapper: Path, expected: str) -> bool:
    try:
        fd = os.open(wrapper, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            info = os.fstat(fd)
            return (
                stat.S_ISREG(info.st_mode)
                and info.st_uid == os.getuid()
                and info.st_mode & 0o777 == 0o700
                and os.read(fd, 8193) == expected.encode()
            )
        finally:
            os.close(fd)
    except OSError:
        return False


def configure_hooks(
    path: Path, source: SourceKind, action: str, *, state_dir: Path = DEFAULT_STATE_DIR
) -> dict:
    if action not in {"install", "uninstall", "check"}:
        raise ValueError("invalid hook action")
    settings, original = _read_settings(path)
    wrapper = state_dir / f"hook-{source.value}.sh"
    merged = merge_hooks(settings, source, wrapper, install=action != "uninstall")
    if action == "check":
        try:
            load_identity_key(state_dir / "identity.key")
            identity_ready = True
        except (OSError, ValueError):
            identity_ready = False
        return {
            "source": source.value,
            "installed": settings == merged,
            "wrapper_ready": _wrapper_ready(wrapper, _observer_script(source, state_dir)),
            "identity_ready": identity_ready,
        }
    if action == "install":
        initialize_identity_key(state_dir / "identity.key")
        script = _observer_script(source, state_dir)
        _atomic_write(wrapper, script.encode(), 0o700)
    if settings == merged:
        return {"source": source.value, "changed": False}
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if original is not None:
        # A unique private backup, never copied into release packages or printed.
        fd, backup = tempfile.mkstemp(prefix=".saki-settings-backup-", dir=path.parent)
        with os.fdopen(fd, "wb") as stream:
            stream.write(original)
        os.chmod(backup, 0o600)
    # Refuse concurrent changes rather than overwriting edits made after our read.
    _, current = _read_settings(path)
    if current != original:
        raise ValueError("settings changed during installation")
    _atomic_write(path, (json.dumps(merged, ensure_ascii=False, indent=2) + "\n").encode())
    return {"source": source.value, "changed": True}
