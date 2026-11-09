"""Tests for scripts/check-names.py on scratch Git repositories, with a list
that holds the hash of a made-up word."""

import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = (pathlib.Path(__file__).resolve().parents[1]
          / "scripts" / "check-names.py")
WORD = "zorblequint"


class CheckNamesTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.list = pathlib.Path(directory.name) / "words.sha256"
        digest = hashlib.sha256(WORD.encode()).hexdigest()
        self.list.write_text(f"# test list\n{digest}\n")
        self.root = pathlib.Path(directory.name) / "repo"
        self.root.mkdir()
        self.git("init", "-q")
        self.write("README.md", "A clean tree.\n")
        self.commit("Add the readme")

    def git(self, *args):
        subprocess.run(["git", "-c", "user.name=Test",
                        "-c", "user.email=test@example.com",
                        "-c", "commit.gpgsign=false",
                        "-c", "core.autocrlf=false",
                        "-C", self.root.as_posix(), *args],
                       check=True, capture_output=True)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", message)

    def check(self, *args):
        result = subprocess.run(
         [sys.executable, str(SCRIPT), "--list", str(self.list), *args],
         cwd=self.root, capture_output=True, text=True)
        self.assertNotIn(WORD, (result.stdout + result.stderr).lower())
        return result

    def assertFails(self, where, *args):
        result = self.check(*args)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(where, result.stderr)

    def test_a_clean_tree_and_history_pass(self):
        self.assertEqual(self.check().returncode, 0)
        self.assertEqual(self.check("--commits", "HEAD").returncode, 0)

    def test_the_word_in_a_tracked_file_fails_with_its_line(self):
        self.write("source/a.c", f"int a;\n/* from the {WORD}-tool */\n")
        self.git("add", "-A")
        self.assertFails("source/a.c:2")

    def test_case_does_not_matter(self):
        self.write("notes.txt", WORD.upper() + "\n")
        self.git("add", "-A")
        self.assertFails("notes.txt:1")

    def test_the_word_in_a_tracked_file_name_fails_masked(self):
        self.write(f"tools/{WORD.capitalize()}_port.py", "VALUE = 1\n")
        self.git("add", "-A")
        self.assertFails(f"tools/{'*' * len(WORD)}_port.py: in the file name")

    def test_the_word_in_an_untracked_docs_file_name_fails(self):
        self.write(f"docs/notes-{WORD}.md", "Notes.\n")
        self.assertFails("in the file name")

    def test_the_word_in_a_commit_message_fails(self):
        self.git("tag", "base")
        self.commit(f"Take the idea\n\nAs {WORD} does it.")
        self.assertEqual(self.check().returncode, 0)
        self.assertFails("message line 3", "--commits", "base..HEAD")

    def test_a_binary_file_is_skipped(self):
        self.write("res/icon.png", b"\x89PNG\0\0" + WORD.encode())
        self.git("add", "-A")
        self.assertEqual(self.check().returncode, 0)

    def test_a_malformed_list_fails(self):
        self.list.write_text(WORD + "\n")
        self.assertFails("line 1")

    def test_the_repository_list_holds_only_hashes(self):
        text = (SCRIPT.parent / "forbidden-words.sha256").read_text()
        hashes = [line for line in text.splitlines()
                  if not line.startswith("#")]
        self.assertTrue(hashes)
        for line in hashes:
            self.assertRegex(line, r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
