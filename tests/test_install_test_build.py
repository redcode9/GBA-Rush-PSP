"""Run scripts/install-test-build.sh --dry-run on a made-up PSP card."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "install-test-build.sh"
CARD = "PSP_2004/PSP/GAME/CAT_Emulators/gpSP"
FOLDER = "PSP_2004/PSP/GAME/GBARush-142"
# A layout like the test PSPs', one line written on Windows.
DIR_INI = (b"rom_directory = GBA/ROM\r\nsave_directory = GBA/SAV\n"
           b"save_state_directory = GBA/SAV\ngame_config_directory = GBA/CFG\n")
# What a card holds besides the build: only the logs, reports and crash log
# may ever be removed.
CARD_FILES = {
    "EBOOT.PBP": b"old eboot",
    "media_engine.prx": b"old module",
    "gba_bios.bin": b"bios",
    "tempgba_crash.log": b"crash",
    "dir.ini": DIR_INI,
    "logs/session-1.gbrlog": b"log",
    "logs/session.bak": b"log backup",
    "benchmarks/perf-20261104-120000-000000.txt": b"report",
    "traces/trace-1.bin": b"trace",
    "GBA/ROM/rosso.gba": b"rom",
    "GBA/SAV/ROSSO.sav": b"save",
    "GBA/STATE/rosso_auto.svs": b"state",
}
REMOVABLE = {
    "tempgba_crash.log", "logs/session-1.gbrlog", "logs/session.bak",
    "benchmarks/perf-20261104-120000-000000.txt",
}


def tree(path):
    return {
        str(file.relative_to(path)): file.read_bytes()
        for file in sorted(path.rglob("*")) if file.is_file()
    }


class InstallTestBuildTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "dist").mkdir()
        self.zip = self.make_zip(
            "0.0.0", ("EBOOT.PBP", "exception.prx", "media_engine.prx"))
        # Like the 1.4.2 and 1.4.3 releases: no Media Engine module.
        self.release = self.make_zip("1.4.2", ("EBOOT.PBP", "exception.prx"))
        self.rom = self.root / "core rom.gba"
        self.rom.write_bytes(b"test rom")
        self.volumes = self.root / "Volumes"
        for name, data in CARD_FILES.items():
            path = self.volumes / CARD / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def tearDown(self):
        self.temporary.cleanup()

    def make_zip(self, version, names, folder="GBARushPSP"):
        path = self.root / "dist" / f"GBA-Rush-PSP-{version}.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name in names:
                archive.writestr(f"GBA-Rush-PSP-{version}/{folder}/{name}",
                                 b"new " + name.encode())
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        Path(f"{path}.sha256").write_text(f"{digest}  {path.name}\n")
        return path

    def run_script(self, *arguments, script=SCRIPT, zip_path=None, env=None):
        return subprocess.run(
            ["sh", str(script), "--dry-run", *arguments,
             str(zip_path or self.zip)],
            env={**os.environ, "PSP_VOLUMES": str(self.volumes), **(env or {})},
            capture_output=True, text=True, check=False)

    def actions(self, result):
        return [line[len("  would run: "):] for line in result.stdout.splitlines()
                if line.startswith("  would run: ")]

    def run_in_folder(self, *arguments, zip_path=None):
        before = tree(self.volumes)
        result = self.run_script("--folder", "GBARush-142", *arguments,
                                 zip_path=zip_path or self.release)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(tree(self.volumes), before)
        folder = f"{self.volumes}/{FOLDER}"
        for action in self.actions(result):
            # Whatever changes on the card is in the new folder.
            if action.startswith(("cp ", "mkdir ", "rm ")):
                target = action.rsplit(" ", 1)[1]
                if target.startswith(str(self.volumes)):
                    self.assertTrue(f"{target}/".startswith(f"{folder}/"),
                                    action)
        return result, folder

    def test_dry_run_changes_nothing_and_spares_saves_and_roms(self):
        before = tree(self.volumes)
        result = self.run_script("--no-media-engine")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(tree(self.volumes), before)
        self.assertIn("PSP_1004: not mounted, skipped", result.stdout)
        removed = {
            line.split(f"{CARD}/", 1)[1]
            for line in result.stdout.splitlines()
            if line.startswith("  would run: rm -f ")
        }
        self.assertEqual(removed, REMOVABLE | {"media_engine.prx"})

    def test_a_diagnostic_zip_goes_into_the_test_build_folder(self):
        diagnostic = self.make_zip(
            "0.0.0-diag", ("EBOOT.PBP", "exception.prx", "media_engine.prx"),
            folder="GBARushPSP-DIAG")
        result = self.run_script(zip_path=diagnostic)
        self.assertEqual(result.returncode, 0, result.stderr)
        copies = [action for action in self.actions(result)
                  if action.startswith("cp -X ")]
        self.assertEqual(len(copies), 3, copies)
        for action in copies:
            source, target = action[len("cp -X "):].split(" ", 1)
            self.assertIn("/GBARushPSP-DIAG/", source)
            self.assertTrue(target.startswith(f"{self.volumes}/{CARD}/"),
                            target)

    def test_a_zip_without_a_build_folder_stops(self):
        other = self.make_zip("0.0.0-other", ("EBOOT.PBP",), folder="Other")
        result = self.run_script(zip_path=other)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no GBARushPSP/ or GBARushPSP-DIAG/", result.stderr)

    def test_a_worktree_archives_into_the_main_checkout(self):
        # Removing a worktree deletes its ignored artifacts/ folder, which
        # would hold the only copy of the logs taken off the card.
        main = self.root / "main"
        worktree = self.root / "worktree"
        git = ["git", "-c", "user.name=Test", "-c", "user.email=test@example.com",
               "-c", "commit.gpgsign=false"]
        subprocess.run(git + ["init", "-q", str(main)], check=True)
        subprocess.run(git + ["-C", str(main), "commit", "-q", "--allow-empty",
                              "-m", "Start"], check=True)
        subprocess.run(git + ["-C", str(main), "worktree", "add", "-q",
                              "--detach", str(worktree)], check=True)
        script = worktree / "scripts" / SCRIPT.name
        script.parent.mkdir()
        shutil.copy(SCRIPT, script)
        result = self.run_script(script=script)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"would run: mkdir -p {main.resolve()}/artifacts/hardware/",
                      result.stdout)

    def test_a_release_goes_into_a_new_folder_with_bios_layout_and_rom(self):
        result, folder = self.run_in_folder("--rom", str(self.rom))
        actions = self.actions(result)
        default = f"{self.volumes}/{CARD}"
        for name in ("gba_bios.bin", "dir.ini"):
            self.assertIn(f"cp -X {default}/{name} {folder}/{name}", actions)
        for directory in ("GBA/ROM", "GBA/SAV", "GBA/CFG"):
            self.assertIn(f"mkdir -p {folder}/{directory}", actions)
        self.assertTrue(any(action.endswith(f"/rom/{self.rom.name} {folder}/GBA/ROM/")
                            for action in actions))
        installed = {action.rsplit("/", 1)[1] for action in actions
                     if action.startswith("cp -X ") and "/zip/" in action}
        self.assertEqual(installed, {"EBOOT.PBP", "exception.prx"})
        self.assertFalse([a for a in actions if a.startswith("rm ")])
        self.assertTrue(re.search(
            r"mkdir -p \S+/artifacts/hardware/\d+-\d+/PSP_2004/GBARush-142/",
            result.stdout))
        # A test build's zip brings its Media Engine module along.
        result, folder = self.run_in_folder(zip_path=self.zip)
        self.assertTrue(any(action.endswith(f" {folder}/media_engine.prx")
                            for action in self.actions(result)))

    def test_a_folder_with_an_older_build_loses_only_its_own_files(self):
        for name in ("EBOOT.PBP", "media_engine.prx", "gba_bios.bin",
                     "logs/session-1.gbrlog", "GBA/SAV/CORE-ROM.sav"):
            path = self.volumes / FOLDER / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"older")
        result, folder = self.run_in_folder()
        actions = self.actions(result)
        removed = {action.split(f"{folder}/", 1)[1] for action in actions
                   if action.startswith("rm -f ")}
        self.assertEqual(removed, {"media_engine.prx", "logs/session-1.gbrlog"})
        self.assertIn(f"cp -X {self.volumes}/{CARD}/dir.ini {folder}/dir.ini",
                      actions)
        self.assertFalse([a for a in actions if a.endswith("/gba_bios.bin")])
        self.assertTrue([a for a in actions if a.startswith(
            f"cp -R {folder}/GBA/SAV/. ")])

    def test_a_bad_folder_or_rom_or_a_full_card_stops(self):
        for arguments in (("--folder", "gpsp"), ("--folder", "a/b"),
                          ("--folder",), ("--rom", "missing.gba")):
            result = self.run_script(*arguments, zip_path=self.release)
            self.assertNotEqual(result.returncode, 0, arguments)
            self.assertNotIn("would run", result.stdout)
        result = self.run_script("--folder", "GBARush-142", zip_path=self.release,
                                 env={"PSP_MIN_FREE_KB": str(1 << 40)})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nothing installed", result.stderr)
        self.assertNotIn("would run: cp", result.stdout)

    def test_a_zip_that_does_not_match_its_checksum_stops(self):
        with zipfile.ZipFile(self.zip, "a") as archive:
            archive.writestr("extra", b"changed")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("would run", result.stdout)


if __name__ == "__main__":
    unittest.main()
