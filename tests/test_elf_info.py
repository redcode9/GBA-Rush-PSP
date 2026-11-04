"""Tests for scripts/elf_info.py on ELF files built in the test."""

import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1] / "scripts"))
import elf_info  # noqa: E402


PT_NOTE = 4


def elf32(segments, ident=b"\x7fELF\x01\x01\x01"):
    """An ELF32 header and one program header per (type, vaddr, memsz)."""
    data = ident.ljust(16, b"\0") + struct.pack(
     "<HHIIIIIHHHHHH", 2, 8, 1, 0, 52, 0, 0, 52, 32, len(segments), 0, 0, 0)
    for p_type, vaddr, memsz in segments:
        data += struct.pack("<8I", p_type, 0, vaddr, vaddr, memsz, memsz, 0, 4)
    return data


class LoadImageSizeTest(unittest.TestCase):
    def size_of(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "test.elf"
            path.write_bytes(data)
            return elf_info.load_image_size(path)

    def test_spans_lowest_start_to_highest_end_of_load_segments(self):
        # The note segment lies outside the loaded image and is ignored.
        data = elf32([(elf_info.PT_LOAD, 0x8000, 0x200),
                      (PT_NOTE, 0x0, 0x100000),
                      (elf_info.PT_LOAD, 0x1000, 0x500)])
        self.assertEqual(self.size_of(data), 0x8200 - 0x1000)

    def test_rejects_a_file_that_is_not_elf(self):
        with self.assertRaisesRegex(ValueError, "not an ELF"):
            self.size_of(b"PK\x03\x04" + bytes(60))

    def test_rejects_a_64_bit_elf(self):
        data = elf32([(elf_info.PT_LOAD, 0, 16)], ident=b"\x7fELF\x02\x01\x01")
        with self.assertRaisesRegex(ValueError, "ELF32"):
            self.size_of(data)

    def test_rejects_an_elf_without_load_segments(self):
        with self.assertRaisesRegex(ValueError, "no PT_LOAD"):
            self.size_of(elf32([(PT_NOTE, 0, 16)]))

    def test_rejects_a_truncated_program_header_table(self):
        data = elf32([(elf_info.PT_LOAD, 0, 16)])
        with self.assertRaisesRegex(ValueError, "truncated"):
            self.size_of(data[:60])


if __name__ == "__main__":
    unittest.main()
