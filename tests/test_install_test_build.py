"""Run scripts/install-test-build.sh --dry-run on a made-up PSP card."""

import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "install-test-build.sh"
CARD = "PSP_2004/PSP/GAME/CAT_Emulators/gpSP"
# What a card holds besides the build: only the logs, reports and crash log
# may ever be removed.
CARD_FILES = {
    "EBOOT.PBP": b"old eboot",
    "media_engine.prx": b"old module",
    "gba_bios.bin": b"bios",
    "tempgba_crash.log": b"crash",
    "dir.ini": b"settings",
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
        self.zip = self.root / "dist" / "GBA-Rush-PSP-0.0.0.zip"
        self.zip.parent.mkdir()
        with zipfile.ZipFile(self.zip, "w") as archive:
            for name in ("EBOOT.PBP", "exception.prx", "media_engine.prx"):
                archive.writestr(f"GBA-Rush-PSP-0.0.0/GBARushPSP/{name}",
                                 b"new " + name.encode())
        digest = hashlib.sha256(self.zip.read_bytes()).hexdigest()
        Path(f"{self.zip}.sha256").write_text(f"{digest}  {self.zip.name}\n")
        self.volumes = self.root / "Volumes"
        for name, data in CARD_FILES.items():
            path = self.volumes / CARD / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def tearDown(self):
        self.temporary.cleanup()

    def run_script(self, *arguments):
        return subprocess.run(
            ["sh", str(SCRIPT), "--dry-run", *arguments, str(self.zip)],
            env={**os.environ, "PSP_VOLUMES": str(self.volumes)},
            capture_output=True, text=True, check=False)

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

    def test_a_zip_that_does_not_match_its_checksum_stops(self):
        with zipfile.ZipFile(self.zip, "a") as archive:
            archive.writestr("extra", b"changed")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("would run", result.stdout)


if __name__ == "__main__":
    unittest.main()
