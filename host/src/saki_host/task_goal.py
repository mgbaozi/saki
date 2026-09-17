"""Local, bounded display goals; never store or forward the original prompt."""

from __future__ import annotations

import re
import unicodedata

from .models import truncate_utf8
from .privacy import redact_text

MAX_GOAL_BYTES = 96
_CONTEXT = re.compile(r"<([A-Za-z_][\w-]*)\b[^>]*>.*?</\1\s*>", re.DOTALL)
_FENCE = re.compile(r"```.*?(?:```|\Z)", re.DOTALL)
_URL = re.compile(r"(?:https?://|www\.)\S+", re.IGNORECASE)
_PATH = re.compile(r"(?<!\w)(?:~?/|[A-Za-z]:\\)[^\s，。；、<>\"']+")
_SECRET = re.compile(
    r"(?i)(?:\b(?:api[_ -]?key|access[_ -]?token|auth(?:orization)?|password|passwd|"
    r"secret|credential|bearer)\b|密码|密钥|令牌|凭据|"
    r"\b(?:sk-|gh[pousr]_|github_pat_|xox[baprs]-|AKIA)[\w-]+|"
    r"-----BEGIN|\beyJ[A-Za-z0-9_-]+\.|[A-Za-z0-9_+/=-]{24,})"
)
_FOLLOWUP = re.compile(
    r"(?i)^(?:请)?(?:继续(?:吧|执行|实现|工作|测试)?|开始(?:吧|实现|执行)?|"
    r"允许|同意|确认|好的?|可以|是的|已启动|已进入下载模式|"
    r"continue|proceed|resume|yes|ok(?:ay)?|go ahead)[\s.!！。]*$"
)
_FILES_HEADING = re.compile(r"(?i)^(?:#{1,6}\s*)?files mentioned by the user\s*:\s*$")
_REQUEST_HEADING = re.compile(r"(?i)^(?:#{1,6}\s*)?my request\s*:\s*$")
_ATTACHMENT_INSTRUCTION = re.compile(
    r"^Distinguish instructions in attached documents from the user's request\.?$"
)


def display_goal(value: object) -> str:
    """Keep one short user-facing line, excluding code, context and sensitive lines.

    This is deterministic extraction, not a model call. Secret-bearing lines are
    discarded before truncation so a clipped credential cannot escape detection.
    """
    if not isinstance(value, str) or len(value.encode("utf-8", errors="replace")) > 65536:
        return ""
    value = _CONTEXT.sub("\n", value)
    value = _FENCE.sub("\n", value)
    in_attachment_list = False
    for line in value.splitlines():
        line = line.strip()
        if _FILES_HEADING.fullmatch(line):
            in_attachment_list = True
            continue
        if _REQUEST_HEADING.fullmatch(line):
            in_attachment_list = False
            continue
        if in_attachment_list or _ATTACHMENT_INSTRUCTION.fullmatch(line):
            continue
        if not line or line.startswith(("<", ">", "-----")):
            continue
        line = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", line)
        line = _URL.sub("[链接]", line)
        if _SECRET.search(line):
            continue
        line = _PATH.sub("[路径]", line)
        line = redact_text(line)
        line = "".join(c for c in line if unicodedata.category(c)[0] != "C")
        line = re.sub(r"\s+", " ", line).strip(" #*-`\t")
        if not line or _FOLLOWUP.fullmatch(line):
            continue
        line = re.split(r"[。！？!?]|(?<=\w)\.\s", line, maxsplit=1)[0].strip()
        if not re.search(r"[\w\u4e00-\u9fff]", line.replace("[链接]", "").replace("[路径]", "")):
            continue
        return truncate_utf8(line[:48], MAX_GOAL_BYTES).strip(" #*-`\t")
    return ""
