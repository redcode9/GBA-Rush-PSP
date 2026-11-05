"""Tests for scripts/check-elf.py on ELF files built in the test."""

import contextlib
import importlib.util
import io
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = pathlib.Path(__file__).parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import elf_info  # noqa: E402

# The script name has a dash, so load it as a module by path.
SPEC = importlib.util.spec_from_file_location("check_elf", SCRIPTS / "check-elf.py")
check_elf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_elf)

BUILD_ID = "0123abcd4567"
IMAGE_SIZE = 0x200000


def elf32(payload, image_size=IMAGE_SIZE):
    """An ELF32 file with one PT_LOAD of image_size bytes, then payload."""
    return (b"\x7fELF\x01\x01\x01".ljust(16, b"\0")
            + struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0, 52, 0, 0, 52, 32, 1,
                          0, 0, 0)
            + struct.pack("<8I", elf_info.PT_LOAD, 0, 0x8804000, 0x8804000,
                          image_size, image_size, 0, 4)
            + payload)


def clean_payload():
    """The strings a release ELF must hold, and no forbidden one."""
    return (b"\0GBA Rush PSP " + check_elf.source_version().encode()
            + b"\0" + BUILD_ID.encode() + b"\0tempgba.cfg\0")


class CheckElfTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = pathlib.Path(directory.name)
        self.elf = self.root / "TempGBA.elf"
        # Without docs/RAM-BUDGET.md and scripts/check-layout.py by default,
        # whatever the tree holds.
        for patch in (mock.patch.object(elf_info, "ram_budget", return_value=None),
                      mock.patch.object(check_elf, "CHECK_LAYOUT",
                                        self.root / "check-layout.py")):
            patch.start()
            self.addCleanup(patch.stop)

    def run_check(self, data, *options):
        """main()'s exit status and what it printed on stdout and stderr."""
        self.elf.write_bytes(data)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = check_elf.main(["--build-id", BUILD_ID, *options,
                                     str(self.elf)])
        return status, out.getvalue(), err.getvalue()

    def test_a_clean_elf_passes_and_reports_its_image(self):
        status, out, err = self.run_check(elf32(clean_payload()))
        self.assertEqual((status, err), (0, ""))
        self.assertIn(f"load image {IMAGE_SIZE} bytes", out)
        self.assertIn("not measured", out)

    def test_each_forbidden_string_fails(self):
        for text in (b"diag-autorun.txt", b"traces/input-AXVE-r1.gbit",
                     b"GBIT", b"GBA Rush PSP [DIAG]"):
            with self.subTest(text=text):
                status, _, err = self.run_check(
                 elf32(clean_payload() + text + b"\0"))
                self.assertEqual(status, 1)
                self.assertIn("forbidden string", err)

    def test_a_missing_version_or_build_id_fails(self):
        version = check_elf.source_version().encode()
        for missing in (version, BUILD_ID.encode()):
            with self.subTest(missing=missing):
                payload = clean_payload().replace(missing, b"x")
                status, _, err = self.run_check(elf32(payload))
                self.assertEqual(status, 1)
                self.assertIn(f"{missing.decode()!r} not found", err)

    def test_an_image_that_leaves_too_little_heap_fails(self):
        # 24 MiB for image and heap with at least 20 MiB of heap: the image
        # may take up to 4 MiB.
        budget = (24 << 20, 20 << 20)
        with mock.patch.object(elf_info, "ram_budget", return_value=budget):
            status, out, _ = self.run_check(elf32(clean_payload(), 4 << 20))
            self.assertEqual(status, 0)
            self.assertIn(f"startup heap {20 << 20} bytes", out)
            status, _, err = self.run_check(elf32(clean_payload(), (4 << 20) + 1))
        self.assertEqual(status, 1)
        self.assertIn("below", err)

    def test_a_failing_check_layout_warns_or_fails_with_strict(self):
        (self.root / "check-layout.py").write_text("import sys\nsys.exit(1)\n",
                                                   encoding="utf-8")
        status, _, err = self.run_check(elf32(clean_payload()))
        self.assertEqual(status, 0)
        self.assertIn("warning: check-layout.py failed", err)
        status, _, err = self.run_check(elf32(clean_payload()), "--strict")
        self.assertEqual(status, 1)
        self.assertIn("check-layout.py failed", err)

    def test_reads_the_version_from_main_h(self):
        header = self.root / "main.h"
        header.write_text('#ifndef MAIN_H\n#define GBA_RUSH_VERSION "9.8.7"\n',
                          encoding="utf-8")
        self.assertEqual(check_elf.source_version(header), "9.8.7")
        header.write_text("#define OTHER 1\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "no GBA_RUSH_VERSION"):
            check_elf.source_version(header)


if __name__ == "__main__":
    unittest.main()
