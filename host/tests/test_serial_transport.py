import unittest

from saki_host.transports.serial import SAKI_PRODUCT, SerialCandidate


class SerialCandidateTests(unittest.TestCase):
    def test_callout_device_is_preferred(self) -> None:
        candidate = SerialCandidate("/dev/cu.usbmodem1", "", None, None, None, None)
        self.assertTrue(candidate.preferred_callout)

    def test_product_identifies_candidate_hint(self) -> None:
        candidate = SerialCandidate(
            "/dev/cu.usbmodem1",
            SAKI_PRODUCT,
            SAKI_PRODUCT,
            "device-1",
            0x303A,
            0x4001,
        )
        self.assertTrue(candidate.looks_like_saki)


if __name__ == "__main__":
    unittest.main()
