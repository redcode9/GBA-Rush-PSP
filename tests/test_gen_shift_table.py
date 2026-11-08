"""Tests of tests/core-rom/gen_shift_table.py on values the ARM
Architecture Reference Manual gives for shifts by a register."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "core-rom"))

import gen_shift_table  # noqa: E402

shift = gen_shift_table.shift


class GenShiftTableTest(unittest.TestCase):
    def test_bits_above_the_bottom_byte_are_ignored(self):
        for kind in gen_shift_table.KINDS:
            for carry in (0, 1):
                self.assertEqual(shift(kind, 0x87654321, 0x100, carry),
                                 (0x87654321, carry))

    def test_lsl_by_32_and_more(self):
        self.assertEqual(shift("lsl", 0x80000001, 32, 0), (0, 1))
        self.assertEqual(shift("lsl", 0x80000000, 32, 1), (0, 0))
        self.assertEqual(shift("lsl", 0xFFFFFFFF, 33, 1), (0, 0))

    def test_lsr_by_32_and_more(self):
        self.assertEqual(shift("lsr", 0x80000000, 32, 0), (0, 1))
        self.assertEqual(shift("lsr", 0xFFFFFFFF, 33, 1), (0, 0))

    def test_asr_by_32_and_more(self):
        self.assertEqual(shift("asr", 0x80000000, 33, 0), (0xFFFFFFFF, 1))
        self.assertEqual(shift("asr", 0x7FFFFFFF, 255, 1), (0, 0))

    def test_ror_by_multiples_of_32(self):
        self.assertEqual(shift("ror", 0x87654321, 32, 0), (0x87654321, 1))
        self.assertEqual(shift("ror", 0x12345678, 64, 1), (0x12345678, 0))
        self.assertEqual(shift("ror", 0x00000003, 33, 0), (0x80000001, 1))

    def test_small_shifts(self):
        self.assertEqual(shift("lsl", 0x40000001, 2, 0), (0x00000004, 1))
        self.assertEqual(shift("lsr", 0x00000003, 1, 0), (0x00000001, 1))
        self.assertEqual(shift("asr", 0x80000000, 31, 0), (0xFFFFFFFF, 0))

    def test_table_covers_more_than_ten_thousand_cases(self):
        rm, rs = len(gen_shift_table.RM), len(gen_shift_table.RS)
        # cpu_shift_reg: five forms with every Rm, two (Rm = Rs, Rm = PC)
        # with one value per Rs; four kinds and two carries each.
        self.assertGreater((5 * rm * rs + 2 * rs) * 4 * 2, 10000)
        self.assertEqual(len(set(gen_shift_table.RS)), rs)

    def test_header_is_c(self):
        text = gen_shift_table.table()
        self.assertIn("static const u32 shift_result[4][8][32]", text)
        self.assertEqual(text.count("{"), text.count("}"))


if __name__ == "__main__":
    unittest.main()
