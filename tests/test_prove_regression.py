"""Tests for scripts/prove-regression.sh on a toy repository.

The toy module source/toy.c gets a bug at tag v1 (v0 predates it); HEAD fixes
it and adds its tests and their Makefile rule. A fake make stands in for make:
it builds build/host/NAME from tests/NAME.c and source/toy.c with HOST_CC
(default cc) when the Makefile has a rule for that target. Needs git and sh.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

FAKE_MAKE = """#!/bin/sh
# make -C DIR TARGET for the toy's one kind of target.
cd "$2" && grep -q "^$3:" Makefile && mkdir -p build/host &&
 exec $HOST_CC -o "$3" "tests/${3#build/host/}.c" source/toy.c
"""

HEAD_MAKEFILE = """build/host/test_toy: tests/test_toy.c source/toy.c
\tmkdir -p build/host
\t$(HOST_CC) -o $@ tests/test_toy.c source/toy.c
"""

TOY_TEST = """int toy_add(int a, int b);
int main(void) { return toy_add(2, 3) == 5 ? 0 : 1; }
"""

# Passes either way but reports the bug the way UBSan does when it recovers.
TOY_PYTHON_TEST = """import pathlib
import sys
import unittest


class ToyTest(unittest.TestCase):
    def test_add(self):
        if "a - b" in pathlib.Path("source/toy.c").read_text():
            print("source/toy.c:1:46: runtime error: wrong sign", file=sys.stderr)
"""


class ProveRegressionTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Path(directory.name)
        fake_make = self.repo / "fake-make"
        fake_make.write_text(FAKE_MAKE, newline="\n")
        fake_make.chmod(0o755)
        # GIT_DIR and friends would point git at the checkout running the tests.
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith("GIT_")}
        self.env.update(MAKE=fake_make.as_posix(), PYTHON=Path(sys.executable).as_posix(),
                        HOST_CC=os.environ.get("HOST_CC", "cc"))
        self.git("init", "-q")
        for key, value in (("user.name", "Test"), ("user.email", "test@example.com"),
                           ("commit.gpgsign", "false"), ("tag.gpgsign", "false"),
                           ("core.autocrlf", "false")):
            self.git("config", key, value)
        self.commit("v0", {"Makefile": "all:\n"})
        self.commit("v1", {"source/toy.c": "int toy_add(int a, int b) { return a - b; }\n"})
        self.commit(None, {"source/toy.c": "int toy_add(int a, int b) { return a + b; }\n",
                           "Makefile": HEAD_MAKEFILE, "tests/test_toy.c": TOY_TEST,
                           "tests/test_toy.py": TOY_PYTHON_TEST})
        # sh needs LF, whatever line endings this checkout has.
        script = self.repo / "scripts/prove-regression.sh"
        script.parent.mkdir()
        script.write_bytes((ROOT / "scripts/prove-regression.sh").read_bytes()
                           .replace(b"\r\n", b"\n"))

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo, env=self.env, check=True,
                              capture_output=True, text=True).stdout

    def commit(self, tag, files):
        for name, text in files.items():
            path = self.repo / name
            path.parent.mkdir(exist_ok=True)
            path.write_text(text, newline="\n")
        self.git("add", *files)
        self.git("commit", "-q", "-m", tag or "Fix")
        if tag:
            self.git("tag", tag)

    def prove(self, ref, test):
        """The exit status and the outcome word; the worktree must be gone."""
        result = subprocess.run(["sh", "scripts/prove-regression.sh", ref, test],
                                cwd=self.repo, env=self.env, capture_output=True,
                                text=True)
        self.assertFalse((self.repo / "build/prove").exists(), result.stdout)
        self.assertEqual(self.git("worktree", "list", "--porcelain").count("worktree "), 1)
        lines = result.stdout.splitlines()
        return result.returncode, lines[-1].split(":")[0] if lines else result.stderr

    def test_a_test_that_catches_the_bug_fails_as_expected(self):
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))

    def test_a_test_that_passes_on_ref_does_not_catch_the_bug(self):
        self.assertEqual(self.prove("HEAD", "test_toy"), (1, "PASSES-ON-REF"))

    def test_a_test_that_does_not_build_on_ref_is_not_applicable(self):
        self.assertEqual(self.prove("v0", "test_toy"), (2, "NOT-APPLICABLE"))

    def test_a_sanitizer_report_fails_even_when_the_test_exits_zero(self):
        self.assertEqual(self.prove("v1", "test_toy.py"), (0, "FAIL-AS-EXPECTED"))
        self.assertEqual(self.prove("HEAD", "test_toy.py"), (1, "PASSES-ON-REF"))

    def test_a_worktree_left_by_a_killed_run_is_replaced(self):
        stale = self.repo / "build/prove/v1"
        stale.mkdir(parents=True)
        (stale / "leftover").write_text("")
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))

    def test_bad_arguments_are_errors(self):
        for ref, test in (("v9", "test_toy"), ("../v1", "test_toy"), ("v1", "test_none"),
                          ("v1", "../test_toy")):
            with self.subTest(ref=ref, test=test):
                self.assertEqual(self.prove(ref, test)[0], 3)


if __name__ == "__main__":
    unittest.main()
