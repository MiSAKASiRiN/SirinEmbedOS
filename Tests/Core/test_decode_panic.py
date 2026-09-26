#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Tools" / "Core"))

import decode_panic  # noqa: E402


SAMPLE = """
[SRNOS::Fault][CAUSE] UsageFault: UNALIGNED
[SRNOS::Fault][CORE] R0=0x2400F1E0 R1=0x2400F1DF R2=0x00000003 R3=0x00000001
[SRNOS::Fault][CORE] R12=0x2400F1E0 LR=0x2400F1E3 PC=0x0801A744 xPSR=0x61000000
[SRNOS::Fault][FAULT] CFSR=0x01000000 HFSR=0x00000000
[SRNOS::Fault][STACK+00] 0x2400F1E0 0x0801A8E3 0xDEADBEEF 0x08001001
"""


class DecodePanicTest(unittest.TestCase):
    def test_parse_addresses(self):
        values = decode_panic.parse_addresses(SAMPLE)
        self.assertEqual(values["PC"], 0x0801A744)
        self.assertEqual(values["LR"], 0x2400F1E3)
        self.assertEqual(values["CFSR"], 0x01000000)

    def test_decode_unaligned(self):
        self.assertEqual(decode_panic.decode_cfsr(0x01000000),
                         ["UsageFault: UNALIGNED"])

    def test_stack_code_candidates(self):
        self.assertEqual(decode_panic.stack_code_candidates(SAMPLE),
                         [0x0801A8E2, 0x08001000])

    def test_retained_boot_stack_candidates(self):
        boot = ("[BOOT][PANIC][STACK+00] 0x08002001 0x24000000\n"
                "[BOOT][PANIC][STACK+04] 0x08003000\n")
        self.assertEqual(decode_panic.stack_code_candidates(boot),
                         [0x08002000, 0x08003000])

    def test_itcm_candidate_with_elf_ranges(self):
        log = "[SRNOS::Fault][STACK+00] 0x00000001 0x08002001\n"
        self.assertEqual(
            decode_panic.stack_code_candidates(
                log, [(0x00000000, 0x00000140),
                      (0x08000300, 0x08050000)]),
            [0x00000000, 0x08002000])


if __name__ == "__main__":
    unittest.main()
