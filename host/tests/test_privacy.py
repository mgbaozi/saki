import unittest
from pathlib import Path

from saki_host.privacy import redact_text


class PrivacyTests(unittest.TestCase):
    def test_redacts_credentials_and_home(self) -> None:
        source = "Authorization: Bearer secret-token /Users/demo/project api_key=abc123"
        result = redact_text(source, home=Path("/Users/demo"))
        self.assertNotIn("secret-token", result)
        self.assertNotIn("abc123", result)
        self.assertIn("~/project", result)

    def test_removes_url_query(self) -> None:
        self.assertEqual(
            redact_text("https://example.test/path?token=secret", home=Path("/not-used")),
            "https://example.test/path?[REDACTED]",
        )


if __name__ == "__main__":
    unittest.main()
