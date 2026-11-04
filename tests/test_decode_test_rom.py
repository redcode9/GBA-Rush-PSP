"""Tests of tools/decode_test_rom.py on synthetic signatures, laid out as
tests/gba-rom/result.h describes and placed in save images of the native
sizes, the way the test ROMs leave them."""

import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import decode_test_rom  # noqa: E402

CORE_CASES = ["bios_openbus_boot", "bios_openbus_swi", "bios_openbus_irq",
              "halt_ime0", "halt_cpsr_i", "ewram_stack", "pc_high"]


def signature(rom, cases, running=0xFFFF, boots=1):
    """cases: (name, status, detail) as result.c writes them."""
    count = len(cases)
    statuses = bytes(c[1] for c in cases).ljust((count + 3) // 4 * 4, b"\0")
    details = b"".join(struct.pack("<I", c[2]) for c in cases)
    names = b"".join(c[0].encode() + b"\0" for c in cases)
    size = 48 + len(statuses) + len(details) + len(names)
    header = b"GRTS" + struct.pack("<BxHHHII", 1, count, running, boots, 0,
                                   size)
    data = header + rom.encode().ljust(28, b"\0") + statuses + details + names
    crc = zlib.crc32(data)
    return data[:12] + struct.pack("<I", crc) + data[16:]


def image(sig, size, fill=0xFF):
    return sig + bytes([fill]) * (size - len(sig))


class DecodeTestRomTest(unittest.TestCase):
    def decode(self, data, *options):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "rom.sav")
            with open(path, "wb") as f:
                f.write(data)
            out = io.StringIO()
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(out):
                status = decode_test_rom.main(list(options) + [path])
        return status, out.getvalue()

    def test_core_rom_in_sram_prints_case_names(self):
        cases = [(name, 1, 0) for name in CORE_CASES]
        cases[0] = ("bios_openbus_boot", 1, 0xE129F000)
        status, out = self.decode(image(signature("core-rom", cases),
                                        0x8000))
        self.assertEqual(status, 0)
        self.assertIn("core-rom: 7 cases, boot 1", out)
        for name in CORE_CASES:
            self.assertRegex(out, rf"{name} +PASS")
        self.assertIn("E129F000", out)

    def test_flash_and_eeprom_images(self):
        cases = [("flash_id", 1, 0x1362), ("read_back", 3, 0),
                 ("write_verify", 1, 1)]
        for size in (0x10000, 0x20000, 0x200, 0x2000):
            status, out = self.decode(image(signature("save", cases), size))
            self.assertEqual(status, 0, out)
            self.assertRegex(out, r"read_back +SKIP")

    def test_stopped_in_a_case(self):
        cases = [(name, 1, 0) for name in CORE_CASES[:3]] + \
            [(name, 0, 0) for name in CORE_CASES[3:]]
        status, out = self.decode(
            image(signature("core-rom", cases, running=3), 0x8000))
        self.assertEqual(status, 1)
        self.assertIn("stopped while running halt_ime0", out)
        self.assertRegex(out, r"halt_ime0 +RUNNING")

    def test_hung_and_failed_cases(self):
        cases = [("halt_ime0", 4, 0), ("pc_high", 2, 1)]
        status, out = self.decode(image(signature("core-rom", cases,
                                                  boots=2), 0x8000))
        self.assertEqual(status, 1)
        self.assertRegex(out, r"halt_ime0 +HUNG")
        self.assertRegex(out, r"pc_high +FAIL +00000001")
        self.assertIn("boot 2", out)

    def test_allowed_failure(self):
        cases = [("bios_openbus_swi", 2, 0x1524), ("halt_ime0", 1, 1)]
        data = image(signature("core-rom", cases), 0x8000)
        self.assertEqual(self.decode(data)[0], 1)
        self.assertEqual(self.decode(data, "--allow-fail",
                                     "bios_openbus_swi")[0], 0)

    def test_bad_crc(self):
        data = bytearray(image(signature("core-rom", [("pc_high", 1, 3)]),
                               0x8000))
        data[48] = 2
        status, out = self.decode(bytes(data))
        self.assertEqual(status, 2)
        self.assertIn("CRC", out)

    def test_blank_save(self):
        status, out = self.decode(b"\xff" * 0x8000)
        self.assertEqual(status, 2)
        self.assertIn("no test ROM signature", out)


if __name__ == "__main__":
    unittest.main()
