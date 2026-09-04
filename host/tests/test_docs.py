from __future__ import annotations

import json
import re
import unittest
from pathlib import Path
from urllib.parse import unquote

REPO_ROOT = Path(__file__).resolve().parents[2]
MARKDOWN_LINK = re.compile(r"\[[^]]*]\(([^)]+)\)")


class DocumentationTests(unittest.TestCase):
    def test_repository_license_and_notice_are_present(self) -> None:
        license_text = (REPO_ROOT / "LICENSE").read_text(encoding="utf-8")
        notice_text = (REPO_ROOT / "NOTICE").read_text(encoding="utf-8")

        self.assertIn("Apache License", license_text)
        self.assertIn("Version 2.0, January 2004", license_text)
        self.assertIn("Copyright 2026 mgbaozi", notice_text)
        self.assertIn("https://github.com/mgbaozi/saki", notice_text)

    def test_project_docs_do_not_describe_a_noncommercial_scope(self) -> None:
        documents = [
            REPO_ROOT / "AGENTS.md",
            REPO_ROOT / "README.md",
            *sorted((REPO_ROOT / "docs").rglob("*.md")),
        ]
        forbidden = ("非商业", "非商用", "个人版", "non-commercial", "noncommercial")
        matches = [
            str(path.relative_to(REPO_ROOT))
            for path in documents
            if any(term in path.read_text(encoding="utf-8").lower() for term in forbidden)
        ]

        self.assertEqual(matches, [])

    def test_local_markdown_links_resolve_after_docs_reorganization(self) -> None:
        documents = [
            REPO_ROOT / "README.md",
            REPO_ROOT / "firmware" / "README.md",
            REPO_ROOT / "host" / "README.md",
            REPO_ROOT / "protocol" / "README.md",
            *sorted((REPO_ROOT / "docs").rglob("*.md")),
        ]
        missing: list[str] = []

        for document in documents:
            for target in MARKDOWN_LINK.findall(document.read_text(encoding="utf-8")):
                target = target.strip().strip("<>")
                if target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                path_text = unquote(target.split("#", 1)[0])
                if path_text and not (document.parent / path_text).resolve().exists():
                    missing.append(f"{document.relative_to(REPO_ROOT)} -> {target}")

        self.assertEqual(missing, [])

    def test_codex_hooks_use_portable_project_wrapper(self) -> None:
        config_path = REPO_ROOT / ".codex" / "hooks.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        commands = [
            hook["command"]
            for groups in config["hooks"].values()
            for group in groups
            for hook in group["hooks"]
            if hook["type"] == "command"
        ]

        self.assertTrue(commands)
        self.assertTrue((REPO_ROOT / "scripts" / "saki-hook.zsh").is_file())
        self.assertTrue(all(command.startswith("scripts/saki-hook.zsh ") for command in commands))
        self.assertTrue(all("/Users/" not in command for command in commands))

    def test_versioned_sources_do_not_embed_a_user_home(self) -> None:
        sources = [
            REPO_ROOT / "scripts" / "env-idf.zsh",
            REPO_ROOT / "firmware" / "components" / "vendor_box3_bsp" / "ORIGIN.md",
            *sorted((REPO_ROOT / "docs").rglob("*.md")),
        ]

        embedded = [
            str(path.relative_to(REPO_ROOT))
            for path in sources
            if "/Users/" in path.read_text(encoding="utf-8")
        ]
        self.assertEqual(embedded, [])


if __name__ == "__main__":
    unittest.main()
