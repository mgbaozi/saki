from __future__ import annotations

import plistlib
import tempfile
import unittest
from pathlib import Path

from saki_host.launch_agent import LAUNCH_AGENT_LABEL, launch_agent_config, render_launch_agent


class LaunchAgentTests(unittest.TestCase):
    def test_config_uses_absolute_project_and_log_paths(self) -> None:
        repo_root = Path("/tmp/saki repo").resolve()
        config = launch_agent_config(repo_root, Path("/Users/tester"))

        self.assertEqual(config["Label"], LAUNCH_AGENT_LABEL)
        self.assertEqual(
            config["ProgramArguments"],
            [str(repo_root / "host" / ".venv" / "bin" / "saki-host"), "serve"],
        )
        self.assertEqual(config["WorkingDirectory"], str(repo_root))
        self.assertEqual(
            config["StandardOutPath"],
            "/Users/tester/Library/Logs/Saki/host.stdout.log",
        )
        self.assertEqual(config["Umask"], "077")
        self.assertIs(config["KeepAlive"], True)

    def test_rendered_plist_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "host" / ".venv" / "bin" / "saki-host"
            executable.parent.mkdir(parents=True)
            executable.touch()
            executable.chmod(0o700)
            output = root / "agent.plist"

            render_launch_agent(output, root, Path("/Users/tester"))

            parsed = plistlib.loads(output.read_bytes())
            self.assertEqual(parsed["Label"], LAUNCH_AGENT_LABEL)
            self.assertEqual(parsed["ProgramArguments"][0], str(executable.resolve()))
            self.assertEqual(parsed["ProcessType"], "Interactive")


if __name__ == "__main__":
    unittest.main()
