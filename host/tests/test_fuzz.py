from __future__ import annotations

import unittest

from saki_host.fuzz import build_fuzz_cases
from saki_host.protocol import MAX_FRAME_BYTES


class FuzzCorpusTests(unittest.TestCase):
    def test_corpus_is_deterministic_and_loads_every_invalid_fixture(self) -> None:
        first = build_fuzz_cases(random_case_count=8, seed=1234)
        second = build_fuzz_cases(random_case_count=8, seed=1234)

        self.assertEqual(first, second)
        self.assertEqual(len(first), 16)
        self.assertEqual(
            [case.name for case in first if case.name.startswith("fixture:")],
            ["fixture:status-bad-progress.json", "fixture:status-missing-state.json"],
        )
        self.assertEqual(len({case.name for case in first}), len(first))

    def test_corpus_covers_chunking_utf8_and_oversize_boundaries(self) -> None:
        cases = {case.name: case for case in build_fuzz_cases(0, seed=1)}

        self.assertGreater(len(cases["chunked-truncation"].chunks), 1)
        self.assertIn(b"\xc0\xaf", cases["invalid-utf8"].chunks[0])
        self.assertGreater(cases["oversized-frame"].byte_count - 1, MAX_FRAME_BYTES)


if __name__ == "__main__":
    unittest.main()
