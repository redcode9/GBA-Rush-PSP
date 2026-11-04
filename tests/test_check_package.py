"""Run scripts/check-package.sh on test archives in a stub release tree."""

import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/check-package.sh"
VERSION = "9.9.9"
INSTALL = "PSP/GAME/GBARushPSP/"
BINARIES = ("EBOOT.PBP", "exception.prx", "media_engine.prx")


def documentation_files():
    text = SCRIPT.read_text()
    return re.search(r"documentation_files='([^']*)'", text).group(1).split()


def have(command):
    """True when the shell that runs the script finds command."""
    return shutil.which("sh") is not None and subprocess.run(
        ["sh", "-c", "command -v " + command], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL).returncode == 0


@unittest.skipUnless(have("unzip") and have("rg")
                     and (have("sha256sum") or have("shasum")),
                     "needs sh, unzip, rg and sha256sum or shasum")
class CheckPackageTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.tree = Path(temporary.name)
        # The script's own line endings, whatever the checkout's.
        (self.tree / "scripts").mkdir()
        (self.tree / "scripts/check-package.sh").write_bytes(
            SCRIPT.read_bytes().replace(b"\r\n", b"\n"))
        self.files = {}
        for name in BINARIES:
            self.files[INSTALL + name] = ("source/" + name, name.encode())
        for document in documentation_files():
            self.files[INSTALL + "docs/" + document] = (
                document, ("doc " + document).encode())
        for path, content in self.files.values():
            (self.tree / path).parent.mkdir(parents=True, exist_ok=True)
            (self.tree / path).write_bytes(content)
        (self.tree / "dist").mkdir()

    def check(self, entries):
        """Zips entries ({name: bytes}) with its checksum and runs the check."""
        archive = self.tree / "dist" / ("GBA-Rush-PSP-%s.zip" % VERSION)
        with zipfile.ZipFile(archive, "w") as package:
            for name, content in entries.items():
                package.writestr(name, content)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        Path(str(archive) + ".sha256").write_text(
            "%s  %s\n" % (digest, archive.name), newline="\n")
        return subprocess.run(["sh", "scripts/check-package.sh", VERSION],
                              cwd=self.tree, capture_output=True, text=True)

    def release_entries(self):
        return {name: content for name, (_, content) in self.files.items()}

    def test_memory_stick_root_layout_is_accepted(self):
        result = self.check(self.release_entries())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Release package checks passed", result.stdout)

    def test_user_files_roms_and_bios_are_refused(self):
        for name in ("dir.ini", "tempgba.cfg", "Game.SAV", "game_auto.svs",
                     "roms/game.gba", "roms/Game.AGB", "game.gbz",
                     "gba_bios.bin"):
            with self.subTest(name=name):
                entries = self.release_entries()
                entries[INSTALL + name] = b"user data"
                result = self.check(entries)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("contains user files, ROMs or BIOS dumps",
                              result.stderr)
                self.assertIn(INSTALL + name, result.stderr)

    def test_old_folder_layout_is_refused(self):
        entries = {name.replace(INSTALL, "GBA-Rush-PSP-%s/GBARushPSP/"
                                % VERSION): content
                   for name, content in self.release_entries().items()}
        result = self.check(entries)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match the approved manifest", result.stderr)

    def test_changed_binary_is_refused(self):
        entries = self.release_entries()
        entries[INSTALL + "EBOOT.PBP"] = b"another build"
        result = self.check(entries)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("Release package checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
