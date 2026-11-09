"""Run scripts/check-package.sh on test archives in a stub release tree."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/check-package.sh"
VERSION = "9.9.9"
INSTALL = "PSP/GAME/GBARushPSP/"
SOURCE = "GBA-Rush-PSP-%s-source" % VERSION
BINARIES = ("EBOOT.PBP", "exception.prx", "media_engine.prx")


def documentation_files():
    text = SCRIPT.read_text()
    return re.search(r"documentation_files='([^']*)'", text).group(1).split()


def have(command):
    """True when the shell that runs the script finds command."""
    return shutil.which("sh") is not None and subprocess.run(
        ["sh", "-c", "command -v " + command], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL).returncode == 0


@unittest.skipUnless(have("unzip") and have("git")
                     and (have("sha256sum") or have("shasum")),
                     "needs sh, unzip, git and sha256sum or shasum")
class CheckPackageTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.tree = Path(temporary.name)
        # The scripts' own line endings, whatever the checkout's.
        (self.tree / "scripts").mkdir()
        for script in ("check-package.sh", "release_manifest.py",
                       "elf_info.py"):
            (self.tree / "scripts" / script).write_bytes(
                (SCRIPT.parent / script).read_bytes().replace(b"\r\n", b"\n"))
        self.files = {}
        for name in BINARIES:
            self.files[INSTALL + name] = ("source/" + name, name.encode())
        for document in documentation_files():
            self.files[INSTALL + "docs/" + document] = (
                document, ("doc " + document).encode())
        for path, content in self.files.values():
            (self.tree / path).parent.mkdir(parents=True, exist_ok=True)
            (self.tree / path).write_bytes(content)
        for path in ("source/Makefile", "source/bios/Makefile"):
            (self.tree / path).write_bytes(b"all:\n")
        # The release tag, and its source zip as package-release.sh makes it.
        git = ["git", "-c", "user.name=test", "-c", "user.email=test@test",
               "-c", "core.autocrlf=false"]
        for command in (["init", "-q"], ["add", "-A"],
                        ["commit", "-q", "-m", "release"], ["tag", VERSION]):
            subprocess.run(git + command, cwd=self.tree, check=True,
                           capture_output=True)
        (self.tree / "dist").mkdir()
        subprocess.run(git + ["archive", "--format=zip", "--prefix=%s/" % SOURCE,
                              "-o", "dist/%s.zip" % SOURCE,
                              "refs/tags/" + VERSION],
                       cwd=self.tree, check=True)

    def check(self, entries):
        """Zips entries ({name: bytes}) with its checksums and build manifest
        for the tree's binaries, and runs the check."""
        dist = self.tree / "dist"
        archive = dist / ("GBA-Rush-PSP-%s.zip" % VERSION)
        with zipfile.ZipFile(archive, "w") as package:
            for name, content in entries.items():
                package.writestr(name, content)
        sha256 = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        Path(str(archive) + ".sha256").write_text(
            "%s  %s\n" % (sha256(archive), archive.name), newline="\n")
        manifest = dist / ("GBA-Rush-PSP-%s.manifest.json" % VERSION)
        manifest.write_text(json.dumps({"format": 1, "files": {
            name: {"sha256": sha256(self.tree / "source" / name)}
            for name in BINARIES}}), newline="\n")
        (dist / "SHA256SUMS").write_text("".join(
            "%s  %s\n" % (sha256(path), path.name)
            for path in (archive, manifest, dist / (SOURCE + ".zip"))),
            newline="\n")
        return subprocess.run(["sh", "scripts/check-package.sh", VERSION],
                              cwd=self.tree, capture_output=True, text=True,
                              env=dict(os.environ, PYTHON=sys.executable))

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
