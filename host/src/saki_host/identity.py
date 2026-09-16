"""Local pseudonyms: original source identities never enter IPC or device frames."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import stat
from pathlib import Path

DEFAULT_STATE_DIR = Path.home() / "Library" / "Application Support" / "Saki"
DEFAULT_IDENTITY_PATH = DEFAULT_STATE_DIR / "identity.key"


def load_identity_key(path: Path = DEFAULT_IDENTITY_PATH) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError("identity key permissions are invalid")
        value = os.read(fd, 33)
        if len(value) != 32:
            raise ValueError("identity key is invalid")
        return value
    finally:
        os.close(fd)


def initialize_identity_key(path: Path = DEFAULT_IDENTITY_PATH) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    except FileExistsError:
        load_identity_key(path)
        return
    try:
        os.write(fd, os.urandom(32))
        os.fsync(fd)
    finally:
        os.close(fd)


def pseudonym(key: bytes, source: str, domain: str, value: str) -> str:
    if len(key) != 32 or not isinstance(value, str) or not 1 <= len(value.encode()) <= 512:
        raise ValueError("invalid source identity")
    data = json.dumps([source, domain, value], ensure_ascii=True, separators=(",", ":")).encode()
    return hmac.new(key, data, hashlib.sha256).hexdigest()[:32]
