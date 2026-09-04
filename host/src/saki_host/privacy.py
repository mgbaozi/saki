from __future__ import annotations

import re
from pathlib import Path

_AUTHORIZATION = re.compile(r"(?i)(authorization\s*[:=]\s*)(bearer\s+)?[^\s,;]+")
_SECRET_ASSIGNMENT = re.compile(
    r"(?i)\b(api[_-]?key|access[_-]?token|password|secret)\b(\s*[:=]\s*)[^\s,;]+"
)
_URL_QUERY = re.compile(r"(https?://[^\s?#]+)\?[^\s#]*")
_DISPLAY_SPACE_ENTITY = re.compile(
    r"(?i)&(?:nbsp|#0*(?:9|10|13|20|32|160)|#x0*(?:9|a|d|20|a0));"
)


def normalize_display_text(value: str) -> str:
    """Restore whitespace entities emitted by UI/hook serialization.

    ``&#20;`` is not a standards-compliant space reference, but is accepted for
    compatibility with observed hook text. Other HTML entities stay literal.
    """
    return _DISPLAY_SPACE_ENTITY.sub(" ", value)


def redact_text(value: str, home: Path | None = None) -> str:
    """Remove common credentials and shorten the user's home path."""
    result = normalize_display_text(value)
    result = _AUTHORIZATION.sub(r"\1[REDACTED]", result)
    result = _SECRET_ASSIGNMENT.sub(r"\1\2[REDACTED]", result)
    result = _URL_QUERY.sub(r"\1?[REDACTED]", result)
    home_path = str(home or Path.home())
    if home_path:
        result = result.replace(home_path, "~")
    return result
