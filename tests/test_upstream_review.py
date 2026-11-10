"""Tests for scripts/upstream-review.sh on a temporary git repository."""

import datetime
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "upstream-review.sh"
# Keeps the user's git configuration (hooks, signing) out of the test repository.
GIT_ENV = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@example.invalid",
               GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@example.invalid")
# One file under each path docs/UPSTREAM.md tracks.
TRACKED = ["cpu_threaded.c", "gba_memory.c", "video.cc", "sound.c", "serial_proto.c",
           "rfu.c", "bios/open_gba_bios.bin", "gba_over.h", "tests/mipsgen.c"]
# Lookalikes the review must not list.
UNTRACKED = ["libretro/libretro.c", "gba_memory.h", "video.c", "serial_proto.h",
             "mips/mips_emit.h"]
HEADER = re.compile(r"^- Last reviewed upstream commit: `([0-9a-f]{40})`$", re.M)


class UpstreamReviewTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.clone = pathlib.Path(directory.name) / "gpsp"
        self.clone.mkdir()
        self.git("init", "-q")
        self.base = self.commit("cpu.cc", 1)
        self.listed = []
        for day, path in enumerate(TRACKED + UNTRACKED, start=2):
            commit = self.commit(path, day)
            if path in TRACKED:
                self.listed.insert(0, f"{commit} 2024-01-{day:02d} Change {path}")
        self.head = self.git("rev-parse", "HEAD")

    def git(self, *argv, env=GIT_ENV):
        return subprocess.run(["git", "-C", str(self.clone), *argv], env=env,
                              check=True, capture_output=True,
                              text=True).stdout.strip()

    def commit(self, path, day):
        file = self.clone / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(path + "\n")
        date = f"2024-01-{day:02d}T12:00:00+0000"
        self.git("add", path)
        self.git("commit", "-q", "-m", "Change " + path,
                 env=dict(GIT_ENV, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date))
        return self.git("rev-parse", "--short", "HEAD")

    def review(self, *argv, script=SCRIPT):
        return subprocess.run(["sh", str(script), *argv], env=GIT_ENV,
                              capture_output=True, text=True)

    def assert_lists_tracked_commits(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), self.listed)
        self.assertEqual(result.stderr.splitlines(), ["Reviewed up to " + self.head])

    def test_lists_only_commits_that_touch_tracked_paths(self):
        self.assert_lists_tracked_commits(self.review(str(self.clone), self.base))

    def test_last_commit_defaults_to_the_register_header(self):
        tree = self.clone.parent / "tree"
        (tree / "scripts").mkdir(parents=True)
        (tree / "docs").mkdir()
        script = shutil.copy(SCRIPT, tree / "scripts")
        base = self.git("rev-parse", self.base)
        (tree / "docs" / "UPSTREAM.md").write_text(
            f"- Last reviewed upstream commit: `{base}`\n- Last review: 2024-01-01\n")
        self.assert_lists_tracked_commits(self.review(str(self.clone), script=script))

    def test_register_header_names_a_full_commit(self):
        header = HEADER.search((ROOT / "docs" / "UPSTREAM.md").read_text(encoding="utf-8"))
        self.assertIsNotNone(header)
        # The test repository lacks that commit, so the review stops on it.
        result = self.review(str(self.clone))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f"Commit {header.group(1)} is not in", result.stderr)

    def test_missing_commit_fails(self):
        result = self.review(str(self.clone), "0123456789abcdef")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Commit 0123456789abcdef is not in", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_missing_clone_fails(self):
        result = self.review(str(self.clone.parent / "missing"), self.base)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Not the top directory of a git clone", result.stderr)

    def test_subdirectory_of_a_clone_fails(self):
        result = self.review(str(self.clone / "tests"), self.base)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Not the top directory of a git clone", result.stderr)
        self.assertEqual(result.stdout, "")


class CheckAgeTest(unittest.TestCase):
    """The --check-age warning on the register's "Last review" date."""

    REVIEWED = datetime.date(2025, 12, 20)

    def check_age(self, today, reviewed=REVIEWED.isoformat()):
        with tempfile.TemporaryDirectory() as directory:
            tree = pathlib.Path(directory)
            (tree / "scripts").mkdir()
            (tree / "docs").mkdir()
            script = shutil.copy(SCRIPT, tree / "scripts")
            (tree / "docs" / "UPSTREAM.md").write_bytes(
                f"- Last reviewed upstream commit: `{'0' * 40}`\r\n"
                f"- Last review: {reviewed}\r\n".encode())
            return subprocess.run(["sh", str(script), "--check-age", "--today", today],
                                  capture_output=True, text=True)

    def after(self, days):
        return (self.REVIEWED + datetime.timedelta(days=days)).isoformat()

    def test_review_up_to_45_days_old_gives_no_warning(self):
        for days in (0, 44, 45):
            with self.subTest(days=days):
                result = self.check_age(self.after(days))
                self.assertEqual((result.returncode, result.stdout, result.stderr),
                                 (0, "", ""))

    def test_review_older_than_45_days_warns(self):
        for days in (46, 400):
            with self.subTest(days=days):
                result = self.check_age(self.after(days))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"Warning: the last upstream review, on 2025-12-20, is "
                              f"{days} days old", result.stderr)

    def test_08_and_09_are_decimal(self):
        # Shell arithmetic reads a leading 0 as octal, where 08 and 09 are errors.
        result = self.check_age("2026-09-08", "2026-08-09")
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        result = self.check_age("2026-09-23", "2026-08-08")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("on 2026-08-08, is 46 days old", result.stderr)

    def test_unparsable_date_fails(self):
        for reviewed in ("2026-13-01", "2026-11-32", "5 Nov 2026", ""):
            with self.subTest(reviewed=reviewed):
                result = self.check_age(self.after(1), reviewed)
                self.assertEqual(result.returncode, 1)
                self.assertIn("No 'Last review: YYYY-MM-DD' line", result.stderr)
        result = self.check_age("2026/01/01")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Not a YYYY-MM-DD date", result.stderr)

    def test_register_date_parses(self):
        register = (ROOT / "docs" / "UPSTREAM.md").read_text(encoding="utf-8")
        reviewed = re.search(r"^- Last review: (\d{4}-\d{2}-\d{2})$", register, re.M)
        self.assertIsNotNone(reviewed)
        result = subprocess.run(["sh", str(SCRIPT), "--check-age", "--today",
                                 reviewed.group(1)], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stderr), (0, ""))


if __name__ == "__main__":
    unittest.main()
