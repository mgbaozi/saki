#!/usr/bin/env python3
"""Minimal real Claude hook probe. Never persists raw hook data or model output.

Run with host/.venv/bin/python. Only a temporary allowlisted Read fixture is
available to Claude; all normal user/project hooks and MCP servers are excluded.
The API credential stays in child-process environment, never in arguments/files.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from saki_host.adapters import SourceEvent
from saki_host.sessions import SessionRegistry


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--claude", type=Path)
    args = parser.parse_args()
    executable = args.claude or Path(
        shutil.which("claude") or Path.home() / ".local/bin/claude"
    )
    env = os.environ.copy()
    # Read only known auth/routing environment settings, without printing them.
    settings_path = Path.home() / ".claude/settings.json"
    try:
        with settings_path.open() as stream:
            settings = json.load(stream)
        configured = settings.get("env", {})
        for name in (
            "ANTHROPIC_API_KEY",
            "ANTHROPIC_AUTH_TOKEN",
            "ANTHROPIC_BASE_URL",
            "ANTHROPIC_MODEL",
            "ANTHROPIC_DEFAULT_SONNET_MODEL",
            "ANTHROPIC_DEFAULT_HAIKU_MODEL",
            "ANTHROPIC_DEFAULT_OPUS_MODEL",
        ):
            if name not in env and isinstance(configured.get(name), str):
                env[name] = configured[name]
    except (OSError, ValueError, AttributeError):
        pass
    if not any(env.get(name) for name in ("ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN")):
        print(json.dumps({"ok": False, "reason": "api_credential_not_available"}))
        return 1
    with tempfile.TemporaryDirectory(prefix="saki-claude-probe-") as directory:
        root = Path(directory)
        os.chmod(root, 0o700)
        events = root / "events.ndjson"
        hook = root / "hook.py"
        hook.write_text("""import json, os, sys, time
from pathlib import Path
from saki_host.adapters import SourceKind, normalize_hook
try:
    raw = sys.stdin.buffer.read(65537)
    if len(raw) > 65536: raise ValueError()
    event = normalize_hook(SourceKind.CLAUDE_CODE, sys.argv[1], json.loads(raw), b"p" * 32)
    if event is not None:
        fd = os.open(Path(__file__).with_name("events.ndjson"), os.O_APPEND|os.O_CREAT|os.O_WRONLY, 0o600)
        try: os.write(fd, (json.dumps(event.to_dict())+"\\n").encode())
        finally: os.close(fd)
except Exception:
    pass
""")
        sentinel = root / "probe.txt"
        sentinel.write_text("SAKI_PROBE_OK\n")
        import shlex

        hooks = {
            name: [
                {
                    "hooks": [
                        {
                            "type": "command",
                            "command": shlex.join([sys.executable, str(hook), name]),
                            "timeout": 2,
                        }
                    ]
                }
            ]
            for name in (
                "SessionStart",
                "UserPromptSubmit",
                "PreToolUse",
                "PostToolUse",
                "PostToolUseFailure",
                "PermissionRequest",
                "Stop",
                "StopFailure",
                "SessionEnd",
            )
        }
        config = root / "settings.json"
        config.write_text(json.dumps({"hooks": hooks}))
        config.chmod(0o600)
        command = [
            str(executable),
            "-p",
            "--setting-sources",
            "",
            "--settings",
            str(config),
            "--no-session-persistence",
            "--strict-mcp-config",
            "--mcp-config",
            '{"mcpServers":{}}',
            "--disable-slash-commands",
            "--no-chrome",
            "--permission-mode",
            "dontAsk",
            "--tools",
            "Read",
            "--allowedTools",
            f"Read({sentinel})",
            "--max-budget-usd",
            "0.25",
            "--effort",
            "low",
            "--system-prompt",
            "This is a local integration test. Read only the specified fixture.",
            "--output-format",
            "json",
            f"Use Read on {sentinel} exactly once, then reply only OK. Do not read any other file.",
        ]
        try:
            completed = subprocess.run(
                command,
                cwd=root,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=90,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            print(json.dumps({"ok": False, "reason": "client_failed_or_timed_out"}))
            return 1
        # Raw stdout/stderr are held in memory and deliberately never echoed/written.
        normalized = []
        if events.exists():
            normalized = [
                SourceEvent.from_dict(json.loads(line))
                for line in events.read_text().splitlines()
            ]
        registry = SessionRegistry()
        for item in normalized:
            registry.accept(item)
        kinds = sorted({item.kind.value for item in normalized})
        ok = completed.returncode == 0 and {"open", "prompt", "stop"} <= set(kinds)
        print(
            json.dumps(
                {
                    "ok": ok,
                    "exit_code": completed.returncode,
                    "event_count": len(normalized),
                    "event_kinds": kinds,
                    "session_count": len({item.session_id for item in normalized}),
                    "raw_payloads_saved": False,
                    "model_output_saved": False,
                }
            )
        )
        return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
