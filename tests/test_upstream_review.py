"""Tests for scripts/upstream-review.sh on a temporary git repository."""

import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "upstream-review.sh"
# Keeps the user's git configuration (hooks, signing) out of the test repository.
GIT_ENV = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@example.invalid",
               GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@example.invalid")


class UpstreamReviewTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.clone = pathlib.Path(directory.name) / "gpsp"
        self.clone.mkdir()
        self.git("init", "-q")
        self.base = self.commit("cpu.cc", "Import the core", "2024-01-01")
        self.listed = self.commit("cpu_threaded.c", "Fix a CPU flag", "2024-01-02")
        self.commit("libretro/libretro.c", "Change the frontend", "2024-01-03")

    def git(self, *argv, env=GIT_ENV):
        return subprocess.run(["git", "-C", str(self.clone), *argv], env=env,
                              check=True, capture_output=True,
                              text=True).stdout.strip()

    def commit(self, path, subject, day):
        file = self.clone / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(subject + "\n")
        date = day + "T12:00:00+0000"
        self.git("add", path)
        self.git("commit", "-q", "-m", subject,
                 env=dict(GIT_ENV, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date))
        return self.git("rev-parse", "--short", "HEAD")

    def review(self, *argv):
        return subprocess.run(["sh", str(SCRIPT), *argv], env=GIT_ENV,
                              capture_output=True, text=True)

    def test_lists_only_commits_that_touch_tracked_paths(self):
        result = self.review(str(self.clone), self.base)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         [self.listed + " 2024-01-02 Fix a CPU flag"])

    def test_missing_commit_fails(self):
        result = self.review(str(self.clone), "0123456789abcdef")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Commit 0123456789abcdef is not in", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_missing_clone_fails(self):
        missing = self.clone.parent / "missing"
        result = self.review(str(missing), self.base)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Not a git clone of libretro/gpsp", result.stderr)

    def test_last_commit_defaults_to_the_register_header(self):
        # 5819380 is the commit docs/UPSTREAM.md records; the test repository lacks it.
        result = self.review(str(self.clone))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Commit 5819380", result.stderr)


if __name__ == "__main__":
    unittest.main()
