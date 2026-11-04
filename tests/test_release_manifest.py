"""Tests for scripts/release_manifest.py on a fake build in a temporary folder."""

import contextlib
import io
import os
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).parents[1] / "scripts"))
import release_manifest  # noqa: E402


PSPDEV = "/opt/pspdev"
PSPSDK = "/opt/pspdev/psp/sdk"
# CFLAGS as make -C source print-flags prints them after build.mak, with a
# quoted define like the build id's.
CFLAGS = (f"-ISDK/include -Isrc -I. -I{PSPDEV}/psp/include -I{PSPSDK}/include"
          ' -O2 -G0 -DZLIB_CONST -DGBA_RUSH_BUILD_ID=\\"0123abcd-dirty\\"'
          " -D_PSP_FW_VERSION=371")


def run(*argv):
    """main()'s exit status, with its error messages kept off the console."""
    with contextlib.redirect_stderr(io.StringIO()):
        return release_manifest.main(list(argv))


class ManifestTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = pathlib.Path(directory.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "EBOOT.PBP").write_bytes(b"\0PBP" + bytes(range(256)))
        (self.source / "exception.prx").write_bytes(b"exception")
        (self.source / "media_engine.prx").write_bytes(b"media engine")
        # An ELF32 header with one PT_LOAD of 0x3000 bytes at 0x100.
        (self.source / "TempGBA.elf").write_bytes(
         b"\x7fELF\x01\x01\x01".ljust(16, b"\0")
         + struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0, 52, 0, 0, 52, 32, 1, 0, 0, 0)
         + struct.pack("<8I", 1, 0, 0x100, 0x100, 0x3000, 0x3000, 0, 4))
        self.manifest = self.root / "GBA-Rush-PSP-1.4.4.manifest.json"

    def write(self, cflags=CFLAGS, version="1.4.4"):
        return run("write", "--output", str(self.manifest), "--version", version,
                   "--commit", "c5b58ea9f3d67c08696dfefdea73916991ec1088",
                   "--tree-clean", "true", "--source-date-epoch", "1767225600",
                   "--compiler", "psp-gcc (GCC) 14.2.0\nCopyright (C) 2024",
                   f"--cflags={cflags}", "--pspsdk", PSPSDK + "/",
                   "--pspdev", PSPDEV, "--source-dir", str(self.source))

    def test_writes_every_field_and_reads_back_quoted_defines(self):
        with mock.patch.dict(os.environ, {"PSPDEV_IMAGE": "pspdev/pspdev@sha256:ab"}):
            os.environ.pop("CHANNEL", None)
            self.assertEqual(self.write(), 0)
        manifest = release_manifest.read_manifest(self.manifest)
        self.assertEqual(manifest["cflags"],
                         "-ISDK/include -Isrc -I. -I$PSPDEV/psp/include"
                         " -I$PSPSDK/include -O2 -G0 -DZLIB_CONST"
                         ' -DGBA_RUSH_BUILD_ID=\\"0123abcd-dirty\\"'
                         " -D_PSP_FW_VERSION=371")
        self.assertEqual(manifest["version"], "1.4.4")
        self.assertEqual(manifest["channel"], "stable")
        self.assertIsNone(manifest["build_id"])
        self.assertIs(manifest["tree_clean"], True)
        self.assertEqual(manifest["source_date_epoch"], 1767225600)
        self.assertEqual(manifest["compiler"], "psp-gcc (GCC) 14.2.0")
        self.assertEqual(manifest["pspdev_image"], "pspdev/pspdev@sha256:ab")
        self.assertEqual(sorted(manifest["files"]), sorted(release_manifest.BUILT_FILES))
        self.assertEqual(manifest["files"]["exception.prx"]["size"], 9)
        self.assertEqual(manifest["elf_load_image_bytes"], 0x3000)
        text = self.manifest.read_text(encoding="utf-8")
        self.assertNotIn(PSPDEV, text)
        self.assertNotIn(str(self.root), text)
        self.assertNotIn(str(self.root).replace("\\", "\\\\"), text)

    def test_refuses_absolute_paths_left_in_cflags(self):
        for token in ("-I/home/builder/include", "-IC:\\pspdev\\include",
                      "-DDATA_DIR=/usr/share/gba", "/usr/lib/crt0.o"):
            with self.subTest(token=token):
                self.assertEqual(self.write(cflags=f"-O2 {token}"), 1)
                self.assertFalse(self.manifest.exists())

    def test_verify_fails_when_one_eboot_byte_differs(self):
        self.assertEqual(self.write(), 0)
        eboot = self.root / "EBOOT.PBP"
        data = bytearray((self.source / "EBOOT.PBP").read_bytes())
        eboot.write_bytes(data)
        self.assertEqual(run("verify", "--manifest", str(self.manifest), str(eboot),
                             str(self.source / "media_engine.prx")), 0)
        data[100] ^= 1
        eboot.write_bytes(data)
        self.assertEqual(run("verify", "--manifest", str(self.manifest), str(eboot)), 1)

    def test_verify_fails_on_a_file_the_manifest_does_not_list(self):
        self.assertEqual(self.write(), 0)
        other = self.root / "PARAM.SFO"
        other.write_bytes(b"exception")
        self.assertEqual(run("verify", "--manifest", str(self.manifest), str(other)), 1)


class ChannelTest(unittest.TestCase):
    def test_channel_follows_the_version(self):
        self.assertEqual(release_manifest.channel_for("1.4.4"), "stable")
        self.assertEqual(release_manifest.channel_for("1.4.4-rc.1"), "prerelease")
        self.assertEqual(release_manifest.channel_for("ci-abc"), "dev")
        self.assertEqual(release_manifest.channel_for("1.4.4-rc"), "dev")

    def test_channel_variable_overrides_the_version(self):
        self.assertEqual(release_manifest.channel_for("ci-abc", "prerelease"),
                         "prerelease")
        with self.assertRaises(ValueError):
            release_manifest.channel_for("1.4.4", "beta")


if __name__ == "__main__":
    unittest.main()
