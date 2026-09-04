from __future__ import annotations

import random
from dataclasses import dataclass
from pathlib import Path

from .protocol import MAX_FRAME_BYTES

INVALID_FIXTURE_ROOT = (
    Path(__file__).resolve().parents[3] / "protocol" / "fixtures" / "v1" / "invalid"
)


@dataclass(frozen=True, slots=True)
class FuzzCase:
    name: str
    chunks: tuple[bytes, ...]

    @property
    def byte_count(self) -> int:
        return sum(len(chunk) for chunk in self.chunks)


def _terminated(payload: bytes) -> bytes:
    return payload if payload.endswith(b"\n") else payload + b"\n"


def _load_invalid_fixtures(root: Path) -> list[FuzzCase]:
    cases: list[FuzzCase] = []
    for path in sorted(root.glob("*.json")):
        cases.append(FuzzCase(f"fixture:{path.name}", (_terminated(path.read_bytes()),)))
    if not cases:
        raise ValueError(f"no invalid protocol fixtures found in {root}")
    return cases


def build_fuzz_cases(
    random_case_count: int,
    seed: int,
    fixture_root: Path = INVALID_FIXTURE_ROOT,
) -> list[FuzzCase]:
    """Build a deterministic, bounded corpus for the device NDJSON parser."""
    if random_case_count < 0:
        raise ValueError("random_case_count cannot be negative")

    fixture_cases = _load_invalid_fixtures(fixture_root)
    fixture_frames = [case.chunks[0] for case in fixture_cases]
    cases = [
        *fixture_cases,
        FuzzCase("malformed-json", (b"{not-json}\n",)),
        FuzzCase(
            "invalid-utf8",
            (b'{"v":1,"type":"status","id":200,"state":"\xc0\xaf"}\n',),
        ),
        FuzzCase(
            "chunked-truncation",
            (b'{"v":1,"type":"status",', b'"id":201,"state":"working"\n'),
        ),
        FuzzCase("concatenated-frames", (b"not-json\n[]\n",)),
        FuzzCase("repeated-frame", (b'{"v":1}\n' * 3,)),
        FuzzCase("oversized-frame", (b"x" * (MAX_FRAME_BYTES + 64) + b"\n",)),
    ]

    generator = random.Random(seed)
    for index in range(random_case_count):
        mutation = index % 4
        first = generator.choice(fixture_frames)
        if mutation == 0:
            cut = generator.randrange(1, max(2, len(first)))
            chunks = (_terminated(first[:cut].rstrip(b"\n")),)
            kind = "truncate"
        elif mutation == 1:
            start = generator.randrange(0, len(first))
            width = generator.randrange(1, min(64, len(first) - start) + 1)
            fragment = first[start : start + width]
            chunks = (_terminated(fragment * generator.randrange(2, 5)),)
            kind = "repeat"
        elif mutation == 2:
            second = generator.choice(fixture_frames)
            chunks = (first, second)
            kind = "concatenate"
        else:
            width = generator.randrange(1, 96)
            payload = bytes(generator.randrange(0, 256) for _ in range(width))
            chunks = (_terminated(payload.replace(b"\n", b"_")),)
            kind = "random-bytes"
        cases.append(FuzzCase(f"random:{index + 1}:{kind}", chunks))

    return cases
