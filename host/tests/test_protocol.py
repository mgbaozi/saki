import unittest

from saki_host.models import AgentState, StateSnapshot
from saki_host.protocol import (
    MAX_FRAME_BYTES,
    ProtocolCodec,
    ProtocolError,
    decode_frame,
    encode_frame,
)


class ProtocolTests(unittest.TestCase):
    def test_encode_decode_round_trip(self) -> None:
        message = ProtocolCodec("00000000-0000-4000-8000-000000000001").status(
            StateSnapshot(state=AgentState.IDLE)
        )
        frame = encode_frame(message)
        self.assertTrue(frame.endswith(b"\n"))
        self.assertEqual(decode_frame(frame), message)

    def test_decode_accepts_crlf(self) -> None:
        self.assertEqual(decode_frame(b'{"v":1,"type":"pong"}\r\n')["type"], "pong")

    def test_rejects_wrong_version(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "unsupported_version"):
            decode_frame(b'{"v":2,"type":"hello"}\n')

    def test_rejects_oversized_frame(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "frame_too_large"):
            decode_frame(b"{" + b"x" * MAX_FRAME_BYTES + b"}\n")


if __name__ == "__main__":
    unittest.main()
