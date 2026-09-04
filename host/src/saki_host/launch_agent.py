from __future__ import annotations

import argparse
import os
import plistlib
from pathlib import Path

LAUNCH_AGENT_LABEL = "com.saki.agent-display"


def launch_agent_config(repo_root: Path, user_home: Path) -> dict[str, object]:
    repo_root = repo_root.resolve()
    user_home = user_home.resolve()
    host_executable = repo_root / "host" / ".venv" / "bin" / "saki-host"
    log_directory = user_home / "Library" / "Logs" / "Saki"
    return {
        "Label": LAUNCH_AGENT_LABEL,
        "ProgramArguments": [str(host_executable), "serve"],
        "WorkingDirectory": str(repo_root),
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 5,
        "ProcessType": "Interactive",
        "Umask": "077",
        "EnvironmentVariables": {"PYTHONUNBUFFERED": "1"},
        "StandardOutPath": str(log_directory / "host.stdout.log"),
        "StandardErrorPath": str(log_directory / "host.stderr.log"),
    }


def render_launch_agent(output: Path, repo_root: Path, user_home: Path) -> None:
    host_executable = repo_root.resolve() / "host" / ".venv" / "bin" / "saki-host"
    if not host_executable.is_file() or not os.access(host_executable, os.X_OK):
        raise FileNotFoundError(f"Host executable is missing or not executable: {host_executable}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(
        plistlib.dumps(
            launch_agent_config(repo_root, user_home),
            fmt=plistlib.FMT_XML,
            sort_keys=False,
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="render the Saki macOS LaunchAgent plist")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--user-home", required=True, type=Path)
    args = parser.parse_args()
    try:
        render_launch_agent(args.output, args.repo_root, args.user_home)
    except (FileNotFoundError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
